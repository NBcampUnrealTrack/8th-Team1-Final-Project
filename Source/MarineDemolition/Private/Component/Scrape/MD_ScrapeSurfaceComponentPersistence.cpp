#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Math/RotationMatrix.h"
#include "World/Contamination/MD_ContaminationItem.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 밤샘 악화 · Day 스냅샷 입출력 · 오염물 드롭

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

	const float SafeAmount = FMath::IsFinite(Amount)
		? FMath::Clamp(Amount, 0.0f, 1.0f)
		: 0.0f;
	const int32 SafePatchRadiusCells = FMath::Max(0, PatchRadiusCells);
	if (SafeAmount <= UE_SMALL_NUMBER)
	{
		return;
	}

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
				Cell.InitialContamination = FMath::Max(
					Cell.InitialContamination,
					TargetRemaining);

				FMD_ScrapeCellChange Change;
				Change.CellIndex = CombinedCellIndex;
				Change.GridCoordinate = FIntPoint(X, Y);
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
		const bool bScraped = Cell.RemainingContamination != Cell.InitialContamination;

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
		? FMath::Max(0.0f, ContaminationItemSpawnOffsetCm)	: 0.0f;

	OutSpawnTransform = FTransform(
		FRotationMatrix::MakeFromZ(OutwardNormal).Rotator(),
		WorldPosition + OutwardNormal * SafeOffsetCm);
	return true;
}


int32 UMD_ScrapeSurfaceComponent::DropContaminationItems(const EMD_ScrapeSurfaceFace SurfaceFace,
	const float RemovedAmount, const FVector2D& SurfacePositionCm)
{
	const int32 FaceSlot = SurfaceFace == EMD_ScrapeSurfaceFace::Back ? 1 : 0;
	if (SurfaceFace != EMD_ScrapeSurfaceFace::Front	&& SurfaceFace != EMD_ScrapeSurfaceFace::Back)
	{
		return 0;
	}

	if (!HasAuthority()	|| !ContaminationItemClass || ContaminationItemsPerFace <= 0
		|| !FMath::IsFinite(RemovedAmount) || RemovedAmount <= 0.0f)
	{
		return 0;
	}

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(ContaminationGridDimensions,
			CellsPerFace,TotalCellCount) || CellsPerFace <= 0)
	{
		return 0;
	}

	const float AmountPerItem =	static_cast<float>(CellsPerFace) / static_cast<float>(ContaminationItemsPerFace);
	if (AmountPerItem <= UE_SMALL_NUMBER)
	{
		return 0;
	}


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
