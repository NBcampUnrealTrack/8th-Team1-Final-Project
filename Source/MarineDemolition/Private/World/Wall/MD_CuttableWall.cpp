#include "World/Wall/MD_CuttableWall.h"

#include "TimerManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "World/Sector/MD_Sector.h"
#include "World/Wall/MD_WallPiece.h"

namespace
{
	const FLinearColor NeutralWallColor(0.4f, 0.4f, 0.45f);
}

void FMD_ReplicatedScrapeCellStateArray::SetOwner(
	AMD_CuttableWall* InOwner)
{
	Owner = InOwner;
	CellToItemIndex.Reset();

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const int32 CellIndex = Items[ItemIndex].CellIndex;
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
						Items[RemovedItemIndex].CellIndex) = RemovedItemIndex;
				}
				MarkArrayDirty();
				return;
			}

			FMD_ReplicatedScrapeCellState& Item = Items[*ExistingItemIndex];
			if (Item.RemainingContamination != SafeRemaining)
			{
				Item.RemainingContamination = SafeRemaining;
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

	const int32 NewItemIndex = Items.Emplace(CellIndex, SafeRemaining);
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
			CellToItemIndex.Add(Items[ItemIndex].CellIndex, ItemIndex);
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
	ContaminationSurfaceSizeOverrideCm = FVector2D::ZeroVector;
	ContaminationOverlayOffsetCm = 0.25f;
	ContaminationOverlayThicknessCm = 0.35f;
	bShowContaminationOverlay = true;
	ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
	ContaminationSurfaceCenterLocal = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMinimum = FVector::ZeroVector;
	ContaminationMeshLocalBoundsMaximum = FVector::ZeroVector;
	CachedContaminationCleanRatio = 0.0f;
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	ReplicatedContaminationState.SetOwner(this);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetRelativeScale3D(FVector(3.f, 0.2f, 2.5f));

	ContaminationOverlay = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ContaminationOverlay"));
	ContaminationOverlay->SetupAttachment(Mesh);
	ContaminationOverlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContaminationOverlay->SetGenerateOverlapEvents(false);
	ContaminationOverlay->SetCanEverAffectNavigation(false);
	ContaminationOverlay->SetReceivesDecals(false);
	ContaminationOverlay->CastShadow = false;
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
			Cell.RemainingContamination = FMath::Clamp(
				Cell.RemainingContamination + Amount, 0.f, Cell.InitialContamination);

			FMD_ScrapeCellChange Change;
			Change.CellIndex = Index;
			Change.GridCoordinate = FIntPoint(X, Y);
			Change.RemainingAmountAfter = Cell.RemainingContamination;
			Changes.Add(Change);
		}
	}

	CachedContaminationCleanRatio = FMD_ScrapeEvaluator::CalculateCleanRatio(ContaminationCells);
	// 오염이 다시 생겼으므로 완료 방송을 다시 허용
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
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
				                                 GetContaminationRemovedPercent(),
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
	if (!TryGetScrapeSurfacePositionFromHit(
			StartHit,
			LocalStroke.StartPositionCm)
		|| !TryGetScrapeSurfacePositionFromHit(
			EndHit,
			LocalStroke.EndPositionCm))
	{
		return FMD_ScrapeResult();
	}

	return ApplyLocalScrapeStrokeWithToolSpec(LocalStroke, ToolSpec);
}

bool AMD_CuttableWall::TryGetScrapeSurfacePositionFromHit(
	const FHitResult& Hit,
	FVector2D& OutSurfacePositionCm)
{
	OutSurfacePositionCm = FVector2D::ZeroVector;
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	return IsHitOnContaminationFace(Hit)
		&& TryProjectWorldPointToContaminationSurface(
			Hit.ImpactPoint,
			OutSurfacePositionCm);
}

bool AMD_CuttableWall::TryGetScrapeSurfaceWorldPosition(
	const FVector2D& SurfacePositionCm,
	FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
	if (!bContaminationSurfaceInitialized
		|| !Mesh
		|| !FMath::IsFinite(SurfacePositionCm.X)
		|| !FMath::IsFinite(SurfacePositionCm.Y))
	{
		return false;
	}

	FVector SurfaceFaceCenterLocal = ContaminationSurfaceCenterLocal;
	SurfaceFaceCenterLocal.Y = ContaminationMeshLocalBoundsMaximum.Y;
	const FVector SurfaceCenterWorld =
		Mesh->GetComponentTransform().TransformPosition(
			SurfaceFaceCenterLocal);
	OutWorldPosition = SurfaceCenterWorld
		+ Mesh->GetForwardVector() * SurfacePositionCm.X
		+ Mesh->GetUpVector() * SurfacePositionCm.Y;
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

	const bool bWasFullyRemoved = IsContaminationFullyRemoved();

	FMD_ScrapeResult Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
		Stroke,
		ToolSpec,
		ScrapeSettings,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm,
		ContaminationCells);

	// 잘못된 입력은 상태를 변경하지 않음
	if (!Result.bIsInputValid)
	{
		return Result;
	}

	CachedContaminationCleanRatio = Result.CleanRatioAfter;
	UpdateContaminationVisualization(Result.CellChanges);
	PublishContaminationChanges(Result.CellChanges);

	// 최초 완료 시 한 번만 방송
	if (!bWasFullyRemoved
		&& Result.bIsSurfaceFullyClean
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

	return FMath::Clamp(CachedContaminationCleanRatio, 0.0f, 1.0f);
}

float AMD_CuttableWall::GetContaminationRemovedPercent() const
{
	return GetContaminationCleanRatio() * 100.0f;
}

float AMD_CuttableWall::GetResidualContaminationRatio() const
{
	// 초기화 실패는 오염 100%로 처리
	return 1.0f - GetContaminationCleanRatio();
}

bool AMD_CuttableWall::IsContaminationFullyRemoved() const
{
	return bContaminationSurfaceInitialized
		&& FMD_ScrapeEvaluator::IsSurfaceFullyClean(
			ContaminationCells,
			ScrapeSettings.CleanTolerance);
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

	return GetContaminationRemovedPercent() >= SafeGate;
}

bool AMD_CuttableWall::ResolveContaminationSurfaceGeometry()
{
	if (!Mesh)
	{
		ResolvedContaminationSurfaceSizeCm = FVector2D::ZeroVector;
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

	if (bHasValidOverride)
	{
		ResolvedContaminationSurfaceSizeCm =
			ContaminationSurfaceSizeOverrideCm;
		return true;
	}

	// 로컬 X/Z Bounds를 월드 cm로 변환
	const FVector LocalBoundsSize =
		ContaminationMeshLocalBoundsMaximum
		- ContaminationMeshLocalBoundsMinimum;
	const FVector AbsoluteComponentScale =
		Mesh->GetComponentScale().GetAbs();

	ResolvedContaminationSurfaceSizeCm = FVector2D(
		LocalBoundsSize.X * AbsoluteComponentScale.X,
		LocalBoundsSize.Z * AbsoluteComponentScale.Z);

	return FMath::IsFinite(ResolvedContaminationSurfaceSizeCm.X)
		&& FMath::IsFinite(ResolvedContaminationSurfaceSizeCm.Y)
		&& ResolvedContaminationSurfaceSizeCm.X > UE_SMALL_NUMBER
		&& ResolvedContaminationSurfaceSizeCm.Y > UE_SMALL_NUMBER;
}

void AMD_CuttableWall::InitializeContaminationSurface()
{
	bContaminationSurfaceInitialized = false;
	bContaminationCompletionBroadcast = false;
	CachedContaminationCleanRatio = 0.0f;
	ContaminationCells.Reset();
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

	const int32 CellCount =
		ContaminationGridDimensions.X * ContaminationGridDimensions.Y;
	ContaminationCells.SetNum(CellCount);

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

	CachedContaminationCleanRatio =
		FMD_ScrapeEvaluator::CalculateCleanRatio(ContaminationCells);
	bContaminationSurfaceInitialized = true;

	// 초기 완료 상태의 중복 방송 방지
	bContaminationCompletionBroadcast =
		IsContaminationFullyRemoved();
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
	const FVector AbsoluteScale = Mesh->GetComponentScale().GetAbs();
	const double SafeScaleX = FMath::Max(
		static_cast<double>(AbsoluteScale.X),
		static_cast<double>(UE_SMALL_NUMBER));
	const double SafeScaleY = FMath::Max(
		static_cast<double>(AbsoluteScale.Y),
		static_cast<double>(UE_SMALL_NUMBER));
	const double SafeScaleZ = FMath::Max(
		static_cast<double>(AbsoluteScale.Z),
		static_cast<double>(UE_SMALL_NUMBER));

	// 월드 크기를 Overlay 로컬 단위로 변환
	const double SurfaceWidthLocal =
		static_cast<double>(ResolvedContaminationSurfaceSizeCm.X)
		/ SafeScaleX;
	const double SurfaceHeightLocal =
		static_cast<double>(ResolvedContaminationSurfaceSizeCm.Y)
		/ SafeScaleZ;
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

	const double SurfaceMinimumX =
		static_cast<double>(ContaminationSurfaceCenterLocal.X)
		- (SurfaceWidthLocal * 0.5);
	const double SurfaceMinimumZ =
		static_cast<double>(ContaminationSurfaceCenterLocal.Z)
		- (SurfaceHeightLocal * 0.5);
	const double ThicknessLocal =
		static_cast<double>(ContaminationOverlayThicknessCm)
		/ SafeScaleY;
	const double OffsetLocal =
		static_cast<double>(ContaminationOverlayOffsetCm)
		/ SafeScaleY;
	const double FrontSurfaceY =
		static_cast<double>(ContaminationMeshLocalBoundsMaximum.Y);

	ContaminationCellBaseTransforms.Reserve(ContaminationCells.Num());
	ContaminationOverlay->PreAllocateInstancesMemory(ContaminationCells.Num());

	for (int32 CellY = 0; CellY < ContaminationGridDimensions.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < ContaminationGridDimensions.X; ++CellX)
		{
			const FVector CellLocation(
				SurfaceMinimumX
					+ (static_cast<double>(CellX) + 0.5) * CellWidthLocal,
				FrontSurfaceY + OffsetLocal + (ThicknessLocal * 0.5),
				SurfaceMinimumZ
					+ (static_cast<double>(CellY) + 0.5) * CellHeightLocal);
			const FVector FullContaminationScale(
				CellWidthLocal / static_cast<double>(OverlayMeshSize.X),
				ThicknessLocal / static_cast<double>(OverlayMeshSize.Y),
				CellHeightLocal / static_cast<double>(OverlayMeshSize.Z));

			ContaminationCellBaseTransforms.Emplace(
				FQuat::Identity,
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
	if (!ContaminationOverlay
		|| CellChanges.IsEmpty()
		|| ContaminationCellBaseTransforms.Num() != ContaminationCells.Num())
	{
		return;
	}

	bool bUpdatedAnyInstance = false;
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (!ContaminationCells.IsValidIndex(CellChange.CellIndex))
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
		if (!ContaminationCells.IsValidIndex(StateItem.CellIndex))
		{
			continue;
		}

		FMD_ScrapeSurfaceCell& Cell = ContaminationCells[StateItem.CellIndex];
		const float PreviousRemaining = Cell.RemainingContamination;
		const float TargetRemaining = bResetToInitial
			? Cell.InitialContamination
			: (FMath::IsFinite(StateItem.RemainingContamination)
				? FMath::Clamp(
					StateItem.RemainingContamination,
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

		FMD_ScrapeCellChange& CellChange = CellChanges.AddDefaulted_GetRef();
		CellChange.CellIndex = StateItem.CellIndex;
		CellChange.GridCoordinate = FIntPoint(
			StateItem.CellIndex % ContaminationGridDimensions.X,
			StateItem.CellIndex / ContaminationGridDimensions.X);
		CellChange.RemovedAmount = FMath::Max(
			0.0f,
			PreviousRemaining - TargetRemaining);
		CellChange.RemainingAmountAfter = TargetRemaining;
		CellChange.bBecameClean =
			PreviousRemaining > ScrapeSettings.CleanTolerance
			&& TargetRemaining <= ScrapeSettings.CleanTolerance;
	}

	CachedContaminationCleanRatio =
		FMD_ScrapeEvaluator::CalculateCleanRatio(ContaminationCells);
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

	const FVector SurfaceCenterWorld =
		Mesh->GetComponentTransform().TransformPosition(
			ContaminationSurfaceCenterLocal);
	const FVector CenterToPoint = WorldPoint - SurfaceCenterWorld;

	// 단위축 내적으로 월드 cm 좌표 계산
	OutSurfacePositionCm = FVector2D(
		FVector::DotProduct(CenterToPoint, Mesh->GetForwardVector()),
		FVector::DotProduct(CenterToPoint, Mesh->GetUpVector()));

	return FMath::IsFinite(OutSurfacePositionCm.X)
		&& FMath::IsFinite(OutSurfacePositionCm.Y);
}

bool AMD_CuttableWall::IsHitOnContaminationFace(
	const FHitResult& Hit) const
{
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

	const FVector SafeImpactNormal =
		Hit.ImpactNormal.GetSafeNormal();
	if (SafeImpactNormal.IsNearlyZero())
	{
		return false;
	}

	// 로컬 +Y를 오염면 방향으로 사용
	return FVector::DotProduct(
			SafeImpactNormal,
			Mesh->GetRightVector())
		>= MinimumContaminationFaceNormalDot;
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
