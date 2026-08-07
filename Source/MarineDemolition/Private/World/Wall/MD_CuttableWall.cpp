#include "World/Wall/MD_CuttableWall.h"

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
#include "World/Sector/MD_Sector.h"
#include "World/Wall/MD_WallPiece.h"

/**
 * 긁기 한 번의 비용을 구간별로 나눠 재는 통계.
 *
 * 콘솔에 `stat MD_Scrape`를 입력하면 화면에 구간별 밀리초가 뜬다.
 * 어느 구간이 실제로 비싼지 숫자로 확인한 뒤 최적화 대상을 정하기 위한 것이다.
 * Shipping 빌드에서는 STATS가 꺼져 매크로가 통째로 사라진다.
 */
DECLARE_STATS_GROUP(TEXT("MD Scrape"), STATGROUP_MD_Scrape, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Stroke 전체"), STAT_MD_ScrapeStrokeTotal, STATGROUP_MD_Scrape);
DECLARE_CYCLE_STAT(TEXT("셀 판정 (Evaluator)"), STAT_MD_ScrapeEvaluate, STATGROUP_MD_Scrape);
DECLARE_CYCLE_STAT(TEXT("제거율 캐시"), STAT_MD_ScrapeCleanRatio, STATGROUP_MD_Scrape);
DECLARE_CYCLE_STAT(TEXT("G8 마스크 갱신"), STAT_MD_ScrapeMaskUpdate, STATGROUP_MD_Scrape);
DECLARE_CYCLE_STAT(TEXT("ISM 오버레이 갱신"), STAT_MD_ScrapeOverlayUpdate, STATGROUP_MD_Scrape);
DECLARE_CYCLE_STAT(TEXT("복제 발행"), STAT_MD_ScrapePublish, STATGROUP_MD_Scrape);
DECLARE_CYCLE_STAT(TEXT("완료 판정"), STAT_MD_ScrapeFullyRemovedCheck, STATGROUP_MD_Scrape);

namespace
{
	const FLinearColor NeutralWallColor(0.4f, 0.4f, 0.45f);
	const FName ContaminationMaskTextureParameterName(TEXT("MaskTexture"));
	const FName ContaminationMaskVScaleParameterName(TEXT("MaskVScale"));
	const FName ContaminationMaskVOffsetParameterName(TEXT("MaskVOffset"));
	const FName ContaminationMaskUFlipParameterName(TEXT("MaskUFlip"));
	const FName ContaminationMaskHalfTexelUParameterName(TEXT("MaskHalfTexelU"));
	const FName ContaminationMaskHalfTexelVParameterName(TEXT("MaskHalfTexelV"));
}

void FMD_ReplicatedScrapeCellStateArray::SetOwner(
	AMD_CuttableWall* InOwner)
{
	Owner = InOwner;
	CellToItemIndex.Reset();

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const int32 CellIndex = Items[ItemIndex].GetCellIndex();
		if (CellIndex != INDEX_NONE)
		{
			CellToItemIndex.Add(CellIndex, ItemIndex);
		}
	}
}

void FMD_ReplicatedScrapeCellStateArray::SetCellRemaining(
	const int32 CellIndex,
	const float RemainingContamination)
{
	if (!Owner
		|| !Owner->HasAuthority()
		|| !Owner->ContaminationCells.IsValidIndex(CellIndex)
		|| !FMath::IsFinite(RemainingContamination))
	{
		return;
	}

	uint16 EncodedCellIndex = FMD_ScrapeReplicationCodec::InvalidCellIndex;
	if (!FMD_ScrapeReplicationCodec::TryEncodeCellIndex(
			CellIndex,
			EncodedCellIndex))
	{
		return;
	}

	const float InitialContamination = FMath::Clamp(
		Owner->ContaminationCells[CellIndex].InitialContamination,
		0.0f,
		1.0f);
	const float SafeRemaining = FMath::Clamp(
		RemainingContamination,
		0.0f,
		InitialContamination);
	const bool bMatchesInitial = FMath::IsNearlyEqual(
		SafeRemaining,
		InitialContamination,
		UE_SMALL_NUMBER);
	// 실제로 보내는 값은 양자화 결과다. 같은 바이트로 떨어지는 갱신은
	// 네트워크로 내보내지 않는다.
	const uint8 QuantizedRemaining =
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			SafeRemaining,
			Owner->ScrapeSettings.CleanTolerance);

	if (const int32* ExistingItemIndex = CellToItemIndex.Find(CellIndex))
	{
		if (Items.IsValidIndex(*ExistingItemIndex))
		{
			if (bMatchesInitial)
			{
				const int32 RemovedItemIndex = *ExistingItemIndex;
				Items.RemoveAtSwap(RemovedItemIndex, 1, EAllowShrinking::No);
				CellToItemIndex.Remove(CellIndex);
				if (Items.IsValidIndex(RemovedItemIndex))
				{
					CellToItemIndex.FindOrAdd(
						Items[RemovedItemIndex].GetCellIndex()) =
							RemovedItemIndex;
				}
				MarkArrayDirty();
				return;
			}

			FMD_ReplicatedScrapeCellState& Item = Items[*ExistingItemIndex];
			if (Item.QuantizedRemainingContamination != QuantizedRemaining)
			{
				Item.QuantizedRemainingContamination = QuantizedRemaining;
				MarkItemDirty(Item);
			}
			return;
		}

		CellToItemIndex.Remove(CellIndex);
	}

	if (bMatchesInitial)
	{
		return;
	}

	const int32 NewItemIndex = Items.Emplace(
		EncodedCellIndex,
		QuantizedRemaining);
	CellToItemIndex.Add(CellIndex, NewItemIndex);
	MarkItemDirty(Items[NewItemIndex]);
}

void FMD_ReplicatedScrapeCellStateArray::ResetState()
{
	if (!Items.IsEmpty())
	{
		Items.Reset();
		MarkArrayDirty();
	}

	CellToItemIndex.Reset();
}

void FMD_ReplicatedScrapeCellStateArray::PreReplicatedRemove(
	const TArrayView<int32>& RemovedIndices,
	const int32)
{
	if (Owner)
	{
		Owner->HandleReplicatedContaminationState(RemovedIndices, true);
	}
}

void FMD_ReplicatedScrapeCellStateArray::PostReplicatedAdd(
	const TArrayView<int32>& AddedIndices,
	const int32)
{
	for (const int32 ItemIndex : AddedIndices)
	{
		if (Items.IsValidIndex(ItemIndex))
		{
			const int32 CellIndex = Items[ItemIndex].GetCellIndex();
			if (CellIndex != INDEX_NONE)
			{
				CellToItemIndex.Add(CellIndex, ItemIndex);
			}
		}
	}

	if (Owner)
	{
		Owner->HandleReplicatedContaminationState(AddedIndices, false);
	}
}

void FMD_ReplicatedScrapeCellStateArray::PostReplicatedChange(
	const TArrayView<int32>& ChangedIndices,
	const int32)
{
	if (Owner)
	{
		Owner->HandleReplicatedContaminationState(ChangedIndices, false);
	}
}

AMD_CuttableWall::AMD_CuttableWall()
{
	PrimaryActorTick.bCanEverTick = false;
	// bHasSupport 복제용 (원격 클라이언트의 절단 판정 일치)
	bReplicates = true;
	ContaminationGridDimensions = FIntPoint(30, 25);
	InitialContaminationPerCell = 1.0f;
	bUseProceduralContamination = false;
	ContaminationNoiseScaleCm = 75.0f;
	ContaminationNoiseSeed = 1337;
	ContaminationCoverage = 0.55f;
	ContaminationMinimumAmount = 0.25f;
	ContaminationSurfaceSizeOverrideCm = FVector2D::ZeroVector;
	ContaminationSurfaceNormalAxis = EMD_ScrapeSurfaceNormalAxis::Auto;
	ContaminationOverlayOffsetCm = 0.25f;
	ContaminationOverlayThicknessCm = 0.35f;
	bShowContaminationOverlay = true;
	ContaminationDecalMaterial = nullptr;
	bShowContaminationDecals = true;
	ContaminationDecalProjectionDepthCm = 1.0f;
	bAutoFitContaminationDecalDepth = true;
	ContaminationDecalDepthFitMarginCm = 1.0f;
	ContaminationDecalSurfaceOffsetCm = 0.05f;
	ContaminationDecalEdgeInsetCm = 2.0f;
	ContaminationDecalSortOrder = 0;
	ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
	ResolvedContaminationSurfaceNormalAxis =
		EMD_ScrapeSurfaceNormalAxis::Y;
	ResolvedFrontDecalDepthCm = 0.0f;
	ResolvedBackDecalDepthCm = 0.0f;
	ResolvedContaminationDecalEdgeInsetCm = 0.0f;
	ContaminationSurfaceCenterLocal = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMinimum = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMaximum = FVector::ZeroVector;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
	bContaminationDecalsConfigured = false;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	ReplicatedContaminationState.SetOwner(this);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 특정 Cube를 벽 크기로 찌그러뜨리지 않는다. 실제 메시 또는 Blueprint가 크기를 결정한다.
	Mesh->SetRelativeScale3D(FVector::OneVector);

	ContaminationOverlay = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ContaminationOverlay"));
	ContaminationOverlay->SetupAttachment(Mesh);
	ContaminationOverlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContaminationOverlay->SetGenerateOverlapEvents(false);
	ContaminationOverlay->SetCanEverAffectNavigation(false);
	ContaminationOverlay->SetReceivesDecals(false);
	ContaminationOverlay->CastShadow = false;

	FrontContaminationDecal = CreateDefaultSubobject<UDecalComponent>(
		TEXT("FrontContaminationDecal"));
	FrontContaminationDecal->SetupAttachment(Mesh);
	FrontContaminationDecal->SetAbsolute(false, false, true);
	FrontContaminationDecal->SetRelativeScale3D(FVector::OneVector);
	FrontContaminationDecal->SetFadeScreenSize(0.0f);
	FrontContaminationDecal->SetVisibility(false);
	FrontContaminationDecal->SetHiddenInGame(true);

	BackContaminationDecal = CreateDefaultSubobject<UDecalComponent>(
		TEXT("BackContaminationDecal"));
	BackContaminationDecal->SetupAttachment(Mesh);
	BackContaminationDecal->SetAbsolute(false, false, true);
	BackContaminationDecal->SetRelativeScale3D(FVector::OneVector);
	BackContaminationDecal->SetFadeScreenSize(0.0f);
	BackContaminationDecal->SetVisibility(false);
	BackContaminationDecal->SetHiddenInGame(true);
}

void AMD_CuttableWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터 변경 시 작업면 크기 갱신
	ResolveContaminationSurfaceGeometry();

	// 스캔 전에는 타입 구분 없이 중립 회색
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}

}

void AMD_CuttableWall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_CuttableWall, bHasSupport);
	DOREPLIFETIME(AMD_CuttableWall, ReplicatedContaminationState);
}

void AMD_CuttableWall::ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells)
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
	if (ContaminationCells.Num() == 0 || GridWidth <= 0 || GridHeight <= 0)
	{
		return;
	}

	// 무작위 중심 주변 사각 패치를 리필해 얼룩처럼 보이게
	const FIntPoint Center(FMath::RandRange(0, GridWidth - 1), FMath::RandRange(0, GridHeight - 1));

	TArray<FMD_ScrapeCellChange> Changes;
	for (int32 Y = Center.Y - PatchRadiusCells; Y <= Center.Y + PatchRadiusCells; ++Y)
	{
		for (int32 X = Center.X - PatchRadiusCells; X <= Center.X + PatchRadiusCells; ++X)
		{
			if (X < 0 || X >= GridWidth || Y < 0 || Y >= GridHeight)
			{
				continue;
			}

			const int32 Index = X + Y * GridWidth;
			FMD_ScrapeSurfaceCell& Cell = ContaminationCells[Index];
			const float PreviousRemaining = Cell.RemainingContamination;
			const float TargetRemaining = FMath::Clamp(
				Cell.RemainingContamination + Amount, 0.f, Cell.InitialContamination);
			if (FMath::IsNearlyEqual(
				PreviousRemaining,
				TargetRemaining,
				UE_SMALL_NUMBER))
			{
				continue;
			}

			Cell.RemainingContamination = TargetRemaining;

			FMD_ScrapeCellChange Change;
			Change.CellIndex = Index;
			Change.GridCoordinate = FIntPoint(X, Y);
			Change.RemainingAmountAfter = Cell.RemainingContamination;
			Changes.Add(Change);
		}
	}

	RefreshContaminationCleanRatioCache();
	// 오염이 다시 생겼으므로 완료 방송을 다시 허용
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
	UpdateContaminationMask(Changes);
	UpdateContaminationVisualization(Changes);
	PublishContaminationChanges(Changes);
}

void AMD_CuttableWall::ExportContaminationDeltas(
	TArray<int32>& OutCellIndices,
	TArray<float>& OutRemainingAmounts) const
{
	OutCellIndices.Reset();
	OutRemainingAmounts.Reset();

	for (int32 Index = 0; Index < ContaminationCells.Num(); ++Index)
	{
		const FMD_ScrapeSurfaceCell& Cell = ContaminationCells[Index];
		if (Cell.RemainingContamination != Cell.InitialContamination)
		{
			OutCellIndices.Add(Index);
			OutRemainingAmounts.Add(Cell.RemainingContamination);
		}
	}
}

void AMD_CuttableWall::RestoreContaminationCells(
	const TArray<int32>& CellIndices,
	const TArray<float>& RemainingAmounts)
{
	if (!HasAuthority() || CellIndices.Num() != RemainingAmounts.Num())
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

void AMD_CuttableWall::BeginPlay()
{
	Super::BeginPlay();
	ReplicatedContaminationState.SetOwner(this);

	// 런타임 오염 상태 초기화
	InitializeContaminationSurface();

	// Transient인 MID는 레벨 로드/PIE 복제 시 유실되므로 런타임에서 재생성
	MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}

	InitializeContaminationVisualization();

	if (!HasAuthority())
	{
		ApplyAllReplicatedContaminationState();
	}
}

void AMD_CuttableWall::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	SetContaminationDecalsVisible(false);
	ReleaseContaminationMask();
	FrontContaminationDecalMID = nullptr;
	BackContaminationDecalMID = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AMD_CuttableWall::Cut(AActor* Cutter)
{
	if (!HasAuthority() || bCut)
	{
		return;
	}

	// 전처리 미달이면 절단 잠김
	if (!CanCut())
	{
		const float Gate = (WallType == EMD_WallType::Strut) ? StrutCutGate : PartitionCutGate;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow,
			                                 FString::Printf(
				                                 TEXT("전처리 미완료: %.0f%% / %.0f%%"),
				                                 GetContaminationDemolitionReadyRatio() * 100.0f,
				                                 Gate));
		}
		return;
	}

	bCut = true;

	if (WallType == EMD_WallType::Strut && !bHasSupport)
	{
		// 벽이 아니라 "절단자가 서 있는 섹터"가 무너진다
		if (AMD_Sector* Sector = AMD_Sector::FindSectorContaining(Cutter))
		{
			Sector->CollapseCeiling();
			OnStrutCollapsed.Broadcast(this, Cutter);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MD_CuttableWall::Cut - 절단자가 속한 섹터를 찾지 못함"));
		}
	}
	else
	{
		// 칸막이 또는 지지 상태 지주 = 철거 성공 (무지지 지주는 붕괴로 전환, 점수 없음)
		OnWallDemolished.Broadcast(this, Cutter);
	}

	// 파편 드랍: 제거율 비율로 깨끗/오염 배분 (기획 §2.5: 제거율 70% → 7:3)
	const int32 CleanCount = FMath::RoundToInt(
		static_cast<float>(PieceCount) * GetContaminationCleanRatio());

	const FVector Origin = GetActorLocation();
	const FVector Extent = Mesh->Bounds.BoxExtent;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 i = 0; i < PieceCount; ++i)
	{
		const FVector SpawnLoc(
				Origin.X + FMath::FRandRange(-Extent.X, Extent.X) * 0.8f,
				Origin.Y + FMath::FRandRange(-Extent.Y, Extent.Y),
				Origin.Z + FMath::FRandRange(-Extent.Z, Extent.Z) * 0.8f);
		const FRotator SpawnRot(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f);

		AMD_WallPiece* Piece = GetWorld()->SpawnActor<AMD_WallPiece>(PieceClass, SpawnLoc, SpawnRot, SpawnParams);
		if (Piece)
		{
			Piece->SetActorScale3D(FVector(FMath::FRandRange(0.15f, 0.35f)));
			Piece->SetContaminated(i >= CleanCount);
		}
	}

	// 경로 개방 (조각이 벽 자리를 대체)
	Destroy();
}

FMD_ScrapeResult AMD_CuttableWall::ApplyScrapeStrokeFromHits(
	const FHitResult& StartHit,
	const FHitResult& EndHit)
{
	return ApplyScrapeStrokeFromHitsWithToolSpec(
		StartHit,
		EndHit,
		ScrapeSettings.GetToolSpec());
}

FMD_ScrapeResult AMD_CuttableWall::ApplyScrapeStrokeFromHitsWithToolSpec(
	const FHitResult& StartHit,
	const FHitResult& EndHit,
	const FMD_ScrapeToolSpec& ToolSpec)
{
	FMD_ScrapeStroke LocalStroke;
	EMD_ScrapeSurfaceFace StartSurfaceFace =
		EMD_ScrapeSurfaceFace::Front;
	EMD_ScrapeSurfaceFace EndSurfaceFace =
		EMD_ScrapeSurfaceFace::Front;
	if (!TryGetScrapeSurfacePositionFromHit(
			StartHit,
			LocalStroke.StartPositionCm,
			StartSurfaceFace)
		|| !TryGetScrapeSurfacePositionFromHit(
			EndHit,
			LocalStroke.EndPositionCm,
			EndSurfaceFace)
		|| StartSurfaceFace != EndSurfaceFace)
	{
		return FMD_ScrapeResult();
	}

	LocalStroke.SurfaceFace = StartSurfaceFace;
	return ApplyLocalScrapeStrokeWithToolSpec(LocalStroke, ToolSpec);
}

bool AMD_CuttableWall::TryGetScrapeSurfacePositionFromHit(
	const FHitResult& Hit,
	FVector2D& OutSurfacePositionCm,
	EMD_ScrapeSurfaceFace& OutSurfaceFace)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	return TryGetContaminationFaceFromHit(Hit, OutSurfaceFace)
		&& TryProjectWorldPointToContaminationSurface(
			Hit.ImpactPoint,
			OutSurfacePositionCm);
}

bool AMD_CuttableWall::TryGetScrapeSurfaceWorldPosition(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
	if (!bContaminationSurfaceInitialized
		|| !Mesh
		|| !FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			Mesh->GetComponentTransform(),
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceCenterLocal,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			OutWorldPosition))
	{
		return false;
	}

	return true;
}

bool AMD_CuttableWall::TryGetScrapeSurfaceWorldPositionAtDepth(
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector& ReferenceWorldPosition,
	FVector& OutWorldPosition) const
{
	if (!TryGetScrapeSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			OutWorldPosition))
	{
		return false;
	}

	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (ReferenceWorldPosition.ContainsNaN()
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		// 깊이 보정만 실패한 경우이므로 Bounds 평면 위치를 그대로 사용한다.
		return true;
	}

	const FVector WorldFrontNormal = Mesh->GetComponentTransform()
		.TransformVector(FrontNormalLocal)
		.GetSafeNormal();
	if (WorldFrontNormal.IsNearlyZero())
	{
		return true;
	}

	// 앞/뒤 축 성분만 참조 위치의 깊이로 바꾼다. 가로/세로 위치는 유지한다.
	const double DepthDifference = FVector::DotProduct(
		ReferenceWorldPosition - OutWorldPosition,
		WorldFrontNormal);
	if (!FMath::IsFinite(DepthDifference))
	{
		return true;
	}

	OutWorldPosition += WorldFrontNormal * DepthDifference;
	return !OutWorldPosition.ContainsNaN();
}

FMD_ScrapeResult AMD_CuttableWall::ApplyLocalScrapeStroke(
	const FMD_ScrapeStroke& Stroke)
{
	return ApplyLocalScrapeStrokeWithToolSpec(
		Stroke,
		ScrapeSettings.GetToolSpec());
}

FMD_ScrapeResult AMD_CuttableWall::ApplyLocalScrapeStrokeWithToolSpec(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeStrokeTotal);

	if (!HasAuthority())
	{
		return FMD_ScrapeResult();
	}

	// Spawn 직후 호출도 지원
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	if (!bContaminationSurfaceInitialized)
	{
		return FMD_ScrapeResult();
	}

	bool bWasFullyRemoved = false;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
		bWasFullyRemoved = IsContaminationFullyRemoved();
	}

	TArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Stroke.SurfaceFace);
	if (FaceCells.IsEmpty())
	{
		FMD_ScrapeResult InvalidResult;
		InvalidResult.SurfaceFace = Stroke.SurfaceFace;
		return InvalidResult;
	}

	FMD_ScrapeResult Result;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeEvaluate);
		Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			Stroke,
			ToolSpec,
			ScrapeSettings,
			ContaminationGridDimensions,
			ResolvedContaminationSurfaceSizeCm,
			FaceCells);
	}

	// 잘못된 입력은 상태를 변경하지 않음
	if (!Result.bIsInputValid)
	{
		return Result;
	}

	const int32 CellsPerFace = FaceCells.Num();
	for (FMD_ScrapeCellChange& CellChange : Result.CellChanges)
	{
		CellChange.CellIndex = FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			Stroke.SurfaceFace,
			CellChange.CellIndex,
			CellsPerFace);
	}

	// 스트로크는 한 면만 건드리므로 그 면만 다시 계산한다.
	RefreshContaminationCleanRatioCache(Stroke.SurfaceFace);
	UpdateContaminationMask(Result.CellChanges);
	UpdateContaminationVisualization(Result.CellChanges);
	PublishContaminationChanges(Result.CellChanges);

	// 어느 한 면이 처음 완료되는 전환에서 한 번만 방송
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (!bWasFullyRemoved
		&& bIsFullyRemoved
		&& !bContaminationCompletionBroadcast)
	{
		bContaminationCompletionBroadcast = true;
		OnContaminationFullyRemoved.Broadcast(this);
	}

	return Result;
}

float AMD_CuttableWall::GetContaminationCleanRatio() const
{
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	return FMD_ScrapeFaceProgress::CalculateOverallCleanRatio(
		CachedFrontContaminationCleanRatio,
		CachedBackContaminationCleanRatio);
}

float AMD_CuttableWall::GetContaminationCleanRatio(
	const EMD_ScrapeSurfaceFace Face) const
{
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	switch (Face)
	{
	case EMD_ScrapeSurfaceFace::Front:
		return FMath::Clamp(
			CachedFrontContaminationCleanRatio,
			0.0f,
			1.0f);

	case EMD_ScrapeSurfaceFace::Back:
		return FMath::Clamp(
			CachedBackContaminationCleanRatio,
			0.0f,
			1.0f);

	default:
		return 0.0f;
	}
}

float AMD_CuttableWall::GetContaminationCleanRatioForFace(
	const EMD_ScrapeSurfaceFace Face) const
{
	return GetContaminationCleanRatio(Face);
}

float AMD_CuttableWall::GetContaminationRemovedPercent() const
{
	return GetContaminationCleanRatio() * 100.0f;
}

float AMD_CuttableWall::GetContaminationRemovedPercent(
	const EMD_ScrapeSurfaceFace Face) const
{
	return GetContaminationCleanRatio(Face) * 100.0f;
}

float AMD_CuttableWall::GetContaminationRemovedPercentForFace(
	const EMD_ScrapeSurfaceFace Face) const
{
	return GetContaminationRemovedPercent(Face);
}

float AMD_CuttableWall::GetResidualContaminationRatio() const
{
	// 초기화 실패는 오염 100%로 처리
	return 1.0f - GetContaminationCleanRatio();
}

bool AMD_CuttableWall::IsContaminationFullyRemoved() const
{
	return IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Front)
		|| IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Back);
}

bool AMD_CuttableWall::IsContaminationFullyRemoved(
	const EMD_ScrapeSurfaceFace Face) const
{
	const TConstArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Face);
	return bContaminationSurfaceInitialized
		&& !FaceCells.IsEmpty()
		&& FMD_ScrapeEvaluator::IsSurfaceFullyClean(
			FaceCells,
			ScrapeSettings.CleanTolerance);
}

bool AMD_CuttableWall::IsContaminationFullyRemovedForFace(
	const EMD_ScrapeSurfaceFace Face) const
{
	return IsContaminationFullyRemoved(Face);
}

bool AMD_CuttableWall::CanCut() const
{
	// 초기화 실패 시 절단 금지
	if (!bContaminationSurfaceInitialized)
	{
		return false;
	}

	const float Gate = (WallType == EMD_WallType::Strut) ? StrutCutGate : PartitionCutGate;
	const float SafeGate = FMath::IsFinite(Gate)
		? FMath::Clamp(Gate, 0.0f, 100.0f)
		: 100.0f;

	return FMD_ScrapeFaceProgress::IsEitherFaceReadyForCut(
		GetContaminationCleanRatio(EMD_ScrapeSurfaceFace::Front),
		GetContaminationCleanRatio(EMD_ScrapeSurfaceFace::Back),
		SafeGate / 100.0f);
}

bool AMD_CuttableWall::ResolveContaminationSurfaceGeometry()
{
	if (!Mesh)
	{
		ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
		ResolvedContaminationSurfaceNormalAxis =
			EMD_ScrapeSurfaceNormalAxis::Y;
		ContaminationSurfaceCenterLocal = FVector::ZeroVector;
		return false;
	}

	Mesh->GetLocalBounds(
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum);

	// 피벗과 무관하게 Bounds 중심 사용
	ContaminationSurfaceCenterLocal =
		(ContaminationMeshLocalBoundsMinimum
			+ ContaminationMeshLocalBoundsMaximum)
		* 0.5f;
	const bool bHasValidOverride =
		FMath::IsFinite(ContaminationSurfaceSizeOverrideCm.X)
		&& FMath::IsFinite(ContaminationSurfaceSizeOverrideCm.Y)
		&& ContaminationSurfaceSizeOverrideCm.X > UE_SMALL_NUMBER
		&& ContaminationSurfaceSizeOverrideCm.Y > UE_SMALL_NUMBER;
	const FVector AbsoluteComponentScale =
		Mesh->GetComponentScale().GetAbs();
	if (!FMD_ScrapeSurfaceGeometry::TryResolveSurfaceNormalAxis(
			ContaminationSurfaceNormalAxis,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			AbsoluteComponentScale,
			ResolvedContaminationSurfaceNormalAxis))
	{
		// 메시 없는 계산 테스트처럼 크기 Override만 제공한 기존 경로는 Y축으로 유지한다.
		if (!bHasValidOverride
			|| ContaminationSurfaceNormalAxis
				!= EMD_ScrapeSurfaceNormalAxis::Auto)
		{
			ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
			return false;
		}
		ResolvedContaminationSurfaceNormalAxis =
			EMD_ScrapeSurfaceNormalAxis::Y;
	}

	if (bHasValidOverride)
	{
		ResolvedContaminationSurfaceSizeCm =
			ContaminationSurfaceSizeOverrideCm;
		return true;
	}

	return FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceSize(
		ResolvedContaminationSurfaceNormalAxis,
		ContaminationMeshLocalBoundsMinimum,
		ContaminationMeshLocalBoundsMaximum,
		AbsoluteComponentScale,
		ResolvedContaminationSurfaceSizeCm);
}

TArrayView<FMD_ScrapeSurfaceCell>
AMD_CuttableWall::GetContaminationCellsForFace(
	const EMD_ScrapeSurfaceFace Face)
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return TArrayView<FMD_ScrapeSurfaceCell>();
	}

	const int32 FaceOffset = FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
		Face,
		CellsPerFace);
	if (FaceOffset == INDEX_NONE)
	{
		return TArrayView<FMD_ScrapeSurfaceCell>();
	}

	return MakeArrayView(ContaminationCells).Slice(
		FaceOffset,
		CellsPerFace);
}

TConstArrayView<FMD_ScrapeSurfaceCell>
AMD_CuttableWall::GetContaminationCellsForFace(
	const EMD_ScrapeSurfaceFace Face) const
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return TConstArrayView<FMD_ScrapeSurfaceCell>();
	}

	const int32 FaceOffset = FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
		Face,
		CellsPerFace);
	if (FaceOffset == INDEX_NONE)
	{
		return TConstArrayView<FMD_ScrapeSurfaceCell>();
	}

	return MakeArrayView(ContaminationCells).Slice(
		FaceOffset,
		CellsPerFace);
}

void AMD_CuttableWall::RefreshContaminationCleanRatioCache()
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeCleanRatio);
	RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Front);
	RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Back);
}

void AMD_CuttableWall::RefreshContaminationCleanRatioCache(
	const EMD_ScrapeSurfaceFace Face)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeCleanRatio);

	// 정의되지 않은 면을 그대로 넘기면 빈 슬라이스가 "오염 없음"으로 계산돼
	// 제거율이 1.0이 되고 철거 게이트가 열린다. 반드시 먼저 막는다.
	if (Face != EMD_ScrapeSurfaceFace::Front
		&& Face != EMD_ScrapeSurfaceFace::Back)
	{
		return;
	}

	// 제거 비율 계산은 면 전체를 훑으므로, 바뀌지 않은 면까지 다시 계산하지 않는다.
	float& CachedCleanRatio = Face == EMD_ScrapeSurfaceFace::Back
		? CachedBackContaminationCleanRatio
		: CachedFrontContaminationCleanRatio;
	CachedCleanRatio = FMD_ScrapeEvaluator::CalculateCleanRatio(
		GetContaminationCellsForFace(Face));
}

float AMD_CuttableWall::GetContaminationDemolitionReadyRatio() const
{
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	return FMD_ScrapeFaceProgress::CalculateDemolitionReadyCleanRatio(
		CachedFrontContaminationCleanRatio,
		CachedBackContaminationCleanRatio);
}

void AMD_CuttableWall::InitializeContaminationSurface()
{
	bContaminationSurfaceInitialized = false;
	bContaminationDecalsConfigured = false;
	SetContaminationDecalsVisible(false);
	bContaminationCompletionBroadcast = false;
	CachedFrontContaminationCleanRatio = 0.0f;
	CachedBackContaminationCleanRatio = 0.0f;
	ContaminationCells.Reset();
	ReleaseContaminationMask();
	if (HasAuthority())
	{
		ReplicatedContaminationState.ResetState();
	}

	if (!ResolveContaminationSurfaceGeometry())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 오염 작업면 크기를 계산하지 못했습니다."),
			*GetName());
		return;
	}

	// 런타임에서도 격자 범위 보정
	ContaminationGridDimensions.X = FMath::Clamp(
		ContaminationGridDimensions.X,
		1,
		MaximumContaminationGridAxis);
	ContaminationGridDimensions.Y = FMath::Clamp(
		ContaminationGridDimensions.Y,
		1,
		MaximumContaminationGridAxis);

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 앞뒤 총 오염 셀 수가 uint16 범위를 벗어났습니다. Grid=(%d, %d)"),
			*GetName(),
			ContaminationGridDimensions.X,
			ContaminationGridDimensions.Y);
		return;
	}

	ContaminationCells.SetNum(TotalCellCount);

	// 잘못된 초기값은 완전 오염으로 처리
	const float SafeInitialContamination =
		FMath::IsFinite(InitialContaminationPerCell)
		? FMath::Clamp(InitialContaminationPerCell, 0.0f, 1.0f)
		: 1.0f;

	for (FMD_ScrapeSurfaceCell& Cell : ContaminationCells)
	{
		Cell.InitialContamination = SafeInitialContamination;
		Cell.RemainingContamination = SafeInitialContamination;
	}
	if (bUseProceduralContamination
		&& !TryApplyProceduralInitialContamination(
			CellsPerFace,
			SafeInitialContamination))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 절차형 초기 오염 생성에 실패해 균일한 초기값을 유지합니다."),
			*GetName());
	}

	if (!InitializeContaminationMask())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationSurface - %s의 런타임 G8 오염 마스크를 만들지 못했습니다."),
			*GetName());
	}

	RefreshContaminationCleanRatioCache();
	bContaminationSurfaceInitialized = true;

	// 초기 완료 상태의 중복 방송 방지
	bContaminationCompletionBroadcast =
		IsContaminationFullyRemoved();
	ConfigureContaminationDecals();
}

bool AMD_CuttableWall::TryApplyProceduralInitialContamination(
	const int32 CellsPerFace,
	const float BaseInitialContamination)
{
	if (!Mesh
		|| CellsPerFace <= 0
		|| ContaminationCells.Num()
			!= CellsPerFace * FMD_ScrapeSurfaceLayout::FaceCount
		|| ContaminationGridDimensions.X <= 0
		|| ContaminationGridDimensions.Y <= 0
		|| ResolvedContaminationSurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| ResolvedContaminationSurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D CellSizeCm(
		ResolvedContaminationSurfaceSizeCm.X
			/ static_cast<double>(ContaminationGridDimensions.X),
		ResolvedContaminationSurfaceSizeCm.Y
			/ static_cast<double>(ContaminationGridDimensions.Y));
	const FVector2D SurfaceMinimumCm =
		ResolvedContaminationSurfaceSizeCm * -0.5;
	TArray<float> InitialAmounts;
	InitialAmounts.SetNumUninitialized(ContaminationCells.Num());

	for (int32 FaceIndex = 0;
		FaceIndex < FMD_ScrapeSurfaceLayout::FaceCount;
		++FaceIndex)
	{
		const EMD_ScrapeSurfaceFace Face = FaceIndex == 0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		const int32 FaceSeed = ContaminationNoiseSeed
			+ FaceIndex * 1013;
		for (int32 LocalCellIndex = 0;
			LocalCellIndex < CellsPerFace;
			++LocalCellIndex)
		{
			const int32 CellX =
				LocalCellIndex % ContaminationGridDimensions.X;
			const int32 CellY =
				LocalCellIndex / ContaminationGridDimensions.X;
			const FVector2D SurfacePositionCm = SurfaceMinimumCm
				+ FVector2D(
					(static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
					(static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);
			FVector WorldPosition = FVector::ZeroVector;
			float InitialAmount = 0.0f;
			if (!FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
					SurfacePositionCm,
					Face,
					Mesh->GetComponentTransform(),
					ResolvedContaminationSurfaceNormalAxis,
					ContaminationSurfaceCenterLocal,
					ContaminationMeshLocalBoundsMinimum,
					ContaminationMeshLocalBoundsMaximum,
					WorldPosition)
				|| !FMD_ContaminationNoise::TryCalculateInitialContamination(
					WorldPosition,
					BaseInitialContamination,
					ContaminationNoiseScaleCm,
					FaceSeed,
					ContaminationCoverage,
					ContaminationMinimumAmount,
					InitialAmount))
			{
				return false;
			}

			const int32 CombinedCellIndex =
				FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
					Face,
					LocalCellIndex,
					CellsPerFace);
			if (!InitialAmounts.IsValidIndex(CombinedCellIndex))
			{
				return false;
			}
			InitialAmounts[CombinedCellIndex] = InitialAmount;
		}
	}

	for (int32 CellIndex = 0;
		CellIndex < ContaminationCells.Num();
		++CellIndex)
	{
		ContaminationCells[CellIndex].InitialContamination =
			InitialAmounts[CellIndex];
		ContaminationCells[CellIndex].RemainingContamination =
			InitialAmounts[CellIndex];
	}
	return true;
}

bool AMD_CuttableWall::InitializeContaminationMask()
{
	ReleaseContaminationMask();

	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	TArray<uint8> AtlasPixels;
	if (!FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions,
			AtlasDimensions)
		|| !FMD_ScrapeMaskAtlas::TryBuildAtlasBuffer(
			ContaminationGridDimensions,
			MakeArrayView(ContaminationCells),
			AtlasPixels))
	{
		return false;
	}

	UTexture2D* NewMaskTexture = UTexture2D::CreateTransient(
		AtlasDimensions.X,
		AtlasDimensions.Y,
		PF_G8);
	if (!NewMaskTexture)
	{
		return false;
	}

	NewMaskTexture->SRGB = false;
	NewMaskTexture->NeverStream = true;
	NewMaskTexture->Filter = TF_Bilinear;
	NewMaskTexture->AddressX = TA_Clamp;
	NewMaskTexture->AddressY = TA_Clamp;

	FTexturePlatformData* PlatformData = NewMaskTexture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.IsEmpty())
	{
		NewMaskTexture->ReleaseResource();
		return false;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	Mip.BulkData.Lock(LOCK_READ_WRITE);
	void* DestinationData = Mip.BulkData.Realloc(AtlasPixels.Num());
	if (DestinationData)
	{
		FMemory::Memcpy(
			DestinationData,
			AtlasPixels.GetData(),
			AtlasPixels.Num());
	}
	Mip.BulkData.Unlock();

	if (!DestinationData)
	{
		NewMaskTexture->ReleaseResource();
		return false;
	}

	ContaminationMaskAtlasDimensions = AtlasDimensions;
	ContaminationMaskAtlasPixels = MoveTemp(AtlasPixels);
	ContaminationMaskTexture = NewMaskTexture;
	ContaminationMaskTexture->UpdateResource();
	RefreshContaminationDecalMaterialParameters();
	return true;
}

void AMD_CuttableWall::ReleaseContaminationMask()
{
	if (ContaminationMaskTexture)
	{
		ContaminationMaskTexture->ReleaseResource();
		ContaminationMaskTexture = nullptr;
	}
	RefreshContaminationDecalMaterialParameters();

	ContaminationMaskAtlasPixels.Reset();
	ContaminationMaskAtlasDimensions = FIntPoint::ZeroValue;
}

void AMD_CuttableWall::UpdateContaminationMask(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeMaskUpdate);

	if (CellChanges.IsEmpty())
	{
		return;
	}

	FIntPoint ExpectedDimensions = FIntPoint::ZeroValue;
	if (!FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
			ContaminationGridDimensions,
			ExpectedDimensions))
	{
		return;
	}

	const int32 ExpectedPixelCount =
		ExpectedDimensions.X * ExpectedDimensions.Y;
	if (!ContaminationMaskTexture
		|| ContaminationMaskAtlasDimensions != ExpectedDimensions
		|| ContaminationMaskAtlasPixels.Num() != ExpectedPixelCount
		|| !ContaminationMaskTexture->GetResource())
	{
		InitializeContaminationMask();
		return;
	}

	// 값이 실제로 달라진 픽셀만 감싸는 최소 사각형을 모은다.
	FIntRect ChangedRegion = FMD_ScrapeMaskAtlas::MakeEmptyAtlasRegion();
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (!ContaminationCells.IsValidIndex(CellChange.CellIndex))
		{
			continue;
		}

		int32 PixelIndex = INDEX_NONE;
		if (!FMD_ScrapeMaskAtlas::TryGetAtlasPixelIndexFromCombinedCellIndex(
				ContaminationGridDimensions,
				CellChange.CellIndex,
				PixelIndex)
			|| !ContaminationMaskAtlasPixels.IsValidIndex(PixelIndex))
		{
			continue;
		}

		const uint8 PreviousMaskValue =
			ContaminationMaskAtlasPixels[PixelIndex];
		FIntPoint PixelCoordinate = FIntPoint::NoneValue;
		if (!FMD_ScrapeMaskAtlas::TryUpdateAtlasPixel(
				ContaminationGridDimensions,
				CellChange.CellIndex,
				ContaminationCells[CellChange.CellIndex],
				ContaminationMaskAtlasPixels,
				PixelCoordinate)
			|| PreviousMaskValue
				== ContaminationMaskAtlasPixels[PixelIndex])
		{
			continue;
		}

		FMD_ScrapeMaskAtlas::ExpandAtlasRegion(
			ChangedRegion,
			PixelCoordinate);
	}

	if (FMD_ScrapeMaskAtlas::IsAtlasRegionEmpty(ChangedRegion))
	{
		// 스트로크가 이미 깨끗한 곳만 지나면 올릴 픽셀이 없다.
		return;
	}

	if (!UploadContaminationMaskRegion(ChangedRegion))
	{
		// 렌더 리소스가 잠시 유효하지 않다면 현재 CPU 전체 상태로 다시 만든다.
		InitializeContaminationMask();
	}
}

bool AMD_CuttableWall::UploadContaminationMaskRegion(
	const FIntRect& AtlasRegion)
{
	const int32 RegionWidth = AtlasRegion.Width();
	const int32 RegionHeight = AtlasRegion.Height();
	if (!ContaminationMaskTexture
		|| !ContaminationMaskTexture->GetResource()
		|| RegionWidth <= 0
		|| RegionHeight <= 0)
	{
		return false;
	}

	// 사각형에 걸친 행만 빈틈없이 모아 RHI 업로드 한 번으로 끝낸다.
	uint8* UploadValues = new uint8[RegionWidth * RegionHeight];
	if (!FMD_ScrapeMaskAtlas::TryPackAtlasRegion(
			ContaminationMaskAtlasDimensions,
			MakeArrayView(ContaminationMaskAtlasPixels),
			AtlasRegion,
			MakeArrayView(UploadValues, RegionWidth * RegionHeight)))
	{
		delete[] UploadValues;
		return false;
	}

	FUpdateTextureRegion2D* UpdateRegions = new FUpdateTextureRegion2D[1];
	UpdateRegions[0] = FUpdateTextureRegion2D(
		static_cast<uint32>(AtlasRegion.Min.X),
		static_cast<uint32>(AtlasRegion.Min.Y),
		0,
		0,
		static_cast<uint32>(RegionWidth),
		static_cast<uint32>(RegionHeight));

	ContaminationMaskTexture->UpdateTextureRegions(
		0,
		1,
		UpdateRegions,
		static_cast<uint32>(RegionWidth),
		1,
		UploadValues,
		[](uint8* SourceData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] SourceData;
			delete[] Regions;
		});
	return true;
}

bool AMD_CuttableWall::ConfigureContaminationDecals()
{
	bContaminationDecalsConfigured = false;
	SetContaminationDecalsVisible(false);

	if (!bShowContaminationDecals
		|| !bContaminationSurfaceInitialized
		|| !ContaminationMaskTexture
		|| !FrontContaminationDecal
		|| !BackContaminationDecal
		|| !ContaminationDecalMaterial)
	{
		return false;
	}

	const UMaterial* BaseMaterial = ContaminationDecalMaterial->GetMaterial();
	if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_DeferredDecal)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationDecals - %s의 ContaminationDecalMaterial은 Deferred Decal Domain이어야 합니다."),
			*GetName());
		return false;
	}

	// 두 면의 투영 깊이는 서로 영향을 주므로 배치 전에 함께 계산한다.
	ResolveContaminationDecalDepths();

	const bool bConfiguredFront = ConfigureContaminationDecalForFace(
		FrontContaminationDecal,
		EMD_ScrapeSurfaceFace::Front,
		FrontContaminationDecalMID);
	const bool bConfiguredBack = ConfigureContaminationDecalForFace(
		BackContaminationDecal,
		EMD_ScrapeSurfaceFace::Back,
		BackContaminationDecalMID);
	if (!bConfiguredFront || !bConfiguredBack)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::ConfigureContaminationDecals - %s의 앞/뒤 Decal 배치에 필요한 표면 크기 또는 벽 두께가 올바르지 않습니다."),
			*GetName());
		return false;
	}

	bContaminationDecalsConfigured = true;
	SetContaminationDecalsVisible(true);
	if (ContaminationOverlay)
	{
		ContaminationOverlay->SetVisibility(false);
	}
	return true;
}

void AMD_CuttableWall::ResolveContaminationDecalDepths()
{
	const double RequestedDepthCm = FMath::IsFinite(
		ContaminationDecalProjectionDepthCm)
		? FMath::Max(
			static_cast<double>(ContaminationDecalProjectionDepthCm),
			0.0)
		: 0.0;
	const double FitMarginCm = FMath::IsFinite(
		ContaminationDecalDepthFitMarginCm)
		? FMath::Max(
			static_cast<double>(ContaminationDecalDepthFitMarginCm),
			0.0)
		: 0.0;

	double FrontDepthCm = RequestedDepthCm;
	double BackDepthCm = RequestedDepthCm;

	// 앞/뒤 축 방향의 실제 벽 두께. 축을 못 구하면 0으로 두고 두께 배분을 건너뛴다.
	double WallThicknessCm = 0.0;
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (Mesh
		&& FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		const FVector LocalBoundsSize = ContaminationMeshLocalBoundsMaximum
			- ContaminationMeshLocalBoundsMinimum;
		WallThicknessCm =
			FMath::Abs(FVector::DotProduct(LocalBoundsSize, FrontNormalLocal))
			* FMath::Abs(FVector::DotProduct(
				Mesh->GetComponentScale().GetAbs(),
				FrontNormalLocal));
		if (!FMath::IsFinite(WallThicknessCm))
		{
			WallThicknessCm = 0.0;
		}
	}

	// 메시 형상에서 각 면이 실제로 필요로 하는 깊이를 구한다.
	if (bAutoFitContaminationDecalDepth && Mesh)
	{
		const UStaticMesh* MeshAsset = Mesh->GetStaticMesh();
		const FVector AbsoluteScale = Mesh->GetComponentScale().GetAbs();
		auto FitFaceDepth = [&](const EMD_ScrapeSurfaceFace Face)
		{
			FMD_ScrapeFaceSurfaceProfile Profile;
			if (FMD_ScrapeMeshSurfaceProfile::TryMeasureFaceProfile(
					MeshAsset,
					Face,
					ResolvedContaminationSurfaceNormalAxis,
					AbsoluteScale,
					ContaminationGridDimensions,
					ResolvedContaminationSurfaceSizeCm,
					MinimumContaminationFaceNormalDot,
					Profile))
			{
				return FMD_ScrapeMeshSurfaceProfile::
					CalculateFittedProjectionDepthCm(
						Profile,
						RequestedDepthCm,
						FitMarginCm);
			}

			// CPU 정점 데이터를 읽을 수 없는 메시는 형상을 알 수 없다.
			// 이때는 벽 두께의 절반을 덮어 어떤 형상이든 자기 쪽 절반을 놓치지 않게 한다.
			return WallThicknessCm > 0.0
				? FMath::Max(RequestedDepthCm, WallThicknessCm * 0.5)
				: RequestedDepthCm;
		};

		FrontDepthCm = FitFaceDepth(EMD_ScrapeSurfaceFace::Front);
		BackDepthCm = FitFaceDepth(EMD_ScrapeSurfaceFace::Back);
	}

	// 앞/뒤 상자가 겹쳐 같은 표면을 두 번 칠하지 않도록 벽 두께를 나눈다.
	FMD_ScrapeMeshSurfaceProfile::BalanceFaceDepths(
		WallThicknessCm,
		FrontDepthCm,
		BackDepthCm);

	ResolvedFrontDecalDepthCm = static_cast<float>(FrontDepthCm);
	ResolvedBackDecalDepthCm = static_cast<float>(BackDepthCm);
}

bool AMD_CuttableWall::ConfigureContaminationDecalForFace(
	UDecalComponent* DecalComponent,
	const EMD_ScrapeSurfaceFace Face,
	TObjectPtr<UMaterialInstanceDynamic>& InOutMID)
{
	if (!DecalComponent || !Mesh || !ContaminationDecalMaterial)
	{
		return false;
	}

	float& ResolvedDepthCm = Face == EMD_ScrapeSurfaceFace::Front
		? ResolvedFrontDecalDepthCm
		: ResolvedBackDecalDepthCm;
	FMD_ScrapeDecalFaceLayout Layout;
	if (!FMD_ScrapeDecalLayout::TryCalculateFaceLayout(
			Face,
			ResolvedContaminationSurfaceNormalAxis,
			ResolvedContaminationSurfaceSizeCm,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			Mesh->GetComponentScale().GetAbs(),
			ResolvedDepthCm,
			ContaminationDecalSurfaceOffsetCm,
			ContaminationDecalEdgeInsetCm,
			Layout))
	{
		return false;
	}

	// 두께·표면 크기 제한까지 적용된 최종값을 에디터 Details에서 확인할 수 있게 남긴다.
	ResolvedDepthCm = static_cast<float>(Layout.AppliedInwardDepthCm);
	ResolvedContaminationDecalEdgeInsetCm =
		static_cast<float>(Layout.AppliedEdgeInsetCm);

	DecalComponent->SetAbsolute(false, false, true);
	DecalComponent->SetRelativeScale3D(FVector::OneVector);
	DecalComponent->SetRelativeLocationAndRotation(
		Layout.RelativeLocation,
		Layout.RelativeRotation);
	DecalComponent->DecalSize = Layout.DecalSize;
	DecalComponent->SetFadeScreenSize(0.0f);
	DecalComponent->SetSortOrder(ContaminationDecalSortOrder);

	if (!InOutMID || InOutMID->Parent != ContaminationDecalMaterial)
	{
		DecalComponent->SetDecalMaterial(ContaminationDecalMaterial);
		InOutMID = DecalComponent->CreateDynamicMaterialInstance();
	}
	if (!InOutMID)
	{
		return false;
	}

	FVector2D MaskHalfTexelUV = FVector2D::ZeroVector;
	if (!FMD_ScrapeDecalLayout::TryCalculateMaskHalfTexelUV(
			ContaminationMaskAtlasDimensions,
			ResolvedContaminationSurfaceSizeCm,
			ContaminationDecalEdgeInsetCm,
			MaskHalfTexelUV))
	{
		return false;
	}

	DecalComponent->SetDecalMaterial(InOutMID);
	InOutMID->SetTextureParameterValue(
		ContaminationMaskTextureParameterName,
		ContaminationMaskTexture);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskVScaleParameterName,
		Layout.MaskVScale);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskVOffsetParameterName,
		Layout.MaskVOffset);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskUFlipParameterName,
		Layout.MaskUFlip);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskHalfTexelUParameterName,
		MaskHalfTexelUV.X);
	InOutMID->SetScalarParameterValue(
		ContaminationMaskHalfTexelVParameterName,
		MaskHalfTexelUV.Y);
	DecalComponent->MarkRenderStateDirty();
	return true;
}

void AMD_CuttableWall::SetContaminationDecalsVisible(
	const bool bVisible)
{
	if (FrontContaminationDecal)
	{
		FrontContaminationDecal->SetVisibility(bVisible);
		FrontContaminationDecal->SetHiddenInGame(!bVisible);
	}
	if (BackContaminationDecal)
	{
		BackContaminationDecal->SetVisibility(bVisible);
		BackContaminationDecal->SetHiddenInGame(!bVisible);
	}
}

void AMD_CuttableWall::RefreshContaminationDecalMaterialParameters()
{
	// 배치 때와 같은 inset 보정을 써야 마스크를 다시 만들 때 무늬가 밀리지 않는다.
	FVector2D MaskHalfTexelUV = FVector2D::ZeroVector;
	if (!FMD_ScrapeDecalLayout::TryCalculateMaskHalfTexelUV(
			ContaminationMaskAtlasDimensions,
			ResolvedContaminationSurfaceSizeCm,
			ContaminationDecalEdgeInsetCm,
			MaskHalfTexelUV))
	{
		return;
	}

	auto RefreshMID = [this, MaskHalfTexelUV](
		UMaterialInstanceDynamic* DecalMID,
		const float VOffset,
		const float UFlip)
	{
		if (!DecalMID)
		{
			return;
		}

		DecalMID->SetTextureParameterValue(
			ContaminationMaskTextureParameterName,
			ContaminationMaskTexture);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskVScaleParameterName,
			0.5f);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskVOffsetParameterName,
			VOffset);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskUFlipParameterName,
			UFlip);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskHalfTexelUParameterName,
			MaskHalfTexelUV.X);
		DecalMID->SetScalarParameterValue(
			ContaminationMaskHalfTexelVParameterName,
			MaskHalfTexelUV.Y);
	};

	RefreshMID(FrontContaminationDecalMID, 0.0f, 1.0f);
	RefreshMID(BackContaminationDecalMID, 0.5f, 0.0f);
}

void AMD_CuttableWall::InitializeContaminationVisualization()
{
	if (!ContaminationOverlay)
	{
		return;
	}

	ContaminationOverlay->ClearInstances();
	ContaminationCellBaseTransforms.Reset();
	ContaminationOverlay->SetVisibility(false);

	if (!bShowContaminationOverlay
		|| bContaminationDecalsConfigured
		|| !bContaminationSurfaceInitialized
		|| ContaminationCells.IsEmpty())
	{
		return;
	}

	UStaticMesh* OverlayMesh = ContaminationOverlay->GetStaticMesh();
	if (!OverlayMesh)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationVisualization - %s의 ContaminationOverlay 컴포넌트에 Static Mesh가 지정되지 않았습니다."),
			*GetName());
		return;
	}

	// Mesh와 Material은 Blueprint에서 지정
	FVector HorizontalAxisLocal;
	FVector FrontNormalLocal;
	FVector VerticalAxisLocal;
	if (!FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxisLocal,
			FrontNormalLocal,
			VerticalAxisLocal))
	{
		return;
	}

	const FVector AbsoluteScale = Mesh->GetComponentScale().GetAbs();
	const double SafeHorizontalScale = FMath::Max(
		FMath::Abs(FVector::DotProduct(
			AbsoluteScale,
			HorizontalAxisLocal)),
		static_cast<double>(UE_SMALL_NUMBER));
	const double SafeNormalScale = FMath::Max(
		FMath::Abs(FVector::DotProduct(
			AbsoluteScale,
			FrontNormalLocal)),
		static_cast<double>(UE_SMALL_NUMBER));
	const double SafeVerticalScale = FMath::Max(
		FMath::Abs(FVector::DotProduct(
			AbsoluteScale,
			VerticalAxisLocal)),
		static_cast<double>(UE_SMALL_NUMBER));

	// 월드 크기를 Overlay 로컬 단위로 변환
	const double SurfaceWidthLocal =
		static_cast<double>(ResolvedContaminationSurfaceSizeCm.X)
		/ SafeHorizontalScale;
	const double SurfaceHeightLocal =
		static_cast<double>(ResolvedContaminationSurfaceSizeCm.Y)
		/ SafeVerticalScale;
	const double CellWidthLocal =
		SurfaceWidthLocal
		/ static_cast<double>(ContaminationGridDimensions.X);
	const double CellHeightLocal =
		SurfaceHeightLocal
		/ static_cast<double>(ContaminationGridDimensions.Y);

	const FVector OverlayMeshSize =
		OverlayMesh->GetBounds().BoxExtent * 2.0f;
	if (OverlayMeshSize.X <= UE_SMALL_NUMBER
		|| OverlayMeshSize.Y <= UE_SMALL_NUMBER
		|| OverlayMeshSize.Z <= UE_SMALL_NUMBER)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("MD_CuttableWall::InitializeContaminationVisualization - %s의 오염막 단위 메시 Bounds가 올바르지 않습니다."),
			*GetName());
		return;
	}

	const double ThicknessLocal =
		static_cast<double>(ContaminationOverlayThicknessCm)
		/ SafeNormalScale;
	const double OffsetLocal =
		static_cast<double>(ContaminationOverlayOffsetCm)
		/ SafeNormalScale;
	const double FrontSurfaceCoordinate = FVector::DotProduct(
		ContaminationMeshLocalBoundsMaximum,
		FrontNormalLocal);
	const double CenterNormalCoordinate = FVector::DotProduct(
		ContaminationSurfaceCenterLocal,
		FrontNormalLocal);
	const FQuat CellRotation = FRotationMatrix::MakeFromXY(
		HorizontalAxisLocal,
		FrontNormalLocal).ToQuat();

	const int32 FrontCellCount =
		GetContaminationCellsForFace(EMD_ScrapeSurfaceFace::Front).Num();
	ContaminationCellBaseTransforms.Reserve(FrontCellCount);
	ContaminationOverlay->PreAllocateInstancesMemory(FrontCellCount);

	for (int32 CellY = 0; CellY < ContaminationGridDimensions.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < ContaminationGridDimensions.X; ++CellX)
		{
			const double HorizontalOffset = -SurfaceWidthLocal * 0.5
				+ (static_cast<double>(CellX) + 0.5) * CellWidthLocal;
			const double VerticalOffset = -SurfaceHeightLocal * 0.5
				+ (static_cast<double>(CellY) + 0.5) * CellHeightLocal;
			const double NormalOffset = FrontSurfaceCoordinate
				- CenterNormalCoordinate
				+ OffsetLocal
				+ ThicknessLocal * 0.5;
			const FVector CellLocation = ContaminationSurfaceCenterLocal
				+ HorizontalAxisLocal * HorizontalOffset
				+ FrontNormalLocal * NormalOffset
				+ VerticalAxisLocal * VerticalOffset;
			const FVector FullContaminationScale(
				CellWidthLocal / static_cast<double>(OverlayMeshSize.X),
				ThicknessLocal / static_cast<double>(OverlayMeshSize.Y),
				CellHeightLocal / static_cast<double>(OverlayMeshSize.Z));

			ContaminationCellBaseTransforms.Emplace(
				CellRotation,
				CellLocation,
				FullContaminationScale);

			const int32 CellIndex =
				CellX + (CellY * ContaminationGridDimensions.X);
			ContaminationOverlay->AddInstance(
				MakeContaminationCellVisualTransform(CellIndex));
		}
	}

	ContaminationOverlay->SetVisibility(true);
}

void AMD_CuttableWall::UpdateContaminationVisualization(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeOverlayUpdate);

	// 데칼 구성에 성공하면 ISM 오버레이는 숨겨져 인스턴스도 비어 있다.
	// 매 틱 헛도는 루프를 아예 들어가지 않는다.
	if (!ContaminationOverlay
		|| bContaminationDecalsConfigured
		|| CellChanges.IsEmpty())
	{
		return;
	}

	bool bUpdatedAnyInstance = false;
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		// 기존 ISM 프로토타입은 앞면 N개만 표시한다. 뒷면은 데칼 Task에서 시각화한다.
		if (!ContaminationCellBaseTransforms.IsValidIndex(
				CellChange.CellIndex)
			|| !ContaminationCells.IsValidIndex(CellChange.CellIndex))
		{
			continue;
		}

		bUpdatedAnyInstance |= ContaminationOverlay->UpdateInstanceTransform(
			CellChange.CellIndex,
			MakeContaminationCellVisualTransform(CellChange.CellIndex),
			false,
			false,
			true);
	}

	if (bUpdatedAnyInstance)
	{
		ContaminationOverlay->MarkRenderStateDirty();
	}
}

void AMD_CuttableWall::HandleReplicatedContaminationState(
	const TConstArrayView<int32> StateItemIndices,
	const bool bResetToInitial)
{
	if (HasAuthority() || !bContaminationSurfaceInitialized)
	{
		return;
	}

	const bool bWasFullyRemoved = IsContaminationFullyRemoved();
	const TArray<FMD_ReplicatedScrapeCellState>& StateItems =
		ReplicatedContaminationState.GetItems();
	TArray<FMD_ScrapeCellChange> CellChanges;
	CellChanges.Reserve(StateItemIndices.Num());

	for (const int32 StateItemIndex : StateItemIndices)
	{
		if (!StateItems.IsValidIndex(StateItemIndex))
		{
			continue;
		}

		const FMD_ReplicatedScrapeCellState& StateItem =
			StateItems[StateItemIndex];
		const int32 StateCellIndex = StateItem.GetCellIndex();
		if (!ContaminationCells.IsValidIndex(StateCellIndex))
		{
			continue;
		}

		FMD_ScrapeSurfaceCell& Cell = ContaminationCells[StateCellIndex];
		const float PreviousRemaining = Cell.RemainingContamination;
		const float ReceivedRemaining = StateItem.GetRemainingContamination();
		const float TargetRemaining = bResetToInitial
			? Cell.InitialContamination
			: (FMath::IsFinite(ReceivedRemaining)
				? FMath::Clamp(
					ReceivedRemaining,
					0.0f,
					Cell.InitialContamination)
				: Cell.InitialContamination);

		if (FMath::IsNearlyEqual(
			PreviousRemaining,
			TargetRemaining,
			UE_SMALL_NUMBER))
		{
			continue;
		}

		Cell.RemainingContamination = TargetRemaining;

		EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
		int32 LocalCellIndex = INDEX_NONE;
		const int32 CellsPerFace = ContaminationCells.Num()
			/ FMD_ScrapeSurfaceLayout::FaceCount;
		if (!FMD_ScrapeSurfaceLayout::TryDecodeCombinedCellIndex(
				StateCellIndex,
				CellsPerFace,
				SurfaceFace,
				LocalCellIndex))
		{
			continue;
		}

		FMD_ScrapeCellChange& CellChange = CellChanges.AddDefaulted_GetRef();
		CellChange.CellIndex = StateCellIndex;
		CellChange.GridCoordinate = FIntPoint(
			LocalCellIndex % ContaminationGridDimensions.X,
			LocalCellIndex / ContaminationGridDimensions.X);
		CellChange.RemovedAmount = FMath::Max(
			0.0f,
			PreviousRemaining - TargetRemaining);
		CellChange.RemainingAmountAfter = TargetRemaining;
		CellChange.bBecameClean =
			PreviousRemaining > ScrapeSettings.CleanTolerance
			&& TargetRemaining <= ScrapeSettings.CleanTolerance;
	}

	RefreshContaminationCleanRatioCache();
	UpdateContaminationMask(CellChanges);
	UpdateContaminationVisualization(CellChanges);

	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (!bWasFullyRemoved
		&& bIsFullyRemoved
		&& !bContaminationCompletionBroadcast)
	{
		bContaminationCompletionBroadcast = true;
		OnContaminationFullyRemoved.Broadcast(this);
	}
	else if (!bIsFullyRemoved)
	{
		bContaminationCompletionBroadcast = false;
	}
}

void AMD_CuttableWall::ApplyAllReplicatedContaminationState()
{
	const TArray<FMD_ReplicatedScrapeCellState>& StateItems =
		ReplicatedContaminationState.GetItems();
	TArray<int32> StateItemIndices;
	StateItemIndices.Reserve(StateItems.Num());

	for (int32 StateItemIndex = 0;
		StateItemIndex < StateItems.Num();
		++StateItemIndex)
	{
		StateItemIndices.Add(StateItemIndex);
	}

	HandleReplicatedContaminationState(StateItemIndices, false);
}

void AMD_CuttableWall::PublishContaminationChanges(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapePublish);

	if (!HasAuthority() || CellChanges.IsEmpty())
	{
		return;
	}

	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (ContaminationCells.IsValidIndex(CellChange.CellIndex))
		{
			ReplicatedContaminationState.SetCellRemaining(
				CellChange.CellIndex,
				ContaminationCells[CellChange.CellIndex].RemainingContamination);
		}
	}

	ForceNetUpdate();
}

FTransform AMD_CuttableWall::MakeContaminationCellVisualTransform(
	const int32 CellIndex) const
{
	if (!ContaminationCellBaseTransforms.IsValidIndex(CellIndex)
		|| !ContaminationCells.IsValidIndex(CellIndex))
	{
		return FTransform::Identity;
	}

	FTransform VisualTransform = ContaminationCellBaseTransforms[CellIndex];
	const FMD_ScrapeSurfaceCell& Cell = ContaminationCells[CellIndex];

	const float RemainingRatio =
		Cell.InitialContamination > UE_SMALL_NUMBER
		? FMath::Clamp(
			Cell.RemainingContamination / Cell.InitialContamination,
			0.0f,
			1.0f)
		: 0.0f;

	if (RemainingRatio <= ScrapeSettings.CleanTolerance)
	{
		VisualTransform.SetScale3D(FVector::ZeroVector);
		return VisualTransform;
	}

	// 면적 비율 유지를 위해 X/Z에 제곱근 적용
	const float LinearScale = FMath::Sqrt(RemainingRatio);
	FVector VisualScale = VisualTransform.GetScale3D();
	VisualScale.X *= LinearScale;
	VisualScale.Z *= LinearScale;
	VisualTransform.SetScale3D(VisualScale);

	return VisualTransform;
}

bool AMD_CuttableWall::TryProjectWorldPointToContaminationSurface(
	const FVector& WorldPoint,
	FVector2D& OutSurfacePositionCm) const
{
	OutSurfacePositionCm = FVector2D::ZeroVector;

	if (!bContaminationSurfaceInitialized
		|| !Mesh
		|| WorldPoint.ContainsNaN())
	{
		return false;
	}

	return FMD_ScrapeSurfaceGeometry::TryProjectWorldPositionToSurface(
		WorldPoint,
		Mesh->GetComponentTransform(),
		ResolvedContaminationSurfaceNormalAxis,
		ContaminationSurfaceCenterLocal,
		OutSurfacePositionCm);
}

bool AMD_CuttableWall::TryGetContaminationFaceFromHit(
	const FHitResult& Hit,
	EMD_ScrapeSurfaceFace& OutSurfaceFace) const
{
	OutSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	if (!bContaminationSurfaceInitialized
		|| !Mesh
		|| !Hit.bBlockingHit
		|| Hit.GetActor() != this
		|| Hit.GetComponent() != Mesh.Get()
		|| Hit.ImpactPoint.ContainsNaN()
		|| Hit.ImpactNormal.ContainsNaN())
	{
		return false;
	}

	return FMD_ScrapeSurfaceGeometry::TryResolveSurfaceFaceFromImpactNormal(
		Hit.ImpactNormal,
		Mesh->GetComponentTransform(),
		ResolvedContaminationSurfaceNormalAxis,
		MinimumContaminationFaceNormalDot,
		OutSurfaceFace);
}

void AMD_CuttableWall::ScanHighlight(float Duration)
{
	// 콘솔/외부 호출에서 0이 들어와도 기본 지속시간 보장
	if (Duration <= 0.f)
	{
		Duration = 3.f;
	}

	if (!MID)
	{
		return;
	}
	
	const bool bStrut = (WallType == EMD_WallType::Strut);
	MID->SetVectorParameterValue(TEXT("Color"),
			bStrut ? FLinearColor(0.7f, 0.05f, 0.05f) : FLinearColor(0.05f, 0.6f, 0.1f));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), Duration, FColor::Cyan,
				bStrut ? TEXT("스캔: 지주격벽 (위험)") : TEXT("스캔: 칸막이격벽 (자유 철거)"));
	}
	
	GetWorldTimerManager().SetTimer(ScanTimerHandle, this, &AMD_CuttableWall::RestoreScanColor, Duration, false);
}

void AMD_CuttableWall::RestoreScanColor()
{
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), NeutralWallColor);
	}
}
