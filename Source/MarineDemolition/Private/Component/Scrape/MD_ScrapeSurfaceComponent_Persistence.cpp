#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "TimerManager.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Gameplay/Scrape/MD_ScrapeDecalLayout.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "Gameplay/Scrape/MD_ScrapeMeshSurfaceProfile.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "RHITypes.h"
#include "Stats/Stats.h"
#include "World/Contamination/MD_Barnacle.h"
#include "World/Contamination/MD_ContaminationItem.h"
#include "World/Sector/MD_Sector.h"
#include "World/Wall/MD_WallPiece.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 이 파일: 밤샘 악화 · Day 스냅샷 입출력 · 오염물 드롭

using namespace MD_ScrapeSurfaceComponentInternal;

void UMD_ScrapeSurfaceComponent::ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	const int32 GridWidth = ContaminationGridDimensions.X;
	const int32 GridHeight = ContaminationGridDimensions.Y;
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (ContaminationCells.Num() == 0
		|| GridWidth <= 0
		|| GridHeight <= 0
		|| !FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return;
	}

	// NaN/무한대가 그대로 들어가면 셀 값이 오염돼 이후 모든 판정이 무너진다.
	const float SafeAmount = FMath::IsFinite(Amount)
		? FMath::Clamp(Amount, 0.0f, 1.0f)
		: 0.0f;
	const int32 SafePatchRadiusCells = FMath::Max(0, PatchRadiusCells);
	if (SafeAmount <= UE_SMALL_NUMBER)
	{
		return;
	}

	// 기획 03 §3 "새로운 소형 오염 분출" — 앞뒤가 각자 따로 분출한다.
	// 같은 좌표를 양면에 쓰면 벽을 관통한 하나의 얼룩처럼 보이는데,
	// 기획이 말하는 것은 면마다 독립적으로 생기는 분출이다.
	static constexpr EMD_ScrapeSurfaceFace PatchTargetFaces[] = {
		EMD_ScrapeSurfaceFace::Front,
		EMD_ScrapeSurfaceFace::Back };

	TArray<FMD_ScrapeCellChange> Changes;
	for (const EMD_ScrapeSurfaceFace TargetFace : PatchTargetFaces)
	{
		const FIntPoint Center(
			FMath::RandRange(0, GridWidth - 1),
			FMath::RandRange(0, GridHeight - 1));

		for (int32 Y = Center.Y - SafePatchRadiusCells;
			Y <= Center.Y + SafePatchRadiusCells;
			++Y)
		{
			for (int32 X = Center.X - SafePatchRadiusCells;
				X <= Center.X + SafePatchRadiusCells;
				++X)
			{
				if (X < 0 || X >= GridWidth || Y < 0 || Y >= GridHeight)
				{
					continue;
				}

				// 면 내부 번호를 그대로 쓰면 최대값이 CellsPerFace - 1이라
				// 뒷면 구간 [CellsPerFace, 2 * CellsPerFace)에 영영 닿지 못한다.
				const int32 CombinedCellIndex =
					FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
						TargetFace,
						X + Y * GridWidth,
						CellsPerFace);
				if (!ContaminationCells.IsValidIndex(CombinedCellIndex))
				{
					continue;
				}

				FMD_ScrapeSurfaceCell& Cell =
					ContaminationCells[CombinedCellIndex];
				const float PreviousRemaining = Cell.RemainingContamination;
				const float TargetRemaining = FMath::Clamp(
					PreviousRemaining + SafeAmount,
					0.0f,
					1.0f);
				if (FMath::IsNearlyEqual(
					PreviousRemaining,
					TargetRemaining,
					UE_SMALL_NUMBER))
				{
					continue;
				}

				Cell.RemainingContamination = TargetRemaining;

				// 새 분출은 이 셀이 짊어진 오염 총량 자체를 늘린다.
				// InitialContamination은 제거율의 분모이자 마스크·완료 판정의
				// 게이트다. 여기를 같이 올리지 않으면 원래 깨끗했던 칸은
				// 마스크가 0으로 고정되고(MD_ScrapeMaskAtlas.cpp SafeInitial 분기)
				// 완료 판정에서도 건너뛰어져(MD_ScrapeEvaluator.cpp IsSurfaceFullyClean)
				// 새 오염이 보이지도 세어지지도 않는다.
				Cell.InitialContamination = FMath::Max(
					Cell.InitialContamination,
					TargetRemaining);

				FMD_ScrapeCellChange Change;
				Change.CellIndex = CombinedCellIndex;
				Change.GridCoordinate = FIntPoint(X, Y);
				// 재도포는 제거가 아니므로 제거량은 0이고 깨끗해지지도 않는다.
				Change.RemovedAmount = 0.0f;
				Change.RemainingAmountAfter = Cell.RemainingContamination;
				Change.bBecameClean = false;
				Changes.Add(Change);
			}
		}
	}

	if (Changes.IsEmpty())
	{
		return;
	}

	RefreshContaminationCleanRatioCache();
	// 양면이 모두 오염됐으면 완료 방송이 다시 무장된다. 한쪽 면이 아직 깨끗하면
	// OR 게이트상 벽은 여전히 "완료"라 무장되지 않는다. 이 의미가 맞는지는
	// 별도 결정 대기 중이며, 같은 한 줄이 RestoreContaminationCells에도 있다.
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
	UpdateContaminationMask(Changes);
	UpdateContaminationVisualization(Changes);
	PublishContaminationChanges(Changes);
}


void UMD_ScrapeSurfaceComponent::ExportContaminationDeltas(
	TArray<int32>& OutCellIndices,
	TArray<float>& OutInitialAmounts,
	TArray<float>& OutRemainingAmounts) const
{
	OutCellIndices.Reset();
	OutInitialAmounts.Reset();
	OutRemainingAmounts.Reset();

	for (int32 Index = 0; Index < ContaminationCells.Num(); ++Index)
	{
		const FMD_ScrapeSurfaceCell& Cell = ContaminationCells[Index];

		// 긁힌 셀은 남은 양이 총량보다 적다.
		const bool bScraped =
			Cell.RemainingContamination != Cell.InitialContamination;

		// 밤샘 분출을 맞았지만 아직 손대지 않은 셀은 남은 양과 총량이 같아
		// 위 조건에 걸리지 않는다. 생성 기준값과 비교해야 잡힌다.
		// 기준 배열이 없거나 길이가 어긋나면 판단 근거가 없으므로 분출로 보지 않는다.
		const bool bBurstSinceGeneration =
			ContaminationGeneratedInitialAmounts.IsValidIndex(Index)
			&& Cell.InitialContamination
				!= ContaminationGeneratedInitialAmounts[Index];

		if (bScraped || bBurstSinceGeneration)
		{
			OutCellIndices.Add(Index);
			OutInitialAmounts.Add(Cell.InitialContamination);
			OutRemainingAmounts.Add(Cell.RemainingContamination);
		}
	}
}


void UMD_ScrapeSurfaceComponent::RestoreContaminationCells(
	const TArray<int32>& CellIndices,
	const TArray<float>& InitialAmounts,
	const TArray<float>& RemainingAmounts)
{
	// 총량 배열은 이 필드가 없던 구세션 스냅샷을 위해 비어 있어도 받아준다.
	// 그 경우 총량은 생성값을 그대로 두고 남은 오염량만 복원한다.
	const bool bHasInitialAmounts = !InitialAmounts.IsEmpty();
	if (!HasAuthority()
		|| CellIndices.Num() != RemainingAmounts.Num()
		|| (bHasInitialAmounts && CellIndices.Num() != InitialAmounts.Num()))
	{
		return;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	const int32 GridWidth = ContaminationGridDimensions.X;
	if (ContaminationCells.Num() == 0 || GridWidth <= 0)
	{
		return;
	}

	TArray<FMD_ScrapeCellChange> Changes;
	for (int32 i = 0; i < CellIndices.Num(); ++i)
	{
		const int32 Index = CellIndices[i];
		// 그리드 크기가 바뀐 구세션 스냅샷의 셀은 무시
		if (!ContaminationCells.IsValidIndex(Index))
		{
			continue;
		}

		FMD_ScrapeSurfaceCell& Cell = ContaminationCells[Index];

		// 총량을 먼저 세운다. 남은 오염량이 총량으로 클램프되므로 순서가 뒤바뀌면
		// 밤샘 분출로 올라간 값이 생성 기준값까지 깎여 얼룩이 사라진다.
		if (bHasInitialAmounts && FMath::IsFinite(InitialAmounts[i]))
		{
			Cell.InitialContamination = FMath::Clamp(
				InitialAmounts[i], 0.0f, 1.0f);
		}

		Cell.RemainingContamination = FMath::Clamp(
			RemainingAmounts[i], 0.f, Cell.InitialContamination);

		FMD_ScrapeCellChange Change;
		Change.CellIndex = Index;
		Change.GridCoordinate = FIntPoint(Index % GridWidth, Index / GridWidth);
		Change.RemainingAmountAfter = Cell.RemainingContamination;
		Changes.Add(Change);
	}

	if (Changes.IsEmpty())
	{
		return;
	}

	// 앞/뒤 제거 비율을 함께 갱신한다.
	RefreshContaminationCleanRatioCache();
	// 어제 완전 제거된 벽이면 완료 이벤트가 재방송되지 않게 이미 방송된 것으로 마킹
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
	// 복원한 셀 상태가 화면에 보이도록 런타임 G8 마스크도 함께 갱신한다.
	UpdateContaminationMask(Changes);
	UpdateContaminationVisualization(Changes);
	PublishContaminationChanges(Changes);
}


bool UMD_ScrapeSurfaceComponent::TryCalculateContaminationItemSpawnTransform(
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& SurfacePositionCm,
	FTransform& OutSpawnTransform) const
{
	OutSpawnTransform = FTransform::Identity;
	if (!SurfaceMesh)
	{
		return false;
	}

	const FTransform SurfaceTransform = SurfaceMesh->GetComponentTransform();

	FVector WorldPosition = FVector::ZeroVector;
	FVector HorizontalAxis = FVector::ZeroVector;
	FVector FrontNormalLocal = FVector::ZeroVector;
	FVector VerticalAxis = FVector::ZeroVector;
	if (!FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			SurfaceTransform,
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceCenterLocal,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			WorldPosition)
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxis,
			FrontNormalLocal,
			VerticalAxis))
	{
		return false;
	}

	// 벽면 안쪽에서 스폰하면 물리가 벽을 밀어내며 튀어나간다. 긁은 면 바깥으로 뺀다.
	const FVector OutwardNormal =
		SurfaceTransform.TransformVectorNoScale(
			SurfaceFace == EMD_ScrapeSurfaceFace::Back
				? -FrontNormalLocal
				: FrontNormalLocal).GetSafeNormal();
	if (OutwardNormal.IsNearlyZero())
	{
		return false;
	}

	const float SafeOffsetCm = FMath::IsFinite(ContaminationItemSpawnOffsetCm)
		? FMath::Max(0.0f, ContaminationItemSpawnOffsetCm)
		: 0.0f;

	OutSpawnTransform = FTransform(
		FRotationMatrix::MakeFromZ(OutwardNormal).Rotator(),
		WorldPosition + OutwardNormal * SafeOffsetCm);
	return true;
}


int32 UMD_ScrapeSurfaceComponent::DropContaminationItems(
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const float RemovedAmount,
	const FVector2D& SurfacePositionCm)
{
	const int32 FaceSlot =
		SurfaceFace == EMD_ScrapeSurfaceFace::Back ? 1 : 0;
	if (SurfaceFace != EMD_ScrapeSurfaceFace::Front
		&& SurfaceFace != EMD_ScrapeSurfaceFace::Back)
	{
		return 0;
	}

	if (!HasAuthority()
		|| !ContaminationItemClass
		|| ContaminationItemsPerFace <= 0
		|| !FMath::IsFinite(RemovedAmount)
		|| RemovedAmount <= 0.0f)
	{
		return 0;
	}

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| CellsPerFace <= 0)
	{
		return 0;
	}

	// 문턱을 셀 수로 잡는다. 셀 하나의 최대 오염량이 1.0이므로 면 전체를 다 긁으면
	// 정확히 ContaminationItemsPerFace개가 나온다. 격자 크기가 달라져도 개수는 같고,
	// 덜 더러운 벽에서는 자연히 덜 나온다.
	const float AmountPerItem =
		static_cast<float>(CellsPerFace)
		/ static_cast<float>(ContaminationItemsPerFace);
	if (AmountPerItem <= UE_SMALL_NUMBER)
	{
		return 0;
	}

	// 문턱 비교에 여유를 둔다. 면을 정확히 다 긁어도 AmountPerItem을 정수 번 빼면
	// 마지막에 1e-7 수준이 모자라 개수가 하나 줄어든다. 그러면 "다 긁었는데 3개가
	// 아니라 2개"가 되어 기획 개수와 어긋난다.
	const float ItemThreshold = AmountPerItem - UE_KINDA_SMALL_NUMBER;

	float& PendingAmount = PendingContaminationItemAmounts[FaceSlot];
	PendingAmount += RemovedAmount;
	if (PendingAmount < ItemThreshold)
	{
		return 0;
	}

	FTransform SpawnTransform = FTransform::Identity;
	if (!TryCalculateContaminationItemSpawnTransform(
			SurfaceFace,
			SurfacePositionCm,
			SpawnTransform))
	{
		// 위치를 못 구하면 쌓인 양을 흘려보내지 않고 다음 스트로크로 넘긴다.
		return 0;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return 0;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	int32 SpawnedCount = 0;
	while (PendingAmount >= ItemThreshold)
	{
		PendingAmount = FMath::Max(0.0f, PendingAmount - AmountPerItem);

		if (World->SpawnActor<AMD_ContaminationItem>(
				ContaminationItemClass,
				SpawnTransform,
				SpawnParams))
		{
			++SpawnedCount;
		}
	}

	return SpawnedCount;
}
