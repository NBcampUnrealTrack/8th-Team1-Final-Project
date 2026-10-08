#include "World/Trash/MD_TrashSpawnVolume.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Game/MD_GameModeBase.h"
#include "Gameplay/Trash/MD_TrashLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Types/Trash/MD_TrashSpawnState.h"
#include "Util/MD_LogUtility.h"
#include "World/Trash/MD_TrashItem.h"
#include "World/Trash/MD_TrashSpawnManager.h"
#include "World/Trash/MD_TrashFloorPlacementComponent.h"
#include "Data/Trash/MD_TrashSpawnProfile.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

#include <limits>

static bool ResolveFreshFloorScale(FName Kind, FName Variant, FVector& OutScale)
{
    OutScale = FVector::OneVector;
    FString ConfiguredScale;
    const FString Key = Kind.ToString() + TEXT(".") + Variant.ToString();
    if (!GConfig || !GConfig->GetString(TEXT("MarineDemolition.TrashFloorScales"), *Key, ConfiguredScale, GGameIni))
    {
        return true;
    }
    if (!OutScale.InitFromString(ConfiguredScale) || OutScale.ContainsNaN() || OutScale.GetMin() <= 0)
    {
        UE_LOG(MarineDemolition, Error, TEXT("Invalid automatic trash floor scale for '%s': %s"), *Key, *ConfiguredScale);
        return false;
    }
    return true;
}

static bool BuildPolygonVertices(
    const USplineComponent* PolygonSpline,
    const int32 SamplesPerSegment,
    TArray<FVector2D>& OutVertices,
    FBox2D& OutBounds,
    double& OutMinWorldZ,
    double& OutMaxWorldZ)
{
    OutVertices.Reset();
    OutBounds = FBox2D(ForceInit);
    OutMinWorldZ = std::numeric_limits<double>::max();
    OutMaxWorldZ = -std::numeric_limits<double>::max();

    if (!IsValid(PolygonSpline)
        || !PolygonSpline->IsClosedLoop()
        || PolygonSpline->GetNumberOfSplinePoints() < 3)
    {
        return false;
    }

    const int32 SegmentCount = PolygonSpline->GetNumberOfSplineSegments();
    const int32 SafeSamplesPerSegment = FMath::Clamp(SamplesPerSegment, 1, 32);
    const FTransform SplineTransform = PolygonSpline->GetComponentTransform();
    OutVertices.Reserve(SegmentCount * SafeSamplesPerSegment);

    for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
    {
        for (int32 SampleIndex = 0; SampleIndex < SafeSamplesPerSegment; ++SampleIndex)
        {
            const float Alpha = static_cast<float>(SampleIndex) / SafeSamplesPerSegment;
            const FVector LocalLocation = PolygonSpline->GetLocationAtSplineInputKey(
                static_cast<float>(SegmentIndex) + Alpha,
                ESplineCoordinateSpace::Local);
            const FVector WorldLocation = SplineTransform.TransformPosition(LocalLocation);
            if (!FMath::IsFinite(WorldLocation.Z))
            {
                return false;
            }

            OutMinWorldZ = FMath::Min(OutMinWorldZ, WorldLocation.Z);
            OutMaxWorldZ = FMath::Max(OutMaxWorldZ, WorldLocation.Z);

            const FVector2D Point(LocalLocation.X, LocalLocation.Y);
            if (!OutVertices.IsEmpty() && OutVertices.Last().Equals(Point, UE_KINDA_SMALL_NUMBER))
            {
                continue;
            }

            OutVertices.Add(Point);
            OutBounds += Point;
        }
    }

    if (OutVertices.Num() < 3)
    {
        return false;
    }

    double TwiceSignedArea = 0.0;
    for (int32 PointIndex = 0; PointIndex < OutVertices.Num(); ++PointIndex)
    {
        const FVector2D& Current = OutVertices[PointIndex];
        const FVector2D& Next = OutVertices[(PointIndex + 1) % OutVertices.Num()];
        TwiceSignedArea += Current.X * Next.Y - Next.X * Current.Y;
    }

    return !FMath::IsNearlyZero(TwiceSignedArea);
}

static bool IsPointInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Vertices)
{
	bool bInside = false;
	for (int32 CurrentIndex = 0, PreviousIndex = Vertices.Num() - 1;
		CurrentIndex < Vertices.Num();
		PreviousIndex = CurrentIndex++)
	{
		const FVector2D& Current = Vertices[CurrentIndex];
		const FVector2D& Previous = Vertices[PreviousIndex];
		const bool bCrossesHorizontalRay = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
		if (!bCrossesHorizontalRay)
		{
			continue;
		}

		const double IntersectionX = Current.X
			+ (Point.Y - Current.Y) * (Previous.X - Current.X) / (Previous.Y - Current.Y);
		if (Point.X < IntersectionX)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

AMD_TrashSpawnVolume::AMD_TrashSpawnVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PolygonBoundary = CreateDefaultSubobject<USplineComponent>(TEXT("PolygonBoundary"));
	PolygonBoundary->SetupAttachment(SceneRoot);
	PolygonBoundary->SetHiddenInGame(true);
	PolygonBoundary->bDrawDebug = true;
	PolygonBoundary->bEditableWhenInherited = true;
	PolygonBoundary->bInputSplinePointsToConstructionScript = false;
	PolygonBoundary->ClearSplinePoints(false);
	PolygonBoundary->AddSplinePoint(FVector(-500.0f, -500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	PolygonBoundary->AddSplinePoint(FVector(500.0f, -500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	PolygonBoundary->AddSplinePoint(FVector(500.0f, 500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	PolygonBoundary->AddSplinePoint(FVector(-500.0f, 500.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	for (int32 PointIndex = 0; PointIndex < PolygonBoundary->GetNumberOfSplinePoints(); ++PointIndex)
	{
		PolygonBoundary->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
	}
	PolygonBoundary->SetClosedLoop(true, true);

	TrashItemClass = AMD_TrashItem::StaticClass();
	FloorPlacement = CreateDefaultSubobject<UMD_TrashFloorPlacementComponent>(TEXT("FloorPlacement"));
	AllowedTrashCategoryTags.Add(MDGameplayTags::Score_Sorting);

	UpdateShapeVisualization();
}

void AMD_TrashSpawnVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UpdateShapeVisualization();
}

void AMD_TrashSpawnVolume::BeginPlay()
{
    Super::BeginPlay();

    if (GetNetMode() == NM_Client || !HasAuthority() || !IsValid(GetWorld()))
    {
        return;
    }

    if (UMD_TrashSpawnManager* Manager = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
    {
        Manager->RegisterGenerator(this);
    }

    // The project GameMode restores saved counts after all actors have begun play.
    if (GetWorld()->GetAuthGameMode<AMD_GameModeBase>())
    {
        return;
    }

    if (bSpawnOnBeginPlay && !UsesAutomaticFloorCells())
    {
        SpawnTrash();
    }
    else if (UsesAutomaticFloorCells())
    {
        GetWorld()->GetSubsystem<UMD_TrashSpawnManager>()->RequestInitialization();
    }
}

void AMD_TrashSpawnVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority() && IsValid(GetWorld()))
    {
        if (UMD_TrashSpawnManager* Manager = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
        {
            Manager->UnregisterGenerator(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}

static FName DrawTrashKind(FRandomStream& RandomStream, const TArray<FMD_TrashSpawnKindChance>& Chances)
{
    double TotalWeight = 0.0;
    for (const FMD_TrashSpawnKindChance& Chance : Chances)
    {
        TotalWeight += Chance.ProbabilityPercent;
    }

    const double Draw = RandomStream.FRand() * TotalWeight;
    double CumulativeWeight = 0.0;
    FName LastPositiveKind = NAME_None;
    for (const FMD_TrashSpawnKindChance& Chance : Chances)
    {
        if (Chance.ProbabilityPercent <= 0.0f)
        {
            continue;
        }

        LastPositiveKind = Chance.TrashKindId;
        CumulativeWeight += Chance.ProbabilityPercent;
        if (Draw < CumulativeWeight)
        {
            return Chance.TrashKindId;
        }
    }
    return LastPositiveKind;
}

static bool BuildVariantChancesForKind(
    const FName KindId,
    const TArray<FMD_TrashSpawnVariantChance>* ConfiguredChances,
    TArray<FMD_TrashSpawnVariantChance>& OutChances,
    const bool bUseAllVariants = false)
{
    OutChances.Reset();
    if (!ConfiguredChances || ConfiguredChances->IsEmpty())
    {
        if (bUseAllVariants)
        {
            const TArray<FName> VariantIds = UMD_TrashLibrary::GetTrashVariantIds(KindId);
            if (VariantIds.IsEmpty()) return false;
            const float EqualPercent = 100.0f / VariantIds.Num();
            for (const FName VariantId : VariantIds)
            {
                FMD_TrashSpawnVariantChance& Chance = OutChances.AddDefaulted_GetRef();
                Chance.VariantId = VariantId;
                Chance.ProbabilityPercent = EqualPercent;
            }
            return true;
        }
        const FName DefaultVariantId = UMD_TrashLibrary::GetDefaultTrashVariantId(KindId);
        if (DefaultVariantId.IsNone())
        {
            return false;
        }
        FMD_TrashSpawnVariantChance& DefaultChance = OutChances.AddDefaulted_GetRef();
        DefaultChance.VariantId = DefaultVariantId;
        DefaultChance.ProbabilityPercent = 100.0f;
        return true;
    }

    const TArray<FName> ValidVariantIds = UMD_TrashLibrary::GetTrashVariantIds(KindId);
    TSet<FName> SeenVariantIds;
    double TotalPercent = 0.0;
    for (const FMD_TrashSpawnVariantChance& Chance : *ConfiguredChances)
    {
        if (Chance.VariantId.IsNone()
            || !ValidVariantIds.Contains(Chance.VariantId)
            || SeenVariantIds.Contains(Chance.VariantId)
            || !FMath::IsFinite(Chance.ProbabilityPercent)
            || Chance.ProbabilityPercent < 0.0f
            || Chance.ProbabilityPercent > 100.0f)
        {
            return false;
        }
        SeenVariantIds.Add(Chance.VariantId);
        TotalPercent += Chance.ProbabilityPercent;
        OutChances.Add(Chance);
    }
    return FMath::IsNearlyEqual(TotalPercent, 100.0, 0.001);
}

static FName DrawTrashVariant(
    FRandomStream& RandomStream,
    const TArray<FMD_TrashSpawnVariantChance>& Chances)
{
    const double Draw = RandomStream.FRand() * 100.0;
    double CumulativeWeight = 0.0;
    FName LastPositiveVariant = NAME_None;
    for (const FMD_TrashSpawnVariantChance& Chance : Chances)
    {
        if (Chance.ProbabilityPercent <= 0.0f)
        {
            continue;
        }
        LastPositiveVariant = Chance.VariantId;
        CumulativeWeight += Chance.ProbabilityPercent;
        if (Draw < CumulativeWeight)
        {
            return Chance.VariantId;
        }
    }
    return LastPositiveVariant;
}

int32 AMD_TrashSpawnVolume::SpawnTrash()
{

    if (GetNetMode() == NM_Client || !HasAuthority() || SpawnCount <= 0 || !IsValid(GetWorld()))
    {
        return 0;
    }
    // Automatic cells are populated once. Explicit RegenerateTrash resets the ledger.
    if (UsesAutomaticFloorCells() && (bStateInitialized || !FloorPlacement->IsReady())) return 0;
    InitializeFloorLedger();

    TArray<FMD_TrashSpawnKindChance> Chances;
    if (!BuildTrashKindChances(Chances))
    {
        return 0;
    }

    LastUsedSeed = bRandomizeSeedEachSpawn
        ? static_cast<int32>(GetTypeHash(FGuid::NewGuid()))
        : SpawnSeed;
    FRandomStream KindRandomStream(LastUsedSeed);
    TArray<FName> KindIds;
    KindIds.Reserve(SpawnCount);
    for (int32 SpawnIndex = 0; SpawnIndex < SpawnCount; ++SpawnIndex)
    {
        KindIds.Add(DrawTrashKind(KindRandomStream, Chances));
    }

    const int32 PreviousCount = GetSpawnedTrashCount();
    const bool bPlacedAll = PlaceTrashKinds(KindIds, LastUsedSeed);
    if (!bPlacedAll && UsesAutomaticFloorCells())
    {
        ClearSpawnedTrashInternal();
        NotifyManagerStateChanged();
        return 0;
    }
    const int32 AddedCount = GetSpawnedTrashCount() - PreviousCount;
    if (AddedCount > 0)
    {
        bStateInitialized = true;
    }
    if (!bPlacedAll)
    {
        UE_LOG(MarineDemolition, Warning,
            TEXT("TrashSpawnVolume '%s' requested %d items but placed %d."),
            *GetName(), SpawnCount, AddedCount);
    }
    NotifyManagerStateChanged();
    return AddedCount;
}

void AMD_TrashSpawnVolume::ClearSpawnedTrashInternal()
{
    for (AMD_TrashItem* SpawnedItem : SpawnedTrashItems)
    {
        if (IsValid(SpawnedItem))
        {
            SpawnedItem->Destroy();
        }
    }
    SpawnedTrashItems.Reset();
    RemainingKindCounts.Reset();
    RemainingFloorCells.Reset();
    bStateInitialized = false;
}

void AMD_TrashSpawnVolume::ClearSpawnedTrash()
{
    if (GetNetMode() == NM_Client || !HasAuthority())
    {
        return;
    }

    ClearSpawnedTrashInternal();
    NotifyManagerStateChanged();
}

int32 AMD_TrashSpawnVolume::RegenerateTrash()
{
    if (GetNetMode() == NM_Client || !HasAuthority())
    {
        return 0;
    }

    ClearSpawnedTrashInternal();
    const int32 SpawnedCount = SpawnTrash();
    if (SpawnedCount == 0)
    {
        NotifyManagerStateChanged();
    }
    return SpawnedCount;
}

int32 AMD_TrashSpawnVolume::GetSpawnedTrashCount() const
{
    int32 LiveCount = 0;
    for (const AMD_TrashItem* SpawnedItem : SpawnedTrashItems)
    {
        LiveCount += IsValid(SpawnedItem) ? 1 : 0;
    }
    return LiveCount;
}

int32 AMD_TrashSpawnVolume::GetRemainingTrashCount() const
{
    int64 TotalCount = 0;
    for (const TPair<FName, int32>& Entry : RemainingKindCounts)
    {
        TotalCount += FMath::Max(0, Entry.Value);
    }
    return static_cast<int32>(FMath::Min<int64>(TotalCount, MAX_int32));
}

int32 AMD_TrashSpawnVolume::GetRemainingKindCount(const FName TrashKindId) const
{
    return FMath::Max(0, RemainingKindCounts.FindRef(TrashKindId));
}
bool AMD_TrashSpawnVolume::PrepareSpawnLocations(const double BudgetSeconds, bool& bReady)
{
    bReady = !UsesAutomaticFloorCells();
    if (bStackAsTowers && (!IsFloorBonusVolume() || TowerCount < 0
        || !FMath::IsFinite(PileSpread) || PileSpread < 0.5f || PileSpread > 3.0f)) return false;
    if (!UsesAutomaticFloorCells()) return true;
    if (!IsFloorBonusVolume()) return FloorPlacement->Prepare(BudgetSeconds, bReady);
    auto* Manager = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>();
    AMD_TrashSpawnVolume* Source = nullptr;
    for (AMD_TrashSpawnVolume* Generator : Manager->GetGenerators())
    {
        if (Generator != this && Generator->UsesAutomaticFloorCells() && !Generator->IsFloorBonusVolume())
        {
            if (Source) return false;
            Source = Generator;
        }
    }
    if (!Source)
    {
        UE_LOG(MarineDemolition, Error, TEXT("Bonus trash volume '%s' requires one whole-map floor generator."), *GetName());
        return false;
    }
    if (!Source->GetFloorPlacement()->IsReady()) return true;
    TArray<FVector2D> Vertices;
    FBox2D Bounds(ForceInit);
    double MinZ = 0, MaxZ = 0;
    if (!FMath::IsFinite(ShapeHalfHeight) || ShapeHalfHeight < 0
        || !BuildPolygonVertices(PolygonBoundary, PolygonSamplesPerSegment, Vertices, Bounds, MinZ, MaxZ)) return false;
    const FTransform Shape = PolygonBoundary->GetComponentTransform();
    return FloorPlacement->PrepareSubset(*Source->GetFloorPlacement(),
        [&](const FVector& Point)
        {
            const FVector Local = Shape.InverseTransformPosition(Point);
            const FVector2D XY(Local.X, Local.Y);
            return Point.Z >= MinZ - ShapeHalfHeight && Point.Z <= MaxZ + ShapeHalfHeight
                && Bounds.IsInside(XY) && IsPointInsidePolygon(XY, Vertices);
        }, BudgetSeconds, bReady);
}

void AMD_TrashSpawnVolume::InitializeFloorLedger()
{
    if (!UsesAutomaticFloorCells() || !RemainingFloorCells.IsEmpty()) return;
    for (const auto& Entry : FloorPlacement->GetCells())
        RemainingFloorCells.FindOrAdd(Entry.Key).Cell = Entry.Key;
}

bool AMD_TrashSpawnVolume::CanRestoreState(const FMD_TrashGeneratorState& State) const
{
    if (GetNetMode() == NM_Client || !HasAuthority() || bStateInitialized
        || State.GeneratorName != GetFName() || !State.IsStructurallyValid()
        || State.bUsesFloorCells != UsesAutomaticFloorCells() || State.bUsesTowerPlacement != UsesTowerPlacement())
    {
        return false;
    }

    if (UsesAutomaticFloorCells())
    {
        if (!FloorPlacement->IsReady() || State.FloorCellSize != FloorPlacement->CellSize
            || State.FloorLayerHeight != FloorPlacement->LayerHeight
            || State.FloorLayoutId != FloorPlacement->GetLayoutId()
            || State.FloorCells.Num() != FloorPlacement->GetCellCount()) return false;
        for (const FMD_TrashFloorCellState& Cell : State.FloorCells)
        {
            const auto* Candidates = FloorPlacement->GetCells().Find(Cell.Cell);
            int64 CellCount = 0;
            for (const auto& Entry : Cell.RemainingKindCounts) CellCount += Entry.Value;
            if (!Candidates || (!UsesTowerPlacement() && CellCount > Candidates->Num())) return false;
        }
    }
    int64 TotalCount = 0;
    for (const TPair<FName, int32>& Entry : State.RemainingKindCounts)
    {
        if (Entry.Key.IsNone() || Entry.Value < 0)
        {
            return false;
        }
        if (Entry.Value == 0)
        {
            continue;
        }

        FMD_TrashKindRow KindRow;
        if (!UMD_TrashLibrary::FindTrashKindRow(Entry.Key, KindRow)
            || !KindRow.Category.IsValid())
        {
            return false;
        }

        TotalCount += Entry.Value;
        if (TotalCount > MAX_int32)
        {
            return false;
        }
    }

    if (UsesAutomaticFloorCells() && !UsesTowerPlacement()
        && TotalCount > FloorPlacement->GetCandidateCount()) return false;
    return true;
}

bool AMD_TrashSpawnVolume::RestoreState(const FMD_TrashGeneratorState& State)
{
    if (!CanRestoreState(State)) return false;
    TArray<FName> KindIds;
    TArray<FIntVector> RequiredCells;
    int64 TotalCount = 0;
    for (const auto& Entry : State.RemainingKindCounts) TotalCount += Entry.Value;
    KindIds.Reserve(static_cast<int32>(TotalCount));
    if (UsesAutomaticFloorCells())
    {
        RequiredCells.Reserve(static_cast<int32>(TotalCount));
        for (const FMD_TrashFloorCellState& Cell : State.FloorCells)
        {
            for (const auto& Entry : Cell.RemainingKindCounts)
            {
                for (int32 Index = 0; Index < Entry.Value; ++Index)
                {
                    KindIds.Add(Entry.Key);
                    RequiredCells.Add(Cell.Cell);
                }
            }
        }
    }
    else
        for (const auto& Entry : State.RemainingKindCounts)
            for (int32 Index = 0; Index < Entry.Value; ++Index) KindIds.Add(Entry.Key);
    InitializeFloorLedger();

    LastUsedSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
    if (!PlaceTrashKinds(KindIds, LastUsedSeed, UsesAutomaticFloorCells() ? &RequiredCells : nullptr))
    {
        ClearSpawnedTrashInternal();
        return false;
    }

    bStateInitialized = true;
    NotifyManagerStateChanged();
    return true;
}

bool AMD_TrashSpawnVolume::InitializeFresh()
{

    if (GetNetMode() == NM_Client || !HasAuthority())
    {
        return false;
    }
    if (bStateInitialized)
    {
        return true;
    }
    if (UsesAutomaticFloorCells() && !FloorPlacement->IsReady()) return false;
    if (UsesAutomaticFloorCells() && bSpawnOnBeginPlay && !UsesTowerPlacement()
        && SpawnCount > FloorPlacement->GetCandidateCount()) return false;
    InitializeFloorLedger();
    if (!bSpawnOnBeginPlay || SpawnCount <= 0)
    {
        bStateInitialized = true;
        NotifyManagerStateChanged();
        return true;
    }

    TArray<FMD_TrashSpawnKindChance> Chances;
    if (!BuildTrashKindChances(Chances))
    {
        return false;
    }

    FRandomStream KindRandomStream(static_cast<int32>(GetTypeHash(FGuid::NewGuid())));
    TArray<FName> KindIds;
    KindIds.Reserve(SpawnCount);
    for (int32 SpawnIndex = 0; SpawnIndex < SpawnCount; ++SpawnIndex)
    {
        KindIds.Add(DrawTrashKind(KindRandomStream, Chances));
    }

    LastUsedSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
    LastSpawnShortfall = 0;
    if (!PlaceTrashKinds(KindIds, LastUsedSeed))
    {
        ClearSpawnedTrashInternal();
        // 고정 개수 볼륨은 기존대로 실패. 비율 볼륨은 공간이 허락하는 만큼 줄여 놓고 부족분을 기록한다
        if (!FMath::IsFinite(SpawnShare) || SpawnShare <= 0.0f)
        {
            return false;
        }
        // 들어가는 최대 개수를 이분 탐색으로 찾는다. 실패는 배치 전 계획 단계에서 끝나므로 비용이 작다
        const TArray<FName> FullKindIds = KindIds;
        int32 Attempt = 0;
        int32 Low = 0;
        int32 High = FullKindIds.Num();
        while (High - Low > 1)
        {
            const int32 Mid = (Low + High) / 2;
            // 정리 함수가 바닥 셀 장부까지 비우므로 매 시도 전에 다시 채운다
            InitializeFloorLedger();
            KindIds = FullKindIds;
            KindIds.SetNum(Mid);
            if (PlaceTrashKinds(KindIds, LastUsedSeed + ++Attempt))
            {
                Low = Mid;
                ClearSpawnedTrashInternal();
            }
            else
            {
                High = Mid;
                ClearSpawnedTrashInternal();
            }
        }
        InitializeFloorLedger();
        if (Low > 0)
        {
            KindIds = FullKindIds;
            KindIds.SetNum(Low);
            // 계획은 무작위라 같은 개수가 다시 실패할 수 있다. 그때는 한 개씩 줄여 확정한다
            while (!KindIds.IsEmpty() && !PlaceTrashKinds(KindIds, LastUsedSeed + ++Attempt))
            {
                ClearSpawnedTrashInternal();
                InitializeFloorLedger();
                KindIds.SetNum(KindIds.Num() - 1);
            }
        }
        LastSpawnShortfall = SpawnCount - GetSpawnedTrashCount();
        UE_LOG(MarineDemolition, Warning,
            TEXT("TrashSpawnVolume '%s' placed %d of %d shared items; %d passed on to other volumes."),
            *GetName(), GetSpawnedTrashCount(), SpawnCount, LastSpawnShortfall);
    }

    bStateInitialized = true;
    NotifyManagerStateChanged();
    return true;
}

bool AMD_TrashSpawnVolume::BuildEmptyState(FMD_TrashGeneratorState& Out) const
{
    Out = FMD_TrashGeneratorState();
    Out.GeneratorName = GetFName();
    Out.bUsesTowerPlacement = UsesTowerPlacement();
    if (UsesAutomaticFloorCells())
    {
        if (!FloorPlacement->IsReady()) return false;
        Out.bUsesFloorCells = true;
        Out.FloorCellSize = FloorPlacement->CellSize;
        Out.FloorLayerHeight = FloorPlacement->LayerHeight;
        Out.FloorLayoutId = FloorPlacement->GetLayoutId();
        for (const auto& Entry : FloorPlacement->GetCells())
            Out.FloorCells.AddDefaulted_GetRef().Cell = Entry.Key;
    }
    return Out.IsStructurallyValid();
}

bool AMD_TrashSpawnVolume::ConvertAuthoredItems(const TArray<AMD_TrashItem*>& Items, const TArray<FVector>* OriginalPositions)
{
    if (!HasAuthority() || !UsesAutomaticFloorCells() || !FloorPlacement->IsReady()
        || GetRemainingTrashCount() != 0 || (OriginalPositions && OriginalPositions->Num() != Items.Num())) return false;
    TArray<FName> Kinds;
    TArray<FIntVector> Cells;
    TArray<AActor*> Sources;
    for (int32 I = 0; I < Items.Num(); ++I)
    {
        AMD_TrashItem* Item = Items[I];
        if (!IsValid(Item) || !Item->HasAuthority() || Item->GetWorld() != GetWorld() || Sources.Contains(Item)
            || Item->GetOriginGenerator() || (I > 0 && Item->GetClass() != Items[0]->GetClass())) return false;
        FIntVector Cell;
        if (OriginalPositions && !FloorPlacement->FindOriginalCell((*OriginalPositions)[I], Cell)) return false;
        Kinds.Add(Item->GetTrashKindId());
        Cells.Add(Cell);
        Sources.Add(Item);
    }
    if (!Items.IsEmpty()) TrashItemClass = Items[0]->GetClass();
    // Empty placeholder state may have been restored alongside legacy volume records.
    ClearSpawnedTrashInternal();
    InitializeFloorLedger();
    FloorPlacement->SetTransitionSources(Sources);
    LastUsedSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
    const bool Placed = PlaceTrashKinds(Kinds, LastUsedSeed, OriginalPositions ? &Cells : nullptr, &Items);
    FloorPlacement->SetTransitionSources({});
    if (!Placed) { ClearSpawnedTrashInternal(); return false; }
    bStateInitialized = true;
    NotifyManagerStateChanged();
    return true;
}

bool AMD_TrashSpawnVolume::CanConsumeTrash(const FName TrashKindId, const FIntVector* FloorCell) const
{
    if (GetNetMode() == NM_Client || !HasAuthority() || !bStateInitialized
        || RemainingKindCounts.FindRef(TrashKindId) <= 0) return false;
    if (!UsesAutomaticFloorCells()) return true;
    const FMD_TrashFloorCellState* Cell = FloorCell ? RemainingFloorCells.Find(*FloorCell) : nullptr;
    return Cell && Cell->RemainingKindCounts.FindRef(TrashKindId) > 0;
}

bool AMD_TrashSpawnVolume::NotifyTrashConsumed(const FName TrashKindId, const FIntVector* FloorCell)
{
    if (!CanConsumeTrash(TrashKindId, FloorCell))
    {
        UE_LOG(MarineDemolition, Warning,
            TEXT("TrashSpawnVolume '%s' cannot consume untracked kind '%s'."),
            *GetName(), *TrashKindId.ToString());
        return false;
    }

    if (UsesAutomaticFloorCells())
    {
        auto& Counts = RemainingFloorCells.FindChecked(*FloorCell).RemainingKindCounts;
        if (--Counts.FindChecked(TrashKindId) == 0) Counts.Remove(TrashKindId);
    }
    int32* RemainingCount = RemainingKindCounts.Find(TrashKindId);
    --(*RemainingCount);
    if (*RemainingCount == 0)
    {
        RemainingKindCounts.Remove(TrashKindId);
    }
    // Destroyed UPROPERTY references are cleared by GC. Compact once before a new batch,
    // rather than scanning the complete actor array on every individual consumption.
    NotifyManagerStateChanged();
    return true;
}

bool AMD_TrashSpawnVolume::BuildTrashKindChances(TArray<FMD_TrashSpawnKindChance>& OutChances) const
{
    OutChances.Reset();
    const TArray<FMD_TrashSpawnKindChance>& EffectiveChances = GetEffectiveKindChances();
    if (EffectiveChances.IsEmpty())
    {
        const TArray<FName> EligibleIds = BuildEligibleTrashKindIds();
        for (const FName KindId : EligibleIds)
        {
            FMD_TrashSpawnKindChance& Chance = OutChances.AddDefaulted_GetRef();
            Chance.TrashKindId = KindId;
            Chance.ProbabilityPercent = 1.0f;
        }
        return !OutChances.IsEmpty();
    }

    TSet<FName> SeenIds;
    double TotalPercent = 0.0;
    for (const FMD_TrashSpawnKindChance& Chance : EffectiveChances)
    {
        FMD_TrashKindRow KindRow;
        if (Chance.TrashKindId.IsNone()
            || SeenIds.Contains(Chance.TrashKindId)
            || !FMath::IsFinite(Chance.ProbabilityPercent)
            || Chance.ProbabilityPercent < 0.0f
            || Chance.ProbabilityPercent > 100.0f
            || !UMD_TrashLibrary::FindTrashKindRow(Chance.TrashKindId, KindRow)
            || !KindRow.Category.IsValid())
        {
            UE_LOG(MarineDemolition, Warning,
                TEXT("TrashSpawnVolume '%s' has an invalid or duplicate kind chance."),
                *GetName());
            return false;
        }
        if (Chance.ProbabilityPercent > 0.0f)
        {
            TArray<FMD_TrashSpawnVariantChance> ResolvedVariants;
            if (!BuildVariantChancesForKind(
                Chance.TrashKindId, &Chance.VariantChances, ResolvedVariants))
            {
                UE_LOG(MarineDemolition, Warning,
                    TEXT("TrashSpawnVolume '%s' kind '%s' has invalid Variant chances or no usable mesh (Variant probabilities must total 100%%)."),
                    *GetName(), *Chance.TrashKindId.ToString());
                return false;
            }
        }
        SeenIds.Add(Chance.TrashKindId);
        TotalPercent += Chance.ProbabilityPercent;
        OutChances.Add(Chance);
    }

    if (!FMath::IsNearlyEqual(TotalPercent, 100.0, 0.001))
    {
        UE_LOG(MarineDemolition, Warning,
            TEXT("TrashSpawnVolume '%s' kind probabilities must total 100%% (actual %.3f%%)."),
            *GetName(), TotalPercent);
        return false;
    }
    return true;
}

bool AMD_TrashSpawnVolume::PlaceTrashKinds(const TArray<FName>& KindIds, const int32 PlacementSeed,
    const TArray<FIntVector>* RequiredCells, const TArray<AMD_TrashItem*>* ReusedItems)
{

    if (KindIds.IsEmpty())
    {
        return true;
    }
    if (!IsValid(GetWorld()) || !TrashItemClass)
    {
        return false;
    }
    if (ReusedItems)
    {
        if (!UsesAutomaticFloorCells() || ReusedItems->Num() != KindIds.Num()) return false;
        TSet<const AMD_TrashItem*> Seen;
        for (int32 I = 0; I < ReusedItems->Num(); ++I)
        {
            const AMD_TrashItem* Item = (*ReusedItems)[I];
            const UStaticMeshComponent* Mesh = IsValid(Item) ? Cast<UStaticMeshComponent>(Item->GetRootComponent()) : nullptr;
            if (!Mesh || !Item->HasAuthority() || Item->GetWorld() != GetWorld() || Item->GetOriginGenerator()
                || Item->GetTrashKindId() != KindIds[I] || !Mesh->GetStaticMesh()
                || Mesh->Mobility != EComponentMobility::Movable || Seen.Contains(Item)) return false;
            Seen.Add(Item);
        }
    }

    // Resolve each used kind once. With no kind overrides, every usable Variant is eligible.
    // Restored kinds absent from an explicit probability list retain their table default.
    TMap<FName, TArray<FMD_TrashSpawnVariantChance>> VariantChancesByKind;
    for (const FName KindId : KindIds)
    {
        if (VariantChancesByKind.Contains(KindId))
        {
            continue;
        }
        const TArray<FMD_TrashSpawnKindChance>& EffectiveChances = GetEffectiveKindChances();
        const FMD_TrashSpawnKindChance* ConfiguredKind = EffectiveChances.FindByPredicate(
            [KindId](const FMD_TrashSpawnKindChance& Chance)
            {
                return Chance.TrashKindId == KindId;
            });
        TArray<FMD_TrashSpawnVariantChance> ResolvedVariants;
        if (!BuildVariantChancesForKind(
            KindId, ConfiguredKind ? &ConfiguredKind->VariantChances : nullptr, ResolvedVariants,
            EffectiveChances.IsEmpty()))
        {
            UE_LOG(MarineDemolition, Warning,
                TEXT("TrashSpawnVolume '%s' cannot place kind '%s': invalid Variant chances or no usable mesh."),
                *GetName(), *KindId.ToString());
            return false;
        }
        VariantChancesByKind.Add(KindId, MoveTemp(ResolvedVariants));
    }

    TArray<FVector2D> PolygonVertices;
    FBox2D PolygonBounds(ForceInit);
    double MinWorldZ = 0.0;
    double MaxWorldZ = 0.0;
    if (!UsesAutomaticFloorCells() && (!FMath::IsFinite(ShapeHalfHeight) || ShapeHalfHeight < 0.0f
        || !BuildPolygonVertices(PolygonBoundary, PolygonSamplesPerSegment,
            PolygonVertices, PolygonBounds, MinWorldZ, MaxWorldZ)))
    {
        return false;
    }

    const double TracePadding = FMath::Max<double>(ShapeHalfHeight, 10.0);
    const double TraceStartZ = MaxWorldZ + TracePadding;
    const double TraceEndZ = MinWorldZ - TracePadding;
    CompactSpawnedTrashItems();
    TArray<FVector> OccupiedLocations;
    OccupiedLocations.Reserve(SpawnedTrashItems.Num() + KindIds.Num());
    for (const AMD_TrashItem* ExistingItem : SpawnedTrashItems)
    {
        if (IsValid(ExistingItem))
        {
            OccupiedLocations.Add(ExistingItem->GetActorLocation());
        }
    }

    TArray<float> OccupiedRadii;
    OccupiedRadii.Init(0.0f, OccupiedLocations.Num());
    float SharedMinimumSpacing = MinimumSpacing;
    if (UsesAutomaticFloorCells())
        GetWorld()->GetSubsystem<UMD_TrashSpawnManager>()->AppendOccupiedTrash(
            this, OccupiedLocations, OccupiedRadii, SharedMinimumSpacing);

    FRandomStream PlacementRandomStream(PlacementSeed);
    FRandomStream VariantRandomStream(PlacementSeed ^ 0x4B1D2F3A);
    TArray<FMD_TrashFloorPoint> FloorPlan;
    TArray<FName> FloorVariants;
    TArray<FBox> FloorBounds;
    TArray<FQuat> FloorRotations;
    TArray<FTransform> FloorTransforms;
    TArray<FVector> FloorScales;
    TArray<float> FloorRadii;
    TArray<int32> TowerIds;
    TArray<float> MoundEnvelopeHeights;
    if (UsesAutomaticFloorCells())
    {
        // A saved cell can fit the remaining count yet reject a particular new variant/orientation.
        // Retry the random appearance plan before spawning anything; never move counts between cells.
        bool bPlanned = false;
        for (int32 AppearanceAttempt = 0; AppearanceAttempt < (RequiredCells ? 8 : 1); ++AppearanceAttempt)
        {
            FloorVariants.Reset(); FloorBounds.Reset(); FloorRotations.Reset();
            FloorScales.Reset(); FloorRadii.Reset();
            for (const FName Kind : KindIds)
            {
                FName Variant = DrawTrashVariant(VariantRandomStream, VariantChancesByKind.FindChecked(Kind));
                FVector Scale = FVector::OneVector;
                if (!FloorAppearances.IsEmpty())
                {
                    const auto* Profiles = FloorAppearances.Find(Kind);
                    if (!Profiles || Profiles->IsEmpty()) return false;
                    int32 Total = 0;
                    for (const auto& Profile : *Profiles) Total += Profile.Weight;
                    int32 Draw = VariantRandomStream.RandRange(1, Total);
                    for (const auto& Profile : *Profiles)
                    {
                        Draw -= Profile.Weight;
                        if (Draw <= 0) { Variant = Profile.Variant; Scale = Profile.Scale; break; }
                    }
                }
                // Reusing a loaded actor keeps its mesh/scale and avoids rebuilding its physics body.
                if (ReusedItems)
                {
                    const AMD_TrashItem* Item = (*ReusedItems)[FloorVariants.Num()];
                    Variant = Item->GetTrashVariantId();
                    Scale = Item->GetActorScale3D();
                }
                else if (FloorAppearances.IsEmpty() && !ResolveFreshFloorScale(Kind, Variant, Scale))
                {
                    return false;
                }
                TSoftObjectPtr<UStaticMesh> Mesh;
                if (!UMD_TrashLibrary::ResolveTrashKindMesh(Kind, Variant, Mesh) || !Mesh.LoadSynchronous()) return false;
#if WITH_EDITOR
                // A cold editor load can leave collision unavailable until asynchronous mesh
                // compilation finishes. Tower packing needs these shapes during this spawn.
                if (UsesTowerPlacement() && Mesh->IsCompiling())
                {
                    TArray<UStaticMesh*> PendingMeshes{Mesh.Get()};
                    FStaticMeshCompilingManager::Get().FinishCompilation(PendingMeshes);
                }
#endif
                if (ReusedItems && CastChecked<UStaticMeshComponent>((*ReusedItems)[FloorVariants.Num()]->GetRootComponent())->GetStaticMesh() != Mesh.Get()) return false;
                FloorVariants.Add(Variant);
                if (Scale.ContainsNaN() || Scale.GetMin() <= 0) return false;
                FloorScales.Add(Scale);
                const FBox Bounds = Mesh.Get()->GetBoundingBox();
                FloorBounds.Add(FBox(Bounds.Min * Scale, Bounds.Max * Scale));
                // Uniformly choose an up direction and twist: include side-facing and upside-down items.
                const FVector Up = PlacementRandomStream.VRand();
                const float Twist = PlacementRandomStream.FRandRange(0.0f, 2.0f * PI);
                FloorRotations.Add(FQuat::FindBetweenNormals(FVector::UpVector, Up) * FQuat(FVector::UpVector, Twist));
                const FVector Extent = FloorBounds.Last().TransformBy(FTransform(FloorRotations.Last())).GetExtent();
                // Radius is measured from the floor anchor after rotation, including the full height.
                FloorRadii.Add(static_cast<float>(FVector(Extent.X, Extent.Y, Extent.Z * 2.0).Size() + 2.0));
            }
            FloorTransforms.SetNum(KindIds.Num());
            const int32 AttemptSeed = static_cast<int32>(static_cast<uint32>(PlacementSeed) + static_cast<uint32>(AppearanceAttempt));
            if (UsesTowerPlacement())
                bPlanned = FloorPlacement->PlanTowers(RequiredCells, OccupiedLocations, OccupiedRadii, SharedMinimumSpacing,
                    AttemptSeed, FloorBounds, FloorRotations, MaxItemsPerTower, MaxTowerHeight, FloorPlan, FloorTransforms, TowerIds,
                    TowerCount, ItemsPerLayer, true, PileSpread, &MoundEnvelopeHeights);
            else
                bPlanned = FloorPlacement->Plan(KindIds.Num(), RequiredCells, OccupiedLocations, SharedMinimumSpacing, AttemptSeed, FloorPlan,
                    [&](int32 Index, const FMD_TrashFloorPoint& Point)
                    {
                        return FloorPlacement->Fit(Point, FloorBounds[Index], FloorRotations[Index], FloorTransforms[Index]);
                    }, &FloorRadii, &OccupiedRadii);
            if (bPlanned) break;
        }
        if (!bPlanned)
        {
            UE_LOG(MarineDemolition, Warning,
                TEXT("Trash volume '%s' could not fit %d items (pile count=%d, spread=%.2f). Check the spline, saved cells, item sizes and free space."),
                *GetName(), KindIds.Num(), UsesTowerPlacement() ? TowerCount : 0, PileSpread);
            return false;
        }
        for (int32 I = 0; I < KindIds.Num(); ++I)
        {
            FloorTransforms[I].SetScale3D(FloorScales[I]);
        }
    }
    for (int32 ItemIndex = 0; ItemIndex < KindIds.Num(); ++ItemIndex)
    {
        const FName KindId = KindIds[ItemIndex];
        const FName VariantId = UsesAutomaticFloorCells() ? FloorVariants[ItemIndex] : DrawTrashVariant(
            VariantRandomStream, VariantChancesByKind.FindChecked(KindId));
        FVector SpawnLocation;
        if (UsesAutomaticFloorCells()) SpawnLocation = FloorPlan[ItemIndex].Location;
        else if (!FindSpawnLocation(PlacementRandomStream, OccupiedLocations,
            PolygonVertices, PolygonBounds, TraceStartZ, TraceEndZ, SpawnLocation))
        {
            return false;
        }

        const FRotator SpawnRotation(0.0f, PlacementRandomStream.FRandRange(0.0f, 360.0f), 0.0f);
        FTransform SpawnTransform(SpawnRotation, SpawnLocation);
        if (UsesAutomaticFloorCells()) SpawnTransform = FloorTransforms[ItemIndex];
        if (ReusedItems)
        {
            AMD_TrashItem* Item = (*ReusedItems)[ItemIndex];
            Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
            Item->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
            auto* Mesh = CastChecked<UStaticMeshComponent>(Item->GetRootComponent());
            if (Mesh->IsSimulatingPhysics())
            {
                Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
                Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            }
            Item->SetOriginGenerator(this);
            Item->SetOriginFloorCell(FloorPlan[ItemIndex].Cell);
            Item->FlushNetDormancy();
            Item->ForceNetUpdate();
            SpawnedTrashItems.Add(Item);
            ++RemainingKindCounts.FindOrAdd(KindId);
            ++RemainingFloorCells.FindChecked(FloorPlan[ItemIndex].Cell).RemainingKindCounts.FindOrAdd(KindId);
            continue;
        }
        AMD_TrashItem* SpawnedItem = GetWorld()->SpawnActorDeferred<AMD_TrashItem>(
            TrashItemClass, SpawnTransform, this, nullptr,
            UsesAutomaticFloorCells() ? ESpawnActorCollisionHandlingMethod::AlwaysSpawn
                : ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
        if (!IsValid(SpawnedItem))
        {
            return false;
        }

        // The checked transform contains the intended mesh scale, including an
        // authored transition profile. Do not multiply the Blueprint's default scale again.
        SpawnedItem->UseAuthoredMeshScaleForProceduralSpawn();
        if (!SpawnedItem->SetTrashAppearance(KindId, VariantId))
        {
            UGameplayStatics::FinishSpawningActor(SpawnedItem, SpawnTransform);
            SpawnedItem->Destroy();
            return false;
        }
        SpawnedItem->SetOriginGenerator(this);
        if (UsesAutomaticFloorCells()) SpawnedItem->SetOriginFloorCell(FloorPlan[ItemIndex].Cell);

        // Place the mesh bottom slightly above the traced support before physics starts.
        if (!UsesAutomaticFloorCells())
        {
            const FBox Bounds = SpawnedItem->GetComponentsBoundingBox(true);
            const double BottomCorrection = Bounds.IsValid
                ? SpawnLocation.Z - Bounds.Min.Z : 10.0;
            SpawnTransform.AddToTranslation(FVector(0.0, 0.0, BottomCorrection + 2.0));
        }

        UGameplayStatics::FinishSpawningActor(SpawnedItem, SpawnTransform);
        if (!IsValid(SpawnedItem))
        {
            return false;
        }

        if (UsesAutomaticFloorCells()) SpawnedItem->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);

        SpawnedTrashItems.Add(SpawnedItem);
        OccupiedLocations.Add(SpawnedItem->GetActorLocation());
        ++RemainingKindCounts.FindOrAdd(KindId);
        if (UsesAutomaticFloorCells())
            ++RemainingFloorCells.FindChecked(FloorPlan[ItemIndex].Cell).RemainingKindCounts.FindOrAdd(KindId);
    }
    if (UsesTowerPlacement())
    {
        // The conservative column fit already reserves this entire space. Sweep each real
        // collision shape down, bottom first, so rotated objects touch instead of floating
        // at the corners of their bounding boxes. Keep a small nonpenetrating clearance.
        // Disable scene queries for unplaced shapes, keeping their physics collision and
        // simulation active. This avoids an O(N^2) future-actor ignore list for tall towers.
        TArray<ECollisionEnabled::Type> QueryModes;
        TArray<double> MoundTops;
        MoundTops.Init(0.0, MoundEnvelopeHeights.Num());
        QueryModes.Reserve(SpawnedTrashItems.Num());
        for (const auto& Item : SpawnedTrashItems)
        {
            auto* Mesh = CastChecked<UStaticMeshComponent>(Item->GetRootComponent());
            QueryModes.Add(Mesh->GetCollisionEnabled());
            Mesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
        }
        ON_SCOPE_EXIT
        {
            for (int32 I = 0; I < SpawnedTrashItems.Num(); ++I)
                if (IsValid(SpawnedTrashItems[I]))
                    CastChecked<UStaticMeshComponent>(SpawnedTrashItems[I]->GetRootComponent())->SetCollisionEnabled(QueryModes[I]);
        };
        for (int32 I = 0; I < SpawnedTrashItems.Num(); ++I)
        {
            auto* Item = SpawnedTrashItems[I].Get();
            auto* Mesh = CastChecked<UStaticMeshComponent>(Item->GetRootComponent());
            // The swept component needs its normal query shapes. Its own actor is
            // ignored by Query; only completed supports and this shape are enabled.
            Mesh->SetCollisionEnabled(QueryModes[I]);
            const int32 MoundId = TowerIds[I];
            const FVector Normal = FloorPlan[I].Normal;
            const FBox Box = FloorBounds[I].TransformBy(FTransform(FloorRotations[I]));
            // Stage above the completed mound, then deposit at its actual surface.
            // Dense staggered XY projections can overlap; unplaced staging bodies
            // must not decide support or start inside already deposited shapes.
            FVector Start = Item->GetActorLocation();
            // A distant peak must not reject a lower opening at this XY. Stay
            // within the checked ceiling, and let the actual sweep reject a
            // locally blocked start instead of treating the whole pile as full.
            const double StageHeight = FMath::Min(MoundTops[MoundId] + 2.0 - Box.Min.Z,
                MoundEnvelopeHeights[MoundId] - Box.Max.Z - 0.01);
            if (StageHeight + Box.Min.Z < 0.5)
            {
                UE_LOG(MarineDemolition, Warning, TEXT("Trash mound '%s' exceeds its available height while placing item %d. Increase Pile Spread or Pile Count."), *GetName(), I);
                return false;
            }
            Start += Normal * (StageHeight - FVector::DotProduct(Start - FloorPlan[I].Location, Normal));
            Item->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
            const FVector Down = -FloorPlan[I].Normal;
            const double DropDistance = FMath::Max(10.0, FVector::DotProduct(Start - FloorPlan[I].Location, FloorPlan[I].Normal)
                + FloorBounds[I].GetSize().Size() + 4.0);
            FComponentQueryParams Query(SCENE_QUERY_STAT(MDTrashTowerStack), Item);
            TArray<FHitResult> Hits;
            GetWorld()->ComponentSweepMulti(Hits, Mesh, Start, Start + Down * DropDistance, Item->GetActorQuat(), Query);
            const FHitResult* Support = Hits.FindByPredicate([](const FHitResult& Hit) { return Hit.bBlockingHit; });
            if (!Support || Support->bStartPenetrating)
            {
                UE_LOG(MarineDemolition, Warning, TEXT("Trash mound '%s' support sweep failed: item=%d variant=%s hit=%s penetrating=%d"),
                    *GetName(), I, *Item->GetTrashVariantId().ToString(), Support ? *GetNameSafe(Support->GetActor()) : TEXT("none"),
                    Support && Support->bStartPenetrating);
                return false;
            }
            Item->SetActorLocation(Start + Down * FMath::Max(0.0, DropDistance * Support->Time - 0.5),
                false, nullptr, ETeleportType::TeleportPhysics);
            const double Top = FVector::DotProduct(Item->GetActorLocation() - FloorPlan[I].Location, Normal) + Box.Max.Z;
            if (Top > MoundEnvelopeHeights[MoundId] + 0.01)
            {
                UE_LOG(MarineDemolition, Warning, TEXT("Trash mound '%s' escaped its validated height at item %d."), *GetName(), I);
                return false;
            }
            MoundTops[MoundId] = FMath::Max(MoundTops[MoundId], Top);
            // A reused item may have slept at its previous location. Fresh placement
            // must begin settling under gravity; exact day restoration takes its own path.
            Mesh->WakeAllRigidBodies();
            Item->ForceNetUpdate();
        }
    }
    return true;
}

TArray<FName> AMD_TrashSpawnVolume::BuildEligibleTrashKindIds() const
{
	TArray<FName> EligibleTrashKindIds;
	if (AllowedTrashCategoryTags.IsEmpty())
	{
		return EligibleTrashKindIds;
	}

	for (const FName TrashKindId : UMD_TrashLibrary::GetTrashKindIds())
	{
		FMD_TrashKindRow TrashKindRow;
		if (!UMD_TrashLibrary::FindTrashKindRow(TrashKindId, TrashKindRow)
			|| !TrashKindRow.Category.IsValid()
			|| UMD_TrashLibrary::GetTrashVariantIds(TrashKindId).IsEmpty())
		{
			continue;
		}

		for (const FGameplayTag& AllowedCategoryTag : AllowedTrashCategoryTags)
		{
			if (AllowedCategoryTag.IsValid() && TrashKindRow.Category.MatchesTag(AllowedCategoryTag))
			{
				EligibleTrashKindIds.Add(TrashKindId);
				break;
			}
		}
	}

	return EligibleTrashKindIds;
}

bool AMD_TrashSpawnVolume::FindSpawnLocation(
    FRandomStream& RandomStream,
    const TArray<FVector>& OccupiedLocations,
    const TArray<FVector2D>& PolygonVertices,
    const FBox2D& PolygonBounds,
    const double TraceStartZ,
    const double TraceEndZ,
    FVector& OutLocation) const
{
    const int32 SafeMaxAttempts = FMath::Max(1, MaxPlacementAttemptsPerItem);
    const float SafeMinimumSpacing = FMath::Max(0.0f, MinimumSpacing);
    const double MinimumSpacingSquared = FMath::Square(static_cast<double>(SafeMinimumSpacing));
    FCollisionObjectQueryParams FloorObjectParams;
    FloorObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
    FloorObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams FloorQueryParams(SCENE_QUERY_STAT(MDTrashSpawnFloor), false, this);
    TArray<FHitResult> FloorHits;

    for (int32 Attempt = 0; Attempt < SafeMaxAttempts; ++Attempt)
    {
        const FVector2D CandidatePoint(
            RandomStream.FRandRange(PolygonBounds.Min.X, PolygonBounds.Max.X),
            RandomStream.FRandRange(PolygonBounds.Min.Y, PolygonBounds.Max.Y));
        if (!IsPointInsidePolygon(CandidatePoint, PolygonVertices))
        {
            continue;
        }

        const FVector CandidateWorld = PolygonBoundary->GetComponentTransform().TransformPosition(
            FVector(CandidatePoint.X, CandidatePoint.Y, 0.0));
        const FVector TraceStart(CandidateWorld.X, CandidateWorld.Y, TraceStartZ);
        const FVector TraceEnd(CandidateWorld.X, CandidateWorld.Y, TraceEndZ);
        FloorHits.Reset();
        GetWorld()->LineTraceMultiByObjectType(
            FloorHits, TraceStart, TraceEnd, FloorObjectParams, FloorQueryParams);

        bool bFoundFloor = false;
        FVector CandidateLocation = FVector::ZeroVector;
        for (const FHitResult& FloorHit : FloorHits)
        {
            if (FloorHit.bBlockingHit && FloorHit.ImpactNormal.Z >= 0.5)
            {
                CandidateLocation = FloorHit.ImpactPoint;
                bFoundFloor = true;
                break;
            }
        }
        if (!bFoundFloor)
        {
            continue;
        }

        bool bHasEnoughSpacing = true;
        if (SafeMinimumSpacing > 0.0f)
        {
            for (const FVector& OccupiedLocation : OccupiedLocations)
            {
                if (FVector::DistSquared(CandidateLocation, OccupiedLocation) < MinimumSpacingSquared)
                {
                    bHasEnoughSpacing = false;
                    break;
                }
            }
        }

        if (bHasEnoughSpacing)
        {
            OutLocation = CandidateLocation;
            return true;
        }
    }
    return false;
}

void AMD_TrashSpawnVolume::UpdateShapeVisualization()
{
    if (IsValid(PolygonBoundary))
    {
        if (!PolygonBoundary->IsClosedLoop())
        {
            PolygonBoundary->SetClosedLoop(true, true);
        }
        PolygonBoundary->SetVisibility(!UsesAutomaticFloorCells() || IsFloorBonusVolume(), true);
        PolygonBoundary->bDrawDebug = !UsesAutomaticFloorCells() || IsFloorBonusVolume();
#if WITH_EDITORONLY_DATA
        // 비율이 클수록 붉게. 비율을 쓰지 않는 볼륨은 기본 흰색
        if (FMath::IsFinite(SpawnShare) && SpawnShare > 0.0f)
        {
            const float Hue = FMath::Lerp(120.0f, 0.0f, FMath::Clamp(SpawnShare / 50.0f, 0.0f, 1.0f));
            PolygonBoundary->EditorUnselectedSplineSegmentColor = FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue / 360.0f * 255.0f), 220, 255);
        }
        else
        {
            PolygonBoundary->EditorUnselectedSplineSegmentColor = FLinearColor::White;
        }
#endif
    }
}

const TArray<FMD_TrashSpawnKindChance>& AMD_TrashSpawnVolume::GetEffectiveKindChances() const
{
    if (!TrashKindChances.IsEmpty())
    {
        return TrashKindChances;
    }
    if (const UMD_TrashSpawnProfile* Profile = UMD_TrashSpawnManager::ResolveProfile(GetWorld(), this))
    {
        return Profile->DefaultKindChances;
    }
    return TrashKindChances;
}

double AMD_TrashSpawnVolume::ComputeBoundaryArea() const
{
    TArray<FVector2D> Vertices;
    FBox2D Bounds(ForceInit);
    double MinZ = 0.0, MaxZ = 0.0;
    if (!BuildPolygonVertices(PolygonBoundary, PolygonSamplesPerSegment, Vertices, Bounds, MinZ, MaxZ))
    {
        return 0.0;
    }
    // 신발끈 공식. 스플라인 로컬 좌표라 액터 스케일을 따로 곱한다
    double TwiceArea = 0.0;
    for (int32 I = 0; I < Vertices.Num(); ++I)
    {
        const FVector2D& A = Vertices[I];
        const FVector2D& B = Vertices[(I + 1) % Vertices.Num()];
        TwiceArea += A.X * B.Y - B.X * A.Y;
    }
    const FVector Scale = PolygonBoundary->GetComponentTransform().GetScale3D();
    return FMath::Abs(TwiceArea) * 0.5 * FMath::Abs(Scale.X * Scale.Y);
}

#if WITH_EDITOR
void AMD_TrashSpawnVolume::PreviewShareDistribution()
{
    UMD_TrashSpawnManager::PreviewShareDistribution(GetWorld(), this, PreviewPlayerCount, PreviewDifficulty);
}
#endif

void AMD_TrashSpawnVolume::CompactSpawnedTrashItems()
{
	SpawnedTrashItems.RemoveAll([](const TObjectPtr<AMD_TrashItem>& SpawnedItem)
	{
		return !IsValid(SpawnedItem);
	});
}

void AMD_TrashSpawnVolume::NotifyManagerStateChanged() const
{
    if (HasAuthority() && IsValid(GetWorld()))
    {
        if (UMD_TrashSpawnManager* Manager = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
        {
            Manager->NotifyGeneratorStateChanged();
        }
    }
}
