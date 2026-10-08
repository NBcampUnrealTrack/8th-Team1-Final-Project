#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Math/Box2D.h"
#include "Templates/SubclassOf.h"
#include "Types/Trash/MD_TrashSpawnState.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TrashSpawnVolume.generated.h"

class AMD_TrashItem;
class USceneComponent;
class USplineComponent;
class UMD_TrashFloorPlacementComponent;
class UMD_TrashSpawnProfile;
struct FMD_TrashGeneratorState;

struct FMD_TrashFloorAppearance
{
    FName Variant;
    FVector Scale = FVector::OneVector;
    int32 Weight = 1;
};

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashSpawnVariantChance
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
    FName VariantId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
        meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0"))
    float ProbabilityPercent = 0.0f;
};

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashSpawnKindChance
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn", meta = (GetOptions = "MarineDemolition.MD_TrashLibrary.GetTrashKindIds"))
    FName TrashKindId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
        meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0"))
    float ProbabilityPercent = 0.0f;

    /** 비어 있으면 종류표의 첫 번째 기본 Variant를 사용한다. 설정한 경우 확률 합계는 100%. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
        meta = (TitleProperty = "VariantId"))
    TArray<FMD_TrashSpawnVariantChance> VariantChances;
};

/**
 * 스플라인으로 지정한 영역의 바닥에 쓰레기를 무작위 배치한다.
 * 실제 생성은 서버에서만 수행하며, 생성된 AMD_TrashItem이 종류와 이동을 복제한다.
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_TrashSpawnVolume : public AActor
{
	GENERATED_BODY()

public:
	AMD_TrashSpawnVolume();

	/** 현재 쓰레기를 유지한 채 SpawnCount개를 추가한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Spawn")
	int32 SpawnTrash();

	/** 이 볼륨이 생성했고 아직 살아 있는 쓰레기를 제거한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Spawn")
	void ClearSpawnedTrash();

	/** 기존 생성물을 제거한 뒤 SpawnCount개를 새로 배치한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Spawn")
	int32 RegenerateTrash();

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 GetSpawnedTrashCount() const;

    bool CanRestoreState(const FMD_TrashGeneratorState& State) const;
    bool RestoreState(const FMD_TrashGeneratorState& State);
    bool InitializeFresh();
    bool BuildEmptyState(FMD_TrashGeneratorState& OutState) const;
    /** Plan the entire transition before relocating any original actor. */
    bool ConvertAuthoredItems(const TArray<AMD_TrashItem*>& Items, const TArray<FVector>* OriginalPositions);
    bool CanConsumeTrash(FName TrashKindId, const FIntVector* FloorCell = nullptr) const;
    bool NotifyTrashConsumed(FName TrashKindId, const FIntVector* FloorCell = nullptr);
    bool PrepareSpawnLocations(double BudgetSeconds, bool& bReady);

    UFUNCTION(BlueprintPure, Category = "MD|Floor Trash")
    bool UsesAutomaticFloorCells() const { return bUseAutomaticFloorCells || bAddToFloorGenerator; }

    UFUNCTION(BlueprintPure, Category = "MD|Floor Trash")
    bool IsFloorBonusVolume() const { return bAddToFloorGenerator && !bUseAutomaticFloorCells; }

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    bool UsesIndependentSplinePlacement() const { return !UsesAutomaticFloorCells(); }

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    bool UsesSplineBoundary() const { return !bUseAutomaticFloorCells; }

    UFUNCTION(BlueprintPure, Category = "MD|Floor Trash|Tower")
    bool UsesTowerPlacement() const { return IsFloorBonusVolume() && bStackAsTowers; }

    UFUNCTION(BlueprintPure, Category = "MD|Floor Trash")
    UMD_TrashFloorPlacementComponent* GetFloorPlacement() const { return FloorPlacement; }

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    int32 GetRemainingTrashCount() const;

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    int32 GetRemainingKindCount(FName TrashKindId) const;

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    bool IsStateInitialized() const { return bStateInitialized; }

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    float GetSpawnShare() const { return SpawnShare; }

    // 이 볼륨에서 우선 사용할 총량 프로필. 전체 생성기에 두면 현장 프로필보다 앞선다
    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    UMD_TrashSpawnProfile* GetTrashSpawnProfileOverride() const { return TrashSpawnProfileOverride; }

    // 볼륨 확률표가 비어 있으면 활성 프로필의 기본 확률표를 쓴다
    const TArray<FMD_TrashSpawnKindChance>& GetEffectiveKindChances() const;

    // 스플라인 다각형의 바닥 면적(cm²). 비율 볼륨의 수용량 추정에 쓴다
    double ComputeBoundaryArea() const;

#if WITH_EDITOR
    // PIE 없이 현재 레벨의 비율 볼륨에 총량을 나눠 보고 각 볼륨의 Preview 필드에 기록한다
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "MD|Trash Spawn|Preview", meta = (DisplayName = "분배 미리보기"))
    void PreviewShareDistribution();
#endif

    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    int32 GetSpawnCount() const { return SpawnCount; }

    // 비율 볼륨이 공간 부족으로 SpawnCount보다 적게 놓았을 때 그 차이. 매니저가 다른 볼륨으로 넘긴다
    UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
    int32 GetLastSpawnShortfall() const { return LastSpawnShortfall; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Add SpawnCount inside this spline using the map generator's floor cells and placement rules.
     * Requires one whole-map floor generator. Existing volume saves require a new run. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Trash",
        meta = (EditCondition = "!bUseAutomaticFloorCells", EditConditionHides, DisplayName = "Add To Floor Generator"))
    bool bAddToFloorGenerator = false;

    /** Deposit a dense, tapered mound. Each trash item keeps its own physics. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Trash|Tower",
        meta = (EditCondition = "bAddToFloorGenerator && !bUseAutomaticFloorCells", EditConditionHides, DisplayName = "Pile Up Trash"))
    bool bStackAsTowers = false;

    /** Positive values choose the mound count. 0 uses one fresh mound, or one per remaining saved cell. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Trash|Tower",
        meta = (EditCondition = "UsesTowerPlacement", EditConditionHides, DisplayName = "Pile Count", ClampMin = "0", UIMin = "0", UIMax = "20"))
    int32 TowerCount = 0;

    /** Larger values spread the mound out; smaller values make a denser initial heap. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Floor Trash|Tower",
        meta = (EditCondition = "UsesTowerPlacement", EditConditionHides, ClampMin = "0.5", ClampMax = "3", UIMin = "0.7", UIMax = "1.5"))
    float PileSpread = 1.0f;

    // Retain serialized names for existing maps and Blueprint defaults. These
    // retired designer options do not control the mound placement path.
    UPROPERTY(BlueprintReadOnly, Category = "MD|Floor Trash|Tower")
    int32 ItemsPerLayer = 3;

    UPROPERTY(BlueprintReadOnly, Category = "MD|Floor Trash|Tower")
    int32 MaxItemsPerTower = 8;

    UPROPERTY(BlueprintReadOnly, Category = "MD|Floor Trash|Tower")
    float MaxTowerHeight = 200.0f;

    /** One generator can cover the map; no polygon boundaries are required in this mode. */
    UPROPERTY(BlueprintReadOnly, Category = "MD|Floor Trash")
    bool bUseAutomaticFloorCells = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Floor Trash")
    TObjectPtr<UMD_TrashFloorPlacementComponent> FloorPlacement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	TObjectPtr<USceneComponent> SceneRoot;

	/** 레벨 인스턴스의 스플라인 점으로 생성 영역의 경계를 만든다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape")
	TObjectPtr<USplineComponent> PolygonBoundary;

    /** 스플라인의 최고·최저 점에서 바닥을 찾을 수직 여유 거리. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape",
        meta = (EditCondition = "UsesSplineBoundary", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ShapeHalfHeight = 100.0f;

	/** 곡선 경계를 다각형으로 근사할 때 세그먼트마다 사용할 샘플 수. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "MD|Trash Spawn|Shape",
        meta = (EditCondition = "UsesSplineBoundary", EditConditionHides, ClampMin = "1", ClampMax = "32", UIMin = "1", UIMax = "32"))
	int32 PolygonSamplesPerSegment = 8;

	/** 생성할 쓰레기 액터. 기본값은 AMD_TrashItem이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	TSubclassOf<AMD_TrashItem> TrashItemClass;

	/**
	 * 허용할 종류 카테고리 배열. 종류 행의 Category가 항목과 같거나 그 하위 태그면 허용된다.
	 * 예: Score.Sorting 하나는 전체, Score.Sorting.Metal 하나는 금속 종류만 허용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
		meta = (Categories = "Score.Sorting"))
	TArray<FGameplayTag> AllowedTrashCategoryTags;

    /** 비어 있으면 허용 종류를 균등 선택하고, 각 종류의 유효한 모든 Variant도 균등 선택한다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Kinds",
        meta = (TitleProperty = "TrashKindId"))
    TArray<FMD_TrashSpawnKindChance> TrashKindChances;

	// 0보다 크면 맵 전체 총량(기준 개수 × 인원 배율 × 난이도 배율)을 다른 볼륨과 이 가중치 비율로 나눠 SpawnCount를 덮어쓴다. 0이면 SpawnCount를 그대로 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "100.0", DisplayPriority = "1"))
	float SpawnShare = 0.0f;

	// 고정 개수. SpawnShare가 0일 때만 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
		meta = (ClampMin = "0", UIMin = "0", EditCondition = "SpawnShare <= 0.0", DisplayPriority = "2"))
	int32 SpawnCount = 20;

	// 총량 규칙 재정의. 전체 생성기에 설정하면 현장(Site) 프로필과 Project Settings보다 우선한다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	TObjectPtr<UMD_TrashSpawnProfile> TrashSpawnProfileOverride;

	// 미리보기에 쓸 인원수
	UPROPERTY(EditAnywhere, Category = "MD|Trash Spawn|Preview", meta = (ClampMin = "1", ClampMax = "4", UIMin = "1", UIMax = "4"))
	int32 PreviewPlayerCount = 4;

	// 미리보기에 쓸 계약 난이도
	UPROPERTY(EditAnywhere, Category = "MD|Trash Spawn|Preview")
	EMD_Difficulty PreviewDifficulty = EMD_Difficulty::Standard;

	// 마지막 미리보기에서 이 볼륨에 배정된 개수
	UPROPERTY(VisibleAnywhere, Transient, Category = "MD|Trash Spawn|Preview")
	int32 PreviewAssignedCount = 0;

	// 바닥 면적과 아이템 크기·간격으로 추정한 이 볼륨의 최대 수용량. 실제 배치는 이보다 적을 수 있다
	UPROPERTY(VisibleAnywhere, Transient, Category = "MD|Trash Spawn|Preview")
	int32 PreviewEstimatedCapacity = 0;

	// 마지막 미리보기의 맵 전체 총량(전체 생성기에서 확인)
	UPROPERTY(VisibleAnywhere, Transient, Category = "MD|Trash Spawn|Preview")
	int32 PreviewTotalCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	bool bSpawnOnBeginPlay = true;

	/** 켜면 SpawnTrash를 호출할 때마다 새 시드를 만든다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Random",
        meta = (EditCondition = "UsesIndependentSplinePlacement", EditConditionHides))
	bool bRandomizeSeedEachSpawn = false;

	/** 같은 볼륨 설정에서 동일한 배치를 재현할 고정 시드. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Random",
		meta = (EditCondition = "UsesIndependentSplinePlacement && !bRandomizeSeedEachSpawn", EditConditionHides))
	int32 SpawnSeed = 1337;

	/** 생성된 쓰레기 중심 사이의 최소 거리. 0이면 간격 검사를 하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Placement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MinimumSpacing = 75.0f;

	/** 한 개의 위치를 찾기 위해 다시 뽑을 최대 횟수. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Placement",
		meta = (EditCondition = "UsesIndependentSplinePlacement", EditConditionHides, ClampMin = "1", UIMin = "1"))
	int32 MaxPlacementAttemptsPerItem = 16;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MD|Trash Spawn|Random",
        meta = (EditCondition = "UsesIndependentSplinePlacement", EditConditionHides))
	int32 LastUsedSeed = 0;

private:
	friend class UMD_TrashSpawnManager;
	// Runtime adapter rebuilds these from the unchanged map's authored kind/variant configuration.
	TMap<FName, TArray<FMD_TrashFloorAppearance>> FloorAppearances;
	TArray<FName> BuildEligibleTrashKindIds() const;
    bool BuildTrashKindChances(TArray<FMD_TrashSpawnKindChance>& OutChances) const;
    bool PlaceTrashKinds(const TArray<FName>& KindIds, int32 PlacementSeed,
        const TArray<FIntVector>* RequiredCells = nullptr, const TArray<AMD_TrashItem*>* ReusedItems = nullptr);
    void InitializeFloorLedger();
    bool FindSpawnLocation(FRandomStream& RandomStream, const TArray<FVector>& OccupiedLocations,
        const TArray<FVector2D>& PolygonVertices, const FBox2D& PolygonBounds,
        double TraceStartZ, double TraceEndZ, FVector& OutLocation) const;
	void UpdateShapeVisualization();
	void CompactSpawnedTrashItems();
	void ClearSpawnedTrashInternal();
	void NotifyManagerStateChanged() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMD_TrashItem>> SpawnedTrashItems;

    UPROPERTY(Transient)
    TMap<FName, int32> RemainingKindCounts;

    UPROPERTY(Transient)
    TMap<FIntVector, FMD_TrashFloorCellState> RemainingFloorCells;

    UPROPERTY(Transient)
    bool bStateInitialized = false;

    UPROPERTY(Transient)
    int32 LastSpawnShortfall = 0;
};
