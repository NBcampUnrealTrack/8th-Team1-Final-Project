#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CollisionQueryParams.h"
#include "MD_TrashFloorPlacementComponent.generated.h"

class UStaticMeshComponent;

struct FMD_TrashFloorPoint
{
	FVector Location = FVector::ZeroVector;
	FIntVector Cell = FIntVector::ZeroValue;
	FVector Normal = FVector::UpVector;
};

/** Server-side, map-lifetime floor cache. Advanced by the loading flow, never by an idle Tick. */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_TrashFloorPlacementComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMD_TrashFloorPlacementComponent();

    UFUNCTION(BlueprintPure, Category = "MD|Floor Placement")
    bool UsesLocalFloorSettings() const;

    UFUNCTION(BlueprintPure, Category = "MD|Floor Placement")
    bool UsesFloorPlacement() const;

	/** Reads actor/ancestor tags already supplied by the map importer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Placement",
        meta = (EditCondition = "UsesLocalFloorSettings", EditConditionHides))
	TArray<FName> FloorActorTags = { TEXT("Rhino.Entity.Type: X7_ver2_Floor") };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Placement", meta = (EditCondition = "UsesLocalFloorSettings", EditConditionHides, ClampMin = "50", Units = "cm"))
	float CellSize = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Placement", meta = (EditCondition = "UsesLocalFloorSettings", EditConditionHides, ClampMin = "50", Units = "cm"))
	float LayerHeight = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Placement", meta = (EditCondition = "UsesLocalFloorSettings", EditConditionHides, ClampMin = "25", Units = "cm"))
	float SampleSpacing = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Placement", meta = (EditCondition = "UsesLocalFloorSettings", EditConditionHides, ClampMin = "0.5", ClampMax = "1"))
	float MinimumNormalZ = 0.95f;

	/** Reaching the limit fails preparation rather than silently omitting part of the map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "MD|Floor Placement", meta = (EditCondition = "UsesLocalFloorSettings", EditConditionHides, ClampMin = "1"))
	int32 MaximumQueries = 50000;

	bool Prepare(double BudgetSeconds, bool& bReady);
    /** Budgeted subset of the prepared map cache; no additional floor traces or world scans. */
    bool PrepareSubset(const UMD_TrashFloorPlacementComponent& Source,
        TFunctionRef<bool(const FVector&)> Contains, double BudgetSeconds, bool& bReady);
	bool IsReady() const { return bPrepared; }
	const FGuid& GetLayoutId() const { return LayoutId; }
	const TMap<FIntVector, TArray<int32>>& GetCells() const { return CandidatesByCell; }
	FIntVector GetCell(const FVector& Location) const;
	bool Plan(int32 Count, const TArray<FIntVector>* RequiredCells, const TArray<FVector>& Occupied,
		float MinimumSpacing, int32 Seed, TArray<FMD_TrashFloorPoint>& OutPoints,
		TFunction<bool(int32, const FMD_TrashFloorPoint&)> Accept = nullptr,
		const TArray<float>* Radii = nullptr, const TArray<float>* OccupiedRadii = nullptr) const;
	/** Scaled mesh bounds and a full local rotation; correct the pivot against the support plane. */
	bool Fit(const FMD_TrashFloorPoint& Point, const FBox& Bounds, const FQuat& LocalRotation, FTransform& Out) const;
    /** Reserve envelopes. Mounds taper and stagger; legacy callers can still request column layers. */
    bool PlanTowers(const TArray<FIntVector>* RequiredCells, const TArray<FVector>& Occupied,
        const TArray<float>& OccupiedRadii, float MinimumSpacing, int32 Seed,
        const TArray<FBox>& Bounds, const TArray<FQuat>& Rotations, int32 MaxItems, float MaxHeight,
        TArray<FMD_TrashFloorPoint>& OutPoints, TArray<FTransform>& OutTransforms, TArray<int32>& OutTowerIds,
        int32 RequestedTowerCount = 0, int32 ItemsPerLayer = 3,
        bool bMoundLayout = false, float MoundSpread = 1.0f, TArray<float>* OutEnvelopeHeights = nullptr) const;
	bool FindOriginalCell(const FVector& Position, FIntVector& Out) const;
	void SetTransitionSources(const TArray<AActor*>& Actors);

	UPROPERTY(EditAnywhere, Category = "MD|Floor Placement", meta = (EditCondition = "UsesFloorPlacement", EditConditionHides, ClampMin = "0", Units = "cm"))
	float AccessRadius = 34.0f;

	UFUNCTION(BlueprintPure, Category = "MD|Floor Placement")
	int32 GetCandidateCount() const { return Candidates.Num(); }

	UFUNCTION(BlueprintPure, Category = "MD|Floor Placement")
	int32 GetCellCount() const { return CandidatesByCell.Num(); }

private:
	bool MatchesFloor(const AActor* Actor) const;
	bool CollectActors();
	void FinishPreparation();
	bool bStarted = false;
	bool bPrepared = false;
	bool bFailed = false;
	int32 NextActor = 0;
    int32 NextRegionCandidate = 0;
	int32 NextSurface = 0;
	int32 NextSample = 0;
	int32 QueryCount = 0;
	FGuid LayoutId;
	TArray<TWeakObjectPtr<AActor>> PendingActors;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Surfaces;
	TSet<TWeakObjectPtr<UStaticMeshComponent>> AllowedSurfaces;
	TArray<TWeakObjectPtr<AActor>> IgnoredActors;
	TArray<TWeakObjectPtr<AActor>> TransitionSources;
	FCollisionQueryParams PlacementQuery;
	FCollisionQueryParams PreparationQuery;
	TSet<FIntVector> SeenPoints;
	TArray<FMD_TrashFloorPoint> Candidates;
	TMap<FIntVector, TArray<int32>> CandidatesByCell;
};
