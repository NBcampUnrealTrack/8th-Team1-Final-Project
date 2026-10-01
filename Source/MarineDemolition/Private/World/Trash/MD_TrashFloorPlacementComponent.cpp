#include "World/Trash/MD_TrashFloorPlacementComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "Gameplay/Loading/MD_MapRegistrationSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/SecureHash.h"
#include "World/Trash/MD_TrashItem.h"
#include "World/Trash/MD_TrashSpawnVolume.h"

namespace
{
bool CellLess(const FIntVector& A, const FIntVector& B)
{
	return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z;
}

bool IsWater(const AActor* Actor)
{
	// Avoid a hard dependency on Water: its volume/surface is not a support obstacle.
	for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
	{
		if (Class->GetFName() == TEXT("WaterBody")) return true;
	}
	return false;
}

template <typename T>
void Shuffle(TArray<T>& Values, FRandomStream& Random)
{
	for (int32 I = Values.Num() - 1; I > 0; --I) Values.Swap(I, Random.RandRange(0, I));
}

struct FSpacingGrid
{
	explicit FSpacingGrid(float InSpacing, float MaximumDiameter = 0) : Spacing(FMath::Max3(1.0f, InSpacing, MaximumDiameter)), DistanceSquared(FMath::Square(FMath::Max(0.0f, InSpacing))) {}
	FIntVector Key(const FVector& P) const
	{
		return FIntVector(FMath::FloorToInt(P.X / Spacing), FMath::FloorToInt(P.Y / Spacing), FMath::FloorToInt(P.Z / Spacing));
	}
	struct FEntry { FVector Position; float Radius; };
	void Add(const FVector& P, float Radius = 0) { Buckets.FindOrAdd(Key(P)).Add({P, Radius}); }
	bool CanAdd(const FVector& P, float Radius = 0) const
	{
		const FIntVector K = Key(P);
		for (int32 X = -1; X <= 1; ++X)
		for (int32 Y = -1; Y <= 1; ++Y)
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			if (const auto* Bucket = Buckets.Find(K + FIntVector(X, Y, Z)))
				for (const FEntry& Existing : *Bucket)
					if (FVector::DistSquared(P, Existing.Position) < FMath::Max(DistanceSquared, static_cast<double>(FMath::Square(Radius + Existing.Radius)))) return false;
		}
		return true;
	}
	float Spacing;
	double DistanceSquared;
	TMap<FIntVector, TArray<FEntry>> Buckets;
};
}

UMD_TrashFloorPlacementComponent::UMD_TrashFloorPlacementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UMD_TrashFloorPlacementComponent::MatchesFloor(const AActor* Actor) const
{
	TSet<const AActor*> Seen;
	for (const AActor* Current = Actor; Current && !Seen.Contains(Current); Current = Current->GetAttachParentActor())
	{
		Seen.Add(Current);
		for (const FName Tag : FloorActorTags)
			if (!Tag.IsNone() && Current->ActorHasTag(Tag)) return true;
	}
	return false;
}

FIntVector UMD_TrashFloorPlacementComponent::GetCell(const FVector& Location) const
{
	return FIntVector(FMath::FloorToInt(Location.X / CellSize), FMath::FloorToInt(Location.Y / CellSize),
		FMath::FloorToInt(Location.Z / LayerHeight));
}

bool UMD_TrashFloorPlacementComponent::CollectActors()
{
	if (FloorActorTags.IsEmpty() || !FMath::IsFinite(CellSize) || CellSize < 50.0f
		|| !FMath::IsFinite(LayerHeight) || LayerHeight < 50.0f
		|| !FMath::IsFinite(SampleSpacing) || SampleSpacing < 25.0f
		|| !FMath::IsFinite(MinimumNormalZ) || MinimumNormalZ < 0.5f || MinimumNormalZ > 1.0f
		|| MaximumQueries <= 0) return false;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) PendingActors.Add(*It);
	PreparationQuery = FCollisionQueryParams(SCENE_QUERY_STAT(MDTrashAutoFloor), false, GetOwner());
	bStarted = true;
	return true;
}

bool UMD_TrashFloorPlacementComponent::Prepare(const double BudgetSeconds, bool& bReady)
{
	bReady = bPrepared;
	if (bFailed || !GetWorld() || GetWorld()->GetNetMode() == NM_Client) return false;
	if (bPrepared) return true;
	if (const auto* Registration = GetWorld()->GetSubsystem<UMD_MapRegistrationSubsystem>(); Registration && Registration->IsPending()) return true;
	if (!GetWorld()->GetPhysicsScene()) return false;
	const double Deadline = FPlatformTime::Seconds() + FMath::Max(0.00001, BudgetSeconds);
	if (!bStarted && !CollectActors()) { bFailed = true; return false; }
	while (NextActor < PendingActors.Num())
	{
		AActor* Actor = PendingActors[NextActor++].Get();
		if (IsValid(Actor))
		{
			if (Actor->IsA<APhysicsVolume>() || IsWater(Actor))
			{
				IgnoredActors.Add(Actor);
				PreparationQuery.AddIgnoredActor(Actor);
			}
			// Cache this once; rebuilding it every budget slice rescanned the entire map.
			else if (Actor->IsA<AMD_TrashItem>()) PreparationQuery.AddIgnoredActor(Actor);
			if (MatchesFloor(Actor))
			{
				TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
				for (UStaticMeshComponent* Mesh : Meshes)
				{
					// Instancing needs per-instance bounds; do not misidentify its combined box as a floor.
					if (Mesh->IsA<UInstancedStaticMeshComponent>()) { bFailed = true; return false; }
					if (Mesh->GetStaticMesh() && Mesh->IsRegistered() && Mesh->IsQueryCollisionEnabled())
					{
						Surfaces.Add(Mesh);
						AllowedSurfaces.Add(Mesh);
					}
				}
			}
		}
		if (FPlatformTime::Seconds() >= Deadline) return true;
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	while (NextSurface < Surfaces.Num())
	{
		const UStaticMeshComponent* Surface = Surfaces[NextSurface].Get();
		if (!IsValid(Surface)) { bFailed = true; return false; }
		const FBox Bounds = Surface->Bounds.GetBox();
		const FVector Size = Bounds.GetSize();
		const int32 NX = FMath::Max(1, FMath::CeilToInt(Size.X / SampleSpacing));
		const int32 NY = FMath::Max(1, FMath::CeilToInt(Size.Y / SampleSpacing));
		if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
			|| static_cast<int64>(NX) * NY > MaximumQueries) { bFailed = true; return false; }
		while (NextSample < NX * NY)
		{
			if (QueryCount++ >= MaximumQueries) { bFailed = true; return false; }
			const int32 IX = NextSample / NY, IY = NextSample % NY;
			++NextSample;
			// Stable sample positions across sessions; only selection of these positions is randomized.
			const double X = Bounds.Min.X + (IX + 0.5) * Size.X / NX;
			const double Y = Bounds.Min.Y + (IY + 0.5) * Size.Y / NY;
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(X, Y, Bounds.Max.Z + 100.0),
				FVector(X, Y, Bounds.Min.Z - 10.0), Objects, PreparationQuery)
				&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z >= MinimumNormalZ
				&& AllowedSurfaces.Contains(Cast<UStaticMeshComponent>(Hit.GetComponent())))
			{
				const FVector P = Hit.ImpactPoint;
				const FIntVector Key(FMath::RoundToInt(P.X / 2.0), FMath::RoundToInt(P.Y / 2.0), FMath::RoundToInt(P.Z / 2.0));
				if (!SeenPoints.Contains(Key))
				{
					SeenPoints.Add(Key);
					Candidates.Add({P, GetCell(P), Hit.ImpactNormal});
				}
			}
			if (FPlatformTime::Seconds() >= Deadline) return true;
		}
		NextSample = 0;
		++NextSurface;
	}
	if (Candidates.IsEmpty()) { bFailed = true; return false; }
	FinishPreparation();
	bReady = true;
	return true;
}

bool UMD_TrashFloorPlacementComponent::PrepareSubset(const UMD_TrashFloorPlacementComponent& Source,
    TFunctionRef<bool(const FVector&)> Contains, double BudgetSeconds, bool& bReady)
{
    bReady = bPrepared;
    if (bFailed || &Source == this || Source.GetWorld() != GetWorld() || !Source.IsReady()) return false;
    if (bPrepared) return true;
    const double Deadline = FPlatformTime::Seconds() + FMath::Max(0.00001, BudgetSeconds);
    if (!bStarted)
    {
        CellSize = Source.CellSize; LayerHeight = Source.LayerHeight; SampleSpacing = Source.SampleSpacing;
        MinimumNormalZ = Source.MinimumNormalZ; FloorActorTags = Source.FloorActorTags;
        AllowedSurfaces = Source.AllowedSurfaces; IgnoredActors = Source.IgnoredActors;
        bStarted = true;
    }
    while (NextRegionCandidate < Source.Candidates.Num())
    {
        const auto& Point = Source.Candidates[NextRegionCandidate++];
        if (Contains(Point.Location)) Candidates.Add(Point);
        if (FPlatformTime::Seconds() >= Deadline) return true;
    }
    if (Candidates.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Bonus trash volume '%s' contains no sampled floor. Check spline height, size and floor tags."), *GetOwner()->GetName());
        bFailed = true; return false;
    }
    FinishPreparation();
    bReady = true;
    return true;
}

void UMD_TrashFloorPlacementComponent::FinishPreparation()
{
	Candidates.Sort([](const FMD_TrashFloorPoint& A, const FMD_TrashFloorPoint& B)
	{
		return A.Location.X != B.Location.X ? A.Location.X < B.Location.X
			: A.Location.Y != B.Location.Y ? A.Location.Y < B.Location.Y : A.Location.Z < B.Location.Z;
	});
	FMD5 Hash;
	for (int32 I = 0; I < Candidates.Num(); ++I)
	{
		const FMD_TrashFloorPoint& Point = Candidates[I];
		CandidatesByCell.FindOrAdd(Point.Cell).Add(I);
		const int64 Quantized[] = { FMath::RoundToInt64(Point.Location.X * 10.0),
			FMath::RoundToInt64(Point.Location.Y * 10.0), FMath::RoundToInt64(Point.Location.Z * 10.0) };
		Hash.Update(reinterpret_cast<const uint8*>(Quantized), sizeof(Quantized));
	}
	uint32 Digest[4];
	Hash.Final(reinterpret_cast<uint8*>(Digest));
	LayoutId = FGuid(Digest[0], Digest[1], Digest[2], Digest[3]);
	SetTransitionSources({});
	PendingActors.Empty(); Surfaces.Empty(); SeenPoints.Empty();
	bPrepared = true;
}

bool UMD_TrashFloorPlacementComponent::Plan(const int32 Count, const TArray<FIntVector>* RequiredCells,
	const TArray<FVector>& Occupied, const float MinimumSpacing, const int32 Seed, TArray<FMD_TrashFloorPoint>& OutPoints,
	TFunction<bool(int32, const FMD_TrashFloorPoint&)> Accept, const TArray<float>* Radii, const TArray<float>* OccupiedRadii) const
{
	OutPoints.Reset();
	if (!bPrepared || Count < 0 || Count > Candidates.Num() || !FMath::IsFinite(MinimumSpacing)
		|| (RequiredCells && RequiredCells->Num() != Count) || (Radii && Radii->Num() != Count)
        || (OccupiedRadii && OccupiedRadii->Num() != Occupied.Num())) return false;
	float MaximumDiameter = 0;
	if (Radii) for (const float Radius : *Radii)
	{
		if (!FMath::IsFinite(Radius) || Radius < 0) return false;
		MaximumDiameter = FMath::Max(MaximumDiameter, Radius * 2);
	}
    if (OccupiedRadii) for (const float Radius : *OccupiedRadii)
    {
        if (!FMath::IsFinite(Radius) || Radius < 0) return false;
        MaximumDiameter = FMath::Max(MaximumDiameter, Radius * 2);
    }
	if (Count == 0) return true;
	TMap<FIntVector, TArray<int32>> Requests;
	if (RequiredCells)
	{
		for (int32 I = 0; I < Count; ++I) Requests.FindOrAdd((*RequiredCells)[I]).Add(I);
		for (const auto& Entry : Requests)
		{
			const auto* Options = CandidatesByCell.Find(Entry.Key);
			if (!Options || Options->Num() < Entry.Value.Num()) return false;
		}
	}
	for (int32 Attempt = 0; Attempt < (RequiredCells ? 16 : 1); ++Attempt)
	{
		FRandomStream Random(static_cast<int32>(static_cast<uint32>(Seed) + static_cast<uint32>(Attempt) * 2654435761u));
		FSpacingGrid Spacing(MinimumSpacing, MaximumDiameter);
		for (int32 I = 0; I < Occupied.Num(); ++I) Spacing.Add(Occupied[I], OccupiedRadii ? (*OccupiedRadii)[I] : 0);
		if (!RequiredCells)
		{
			TArray<int32> Options;
			for (int32 I = 0; I < Candidates.Num(); ++I) Options.Add(I);
			Shuffle(Options, Random);
			for (const int32 Index : Options)
			{
				const float Radius = Radii ? (*Radii)[OutPoints.Num()] : 0;
				if (Spacing.CanAdd(Candidates[Index].Location, Radius) && (!Accept || Accept(OutPoints.Num(), Candidates[Index])))
				{
					Spacing.Add(Candidates[Index].Location, Radius);
					OutPoints.Add(Candidates[Index]);
				}
				if (OutPoints.Num() == Count) return true;
			}
			break;
		}
		TArray<FIntVector> Order;
		Requests.GetKeys(Order);
		Order.Sort(CellLess);
		Shuffle(Order, Random);
		Order.StableSort([&](const FIntVector& A, const FIntVector& B)
		{
			return static_cast<double>(CandidatesByCell[A].Num()) / Requests[A].Num()
				< static_cast<double>(CandidatesByCell[B].Num()) / Requests[B].Num();
		});
		OutPoints.SetNum(Count);
		bool bSuccess = true;
		for (const FIntVector& Cell : Order)
		{
			TArray<int32> Options = CandidatesByCell[Cell];
			Shuffle(Options, Random);
			int32 Placed = 0;
			for (const int32 Index : Options)
			{
				const int32 Request = Requests[Cell][Placed];
				const float Radius = Radii ? (*Radii)[Request] : 0;
				if (Spacing.CanAdd(Candidates[Index].Location, Radius) && (!Accept || Accept(Request, Candidates[Index])))
				{
					Spacing.Add(Candidates[Index].Location, Radius);
					OutPoints[Request] = Candidates[Index];
					++Placed;
				}
				if (Placed == Requests[Cell].Num()) break;
			}
			if (Placed != Requests[Cell].Num()) { bSuccess = false; break; }
		}
		if (bSuccess) return true;
	}
	UE_LOG(LogTemp, Display, TEXT("Floor plan failed: requested=%d selected=%d spacing=%.1f"),
		Count, OutPoints.Num(), MinimumSpacing);
	OutPoints.Reset();
	return false;
}

bool UMD_TrashFloorPlacementComponent::UsesLocalFloorSettings() const
{
    const auto* Volume = Cast<AMD_TrashSpawnVolume>(GetOwner());
    return !Volume || (Volume->UsesAutomaticFloorCells() && !Volume->IsFloorBonusVolume());
}

bool UMD_TrashFloorPlacementComponent::UsesFloorPlacement() const
{
    const auto* Volume = Cast<AMD_TrashSpawnVolume>(GetOwner());
    return !Volume || Volume->UsesAutomaticFloorCells();
}

bool UMD_TrashFloorPlacementComponent::PlanTowers(const TArray<FIntVector>* RequiredCells,
    const TArray<FVector>& Occupied, const TArray<float>& OccupiedRadii, float MinimumSpacing, int32 Seed,
    const TArray<FBox>& Bounds, const TArray<FQuat>& Rotations, int32 MaxItems, float MaxHeight,
    TArray<FMD_TrashFloorPoint>& OutPoints, TArray<FTransform>& OutTransforms, TArray<int32>& OutTowerIds,
    int32 RequestedTowerCount, int32 ItemsPerLayer, bool bMoundLayout, float MoundSpread,
    TArray<float>* OutEnvelopeHeights) const
{
    OutPoints.Reset(); OutTransforms.Reset(); OutTowerIds.Reset();
    if (OutEnvelopeHeights) OutEnvelopeHeights->Reset();
    const int32 Count = Bounds.Num();
    const bool bFixedCount = RequestedTowerCount > 0;
    const bool bGroupedLayout = bFixedCount || bMoundLayout;
    if (!bPrepared || Rotations.Num() != Count || (RequiredCells && RequiredCells->Num() != Count)
        || RequestedTowerCount < 0
        || (bMoundLayout && (!FMath::IsFinite(MoundSpread) || MoundSpread < 0.5f || MoundSpread > 3.0f))
        || (!bMoundLayout && ((bFixedCount && ItemsPerLayer < 1)
            || (!bFixedCount && (MaxItems < 2 || MaxItems > 32 || !FMath::IsFinite(MaxHeight) || MaxHeight < 10 || MaxHeight > 500))))) return false;
    if (Count == 0) return true;
    struct FColumn
    {
        TArray<int32> Items;
        TArray<FVector> Offsets;
        double Height = 0, X = 0, Y = 0;
        FIntVector Cell = FIntVector::ZeroValue;
    };
    TArray<FColumn> Columns;
    TArray<FBox> Rotated;
    TArray<int32> Order;
    for (int32 I = 0; I < Count; ++I)
    {
        if (!Bounds[I].IsValid || Bounds[I].Min.ContainsNaN() || Bounds[I].Max.ContainsNaN()
            || Rotations[I].ContainsNaN() || !Rotations[I].IsNormalized()) return false;
        Rotated.Add(Bounds[I].TransformBy(FTransform(Rotations[I])));
        if (!bGroupedLayout && Rotated.Last().GetSize().Z + 2.0 > MaxHeight) return false;
        Order.Add(I);
    }
    // Group restored items by their original ground cell, never their current stacked height.
    if (RequiredCells) Order.StableSort([&](int32 A, int32 B) { return CellLess((*RequiredCells)[A], (*RequiredCells)[B]); });
    if (bGroupedLayout)
    {
        struct FCellGroup
        {
            FIntVector Cell = FIntVector::ZeroValue;
            int32 First = 0, Count = 0, Capacity = 0;
        };
        TArray<FCellGroup> Groups;
        for (int32 J = 0; J < Order.Num(); ++J)
        {
            const FIntVector Cell = RequiredCells ? (*RequiredCells)[Order[J]] : FIntVector::ZeroValue;
            if (Groups.IsEmpty() || Groups.Last().Cell != Cell)
            {
                auto& Group = Groups.AddDefaulted_GetRef();
                Group.Cell = Cell;
                Group.First = J;
            }
            ++Groups.Last().Count;
        }
        // Automatic mounds respect saved clean areas without an obsolete per-tower
        // size limit: one fresh mound, or one anchor in each nonempty original cell.
        const int32 Target = bMoundLayout && !bFixedCount ? Groups.Num() : FMath::Min(RequestedTowerCount, Count);
        // Saved items must stay in their original ground cells. Never make an extra
        // tower or refill a cleaned cell just to accommodate a changed tower count.
        if (Target < Groups.Num())
        {
            UE_LOG(LogTemp, Warning, TEXT("Requested %d trash piles, but the save has %d nonempty floor cells. Start a new progression or increase Pile Count."),
                Target, Groups.Num());
            return false;
        }
        int64 ExtraCapacity = 0;
        for (auto& Group : Groups)
        {
            const auto* Options = RequiredCells ? CandidatesByCell.Find(Group.Cell) : nullptr;
            if (RequiredCells && !Options) return false;
            Group.Capacity = FMath::Min(Group.Count, Options ? Options->Num() : Candidates.Num());
            if (Group.Capacity == 0) return false;
            ExtraCapacity += Group.Capacity - 1;
        }
        int32 ExtraTowers = Target - Groups.Num();
        if (ExtraTowers > ExtraCapacity) return false;
        // Allocate extra anchors proportionally in one pass, bounded by each cell's
        // item and candidate counts. This stays linear even for many saved cells.
        for (const auto& Group : Groups)
        {
            const int32 Available = Group.Capacity - 1;
            const int32 Extra = ExtraCapacity > 0 ? static_cast<int32>(static_cast<int64>(ExtraTowers) * Available / ExtraCapacity) : 0;
            const int32 Towers = 1 + Extra;
            ExtraTowers -= Extra;
            ExtraCapacity -= Available;
            int32 First = Group.First;
            for (int32 Tower = 0; Tower < Towers; ++Tower)
            {
                auto& Column = Columns.AddDefaulted_GetRef();
                Column.Cell = Group.Cell;
                const int32 Members = Group.Count / Towers + (Tower < Group.Count % Towers ? 1 : 0);
                for (int32 J = 0; J < Members; ++J)
                {
                    const int32 I = Order[First++];
                    Column.Items.Add(I);
                    Column.X = FMath::Max(Column.X, Rotated[I].GetExtent().X);
                    Column.Y = FMath::Max(Column.Y, Rotated[I].GetExtent().Y);
                }
            }
        }
    }
    else for (int32 I : Order)
    {
        const double Height = Rotated[I].GetSize().Z;
        if (Columns.IsEmpty() || Columns.Last().Items.Num() >= MaxItems
            || Columns.Last().Height + Height + 2.5 > MaxHeight
            || (RequiredCells && Columns.Last().Cell != (*RequiredCells)[I]))
        {
            auto& Column = Columns.AddDefaulted_GetRef();
            if (RequiredCells) Column.Cell = (*RequiredCells)[I];
        }
        auto& Column = Columns.Last();
        if (!Column.Items.IsEmpty()) Column.Height += 0.5;
        Column.Height += Height;
        Column.X = FMath::Max(Column.X, Rotated[I].GetExtent().X);
        Column.Y = FMath::Max(Column.Y, Rotated[I].GetExtent().Y);
        Column.Items.Add(I);
    }
    TArray<FIntVector> Cells;
    TArray<float> Radii;
    TArray<FBox> ColumnBounds;
    FRandomStream PileRandom(Seed ^ 0x31AB72E5);
    for (auto& Column : Columns)
    {
        if (bMoundLayout)
        {
            double MeanFootprint = 0, MeanHeight = 0, LargestHeight = 0;
            for (const int32 I : Column.Items)
            {
                const FVector Size = Rotated[I].GetSize();
                MeanFootprint += FMath::Sqrt(Size.X * Size.Y);
                MeanHeight += Size.Z;
                LargestHeight = FMath::Max(LargestHeight, Size.Z);
            }
            MeanFootprint /= Column.Items.Num();
            MeanHeight /= Column.Items.Num();
            const double Pitch = (MeanFootprint * 0.70 + 1.0) * MoundSpread;
            const int32 Layers = FMath::Max(1, FMath::RoundToInt(FMath::Pow(static_cast<double>(Column.Items.Num()), 1.0 / 3.0)));
            // The conservative envelope is proportional to mound thickness, not
            // the sum of all item heights. Actual deposition must remain inside it.
            Column.Height = (MeanHeight * Layers * 3.0 + LargestHeight * 2.0 + 4.0)
                / FMath::Min(1.0f, FMath::Square(MoundSpread));
            const FQuat LayoutYaw(FVector::UpVector, PileRandom.FRandRange(0, 2.0f * PI));
            int32 Remaining = Column.Items.Num();
            int64 WeightLeft = static_cast<int64>(Layers) * (Layers + 1) * (2 * Layers + 1) / 6;
            TArray<FVector2D> Slots;
            const FIntPoint Directions[] = {{1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}, {1,-1}};
            for (int32 Tier = 0; Tier < Layers && Remaining > 0; ++Tier)
            {
                const int64 Weight = FMath::Square(static_cast<int64>(Layers - Tier));
                const int32 TierCount = static_cast<int32>((static_cast<int64>(Remaining) * Weight + WeightLeft - 1) / WeightLeft);
                if (Slots.IsEmpty())
                {
                    Slots.Reserve(TierCount);
                    Slots.Add(FVector2D::ZeroVector);
                    // Grow concentric hex rings in linear time. Upper layers use
                    // fewer central slots, avoiding repeated narrow vertical columns.
                    for (int32 Ring = 1; Slots.Num() < TierCount; ++Ring)
                    {
                        FIntPoint Hex(0, -Ring);
                        for (const auto& Direction : Directions)
                        for (int32 Step = 0; Step < Ring && Slots.Num() < TierCount; ++Step)
                        {
                            Slots.Add(FVector2D(Hex.X + Hex.Y * 0.5, Hex.Y * FMath::Sqrt(3.0) * 0.5) * Pitch);
                            Hex += Direction;
                        }
                    }
                }
                FVector2D Center = FVector2D::ZeroVector;
                for (int32 J = 0; J < TierCount; ++J) Center += Slots[J];
                Center /= TierCount;
                const FVector2D Stagger = Tier % 2 ? FVector2D(Pitch * 0.5, Pitch * FMath::Sqrt(3.0) / 6.0) : FVector2D::ZeroVector;
                for (int32 J = 0; J < TierCount; ++J)
                {
                    const FVector2D Jitter(PileRandom.FRandRange(-0.08f, 0.08f) * Pitch,
                        PileRandom.FRandRange(-0.08f, 0.08f) * Pitch);
                    const FVector2D XY = Slots[J] - Center + Stagger + Jitter;
                    Column.Offsets.Add(LayoutYaw.RotateVector(FVector(XY.X, XY.Y, 0)));
                }
                Remaining -= TierCount;
                WeightLeft -= Weight;
            }
            Cells.Add(Column.Cell);
            continue;
        }
        if (bFixedCount)
        {
            // Constant-width layers let one tower grow with its population. Keep
            // rotated boxes apart initially; real shape sweeps compact them below.
            const double Spacing = FMath::Sqrt(Column.X * Column.X + Column.Y * Column.Y) * 2.4 + 2.0;
            const FQuat LayoutYaw(FVector::UpVector, PileRandom.FRandRange(0, 2.0f * PI));
            for (int32 First = 0; First < Column.Items.Num();)
            {
                const int32 TierCount = FMath::Min(ItemsPerLayer, Column.Items.Num() - First);
                const int32 Rows = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TierCount)));
                const int32 ColumnsPerRow = FMath::DivideAndRoundUp(TierCount, Rows);
                double TierHeight = 0;
                for (int32 J = 0; J < TierCount; ++J)
                {
                    const int32 Row = J / ColumnsPerRow;
                    const int32 InRow = FMath::Min(ColumnsPerRow, TierCount - Row * ColumnsPerRow);
                    const FVector Grid((J % ColumnsPerRow - (InRow - 1) * 0.5) * Spacing,
                        (Row - (Rows - 1) * 0.5) * Spacing, 0);
                    const double JitterRange = TierCount > 1 ? Spacing * 0.04 : 0;
                    const FVector Jitter(PileRandom.FRandRange(-JitterRange, JitterRange),
                        PileRandom.FRandRange(-JitterRange, JitterRange), 0);
                    Column.Offsets.Add(LayoutYaw.RotateVector(Grid + Jitter) + FVector(0, 0, Column.Height));
                    TierHeight = FMath::Max(TierHeight, Rotated[Column.Items[First + J]].GetSize().Z);
                }
                Column.Height += TierHeight;
                First += TierCount;
                if (First < Column.Items.Num()) Column.Height += 0.5;
            }
            Cells.Add(Column.Cell);
            continue;
        }
        // A broad lower tier gives ordinary rigid bodies room to settle into a pile.
        // Eight items use 4+3+1 tiers. Full item rotations stay independently random.
        // Keep the conservative vertical envelope while the real-shape sweeps below
        // pack the lower tiers against the floor and the upper tiers against trash.
        const double Spacing = FMath::Max(Column.X, Column.Y) * 1.6 + 2.0;
        const FQuat LayoutYaw(FVector::UpVector, PileRandom.FRandRange(0, 2.0f * PI));
        int32 Remaining = Column.Items.Num();
        int32 TierCapacity = FMath::Max(1, FMath::DivideAndRoundUp(Remaining, 2));
        int32 Tier = 0;
        while (Remaining > 0)
        {
            const int32 TierCount = FMath::Min(TierCapacity, Remaining);
            const int32 Rows = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TierCount)));
            const int32 ColumnsPerRow = FMath::DivideAndRoundUp(TierCount, Rows);
            const double TierSpacing = Spacing * FMath::Pow(0.78, Tier);
            for (int32 J = 0; J < TierCount; ++J)
            {
                const int32 Row = J / ColumnsPerRow;
                const int32 InRow = FMath::Min(ColumnsPerRow, TierCount - Row * ColumnsPerRow);
                const FVector Grid((J % ColumnsPerRow - (InRow - 1) * 0.5) * TierSpacing,
                    (Row - (Rows - 1) * 0.5) * TierSpacing, 0);
                const FVector Jitter(PileRandom.FRandRange(-0.08f, 0.08f) * Spacing,
                    PileRandom.FRandRange(-0.08f, 0.08f) * Spacing, 0);
                const FVector Offset = LayoutYaw.RotateVector(Grid + Jitter);
                Column.Offsets.Add(Offset);
            }
            Remaining -= TierCount;
            TierCapacity = FMath::Max(1, TierCapacity - 1);
            ++Tier;
        }
        Cells.Add(Column.Cell);
    }
    TArray<FTransform> Anchors;
    TArray<FMD_TrashFloorPoint> FloorPoints;
    Anchors.SetNum(Columns.Num());
    bool bPlanned = false;
    double Spread = 1.0;
    // Old count ledgers can contain narrow ground cells that fitted a vertical
    // stack. Prefer the broad base, then reduce its spread without losing items,
    // changing their original cell, rotation, scale or ordinary physics.
    for (const double TrialSpread : {1.0, 0.75, 0.5, 0.25, 0.0})
    {
        // Fixed layers share a height. Shrinking their safe XY spacing could make
        // members penetrate one another; fail the requested layout instead.
        if (bGroupedLayout && TrialSpread < 1.0) break;
        ColumnBounds.Reset(); Radii.Reset();
        for (const auto& Column : Columns)
        {
            double X = 0, Y = 0;
            for (int32 J = 0; J < Column.Items.Num(); ++J)
            {
                const FVector Offset = Column.Offsets[J] * TrialSpread;
                const FVector Extent = Rotated[Column.Items[J]].GetExtent();
                X = FMath::Max(X, FMath::Abs(Offset.X) + Extent.X);
                Y = FMath::Max(Y, FMath::Abs(Offset.Y) + Extent.Y);
            }
            ColumnBounds.Add(FBox(FVector(-X, -Y, 0), FVector(X, Y, Column.Height)));
            Radii.Add(static_cast<float>(FMath::Sqrt(X*X + Y*Y)
                + Column.Height * FMath::Sqrt(1.0 - FMath::Square(MinimumNormalZ)) + 2.0));
        }
        if (Plan(Columns.Num(), RequiredCells ? &Cells : nullptr, Occupied, MinimumSpacing, Seed, FloorPoints,
            [&](int32 I, const FMD_TrashFloorPoint& Point) { return Fit(Point, ColumnBounds[I], FQuat::Identity, Anchors[I]); },
            &Radii, &OccupiedRadii))
        {
            Spread = TrialSpread; bPlanned = true; break;
        }
    }
    if (!bPlanned) return false;
    OutPoints.SetNum(Count); OutTransforms.SetNum(Count); OutTowerIds.SetNum(Count);
    if (OutEnvelopeHeights)
    {
        for (const auto& Column : Columns) OutEnvelopeHeights->Add(static_cast<float>(Column.Height + 2.0));
    }
    for (int32 ColumnId = 0; ColumnId < Columns.Num(); ++ColumnId)
    {
        double Bottom = 0;
        for (int32 J = 0; J < Columns[ColumnId].Items.Num(); ++J)
        {
            const int32 I = Columns[ColumnId].Items[J];
            const FBox& Box = Rotated[I];
            const FVector PlannedOffset = Columns[ColumnId].Offsets[J];
            const FVector Offset(PlannedOffset.X * Spread - Box.GetCenter().X, PlannedOffset.Y * Spread - Box.GetCenter().Y,
                (bFixedCount && !bMoundLayout ? PlannedOffset.Z : Bottom) - Box.Min.Z);
            OutPoints[I] = FloorPoints[ColumnId];
            OutTransforms[I] = FTransform(Anchors[ColumnId].GetRotation() * Rotations[I], Anchors[ColumnId].TransformPosition(Offset));
            OutTowerIds[I] = ColumnId;
            Bottom += Box.GetSize().Z + 0.5;
        }
    }
    return true;
}

void UMD_TrashFloorPlacementComponent::SetTransitionSources(const TArray<AActor*>& Actors)
{
	TransitionSources.Reset();
	for (AActor* Actor : Actors) TransitionSources.Add(Actor);
	PlacementQuery = FCollisionQueryParams(SCENE_QUERY_STAT(MDTrashFloorFootprint), false, GetOwner());
	for (const auto& Actor : IgnoredActors) if (Actor.IsValid()) PlacementQuery.AddIgnoredActor(Actor.Get());
	for (const auto& Actor : TransitionSources) if (Actor.IsValid()) PlacementQuery.AddIgnoredActor(Actor.Get());
}

bool UMD_TrashFloorPlacementComponent::FindOriginalCell(const FVector& Position, FIntVector& Out) const
{
	// Bounded lookup prevents moving an old item to a different deck or distant clean room.
	double Best = FMath::Square(150.0);
	bool Found = false;
	for (const auto& Point : Candidates)
	{
		if (FMath::Abs(Point.Location.Z - Position.Z) > 100.0) continue;
		const double Distance = FVector::DistSquared(Point.Location, Position);
		if (Distance < Best) { Best = Distance; Out = Point.Cell; Found = true; }
	}
	return Found;
}

bool UMD_TrashFloorPlacementComponent::Fit(const FMD_TrashFloorPoint& Point, const FBox& Bounds,
	const FQuat& LocalRotation, FTransform& Out) const
{
	if (!bPrepared || !Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
		|| LocalRotation.ContainsNaN() || !LocalRotation.IsNormalized()
		|| !FMath::IsFinite(AccessRadius) || AccessRadius < 0) return false;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	const FCollisionQueryParams& Query = PlacementQuery;
	const FQuat SurfaceRotation = FQuat::FindBetweenNormals(FVector::UpVector, Point.Normal);
	const FQuat Rotation = SurfaceRotation * LocalRotation;
	const FVector Extent = Bounds.GetExtent();
	const FBox RotatedBounds = Bounds.TransformBy(FTransform(LocalRotation));
	const FVector Footprint = RotatedBounds.GetExtent();
	const FVector Bottom(RotatedBounds.GetCenter().X, RotatedBounds.GetCenter().Y, RotatedBounds.Min.Z);
	Out = FTransform(Rotation, Point.Location + Point.Normal * 2.0 - SurfaceRotation.RotateVector(Bottom));
	// Nine probes cover center, edges and corners. All must hit the same local support plane.
	for (int32 X = -1; X <= 1; ++X)
	for (int32 Y = -1; Y <= 1; ++Y)
	{
		const FVector P = Point.Location + SurfaceRotation.RotateVector(FVector(X * (Footprint.X + 1.0), Y * (Footprint.Y + 1.0), 0));
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, P + Point.Normal * 6.0, P - Point.Normal * 6.0, Objects, Query)
			|| Hit.bStartPenetrating || !AllowedSurfaces.Contains(Cast<UStaticMeshComponent>(Hit.GetComponent()))
			|| Hit.ImpactNormal.Z < MinimumNormalZ || FVector::DotProduct(Hit.ImpactNormal, Point.Normal) < 0.99
			|| FVector::DistSquared(Hit.ImpactPoint, P) > 4.0) { return false; }
	}
	// Keep the bottom above its supporting plane while padding the sides/top against furniture.
	if (GetWorld()->OverlapAnyTestByObjectType(Out.TransformPosition(Bounds.GetCenter()) + Point.Normal * 0.5,
		Rotation, Objects, FCollisionShape::MakeBox(Extent + FVector(1)), Query)) { return false; }
	if (AccessRadius == 0) return true;
	const double Offset = FMath::Max(80.0, Footprint.Size2D() + AccessRadius + 5.0);
	for (const FVector Direction : {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector})
	{
		const FVector Near = Point.Location + Direction * Offset;
		FHitResult FloorHit;
		if (!GetWorld()->LineTraceSingleByObjectType(FloorHit, Near + FVector(0, 0, 40), Near - FVector(0, 0, 40), Objects, Query)
			|| FloorHit.bStartPenetrating || !AllowedSurfaces.Contains(Cast<UStaticMeshComponent>(FloorHit.GetComponent()))
			|| FloorHit.ImpactNormal.Z < MinimumNormalZ) continue;
		const FVector Stand = FloorHit.ImpactPoint + FVector(0, 0, 92);
		if (GetWorld()->OverlapAnyTestByObjectType(Stand, FQuat::Identity, Objects,
			FCollisionShape::MakeCapsule(AccessRadius, FMath::Max(88.0f, AccessRadius)), Query)) continue;
		FHitResult Obstacle;
		if (!GetWorld()->LineTraceSingleByObjectType(Obstacle, Stand, Point.Location + FVector(0, 0, 20), Objects, Query)) return true;
	}
	return false;
}
