#pragma once

#include "CoreMinimal.h"
#include "MD_TrashSpawnState.generated.h"

/** Empty cells are retained so a later session cannot refill a cleaned area. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashFloorCellState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	FIntVector Cell = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	TMap<FName, int32> RemainingKindCounts;
};

/** A placed generator's unsorted trash, independent of actor transforms. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashGeneratorState
{
	GENERATED_BODY()

	/** Stable level actor name within the saved map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	FName GeneratorName = NAME_None;

	/** Exact remaining count by trash kind after the initial weighted draws. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	TMap<FName, int32> RemainingKindCounts;

	/** False is the original volume ledger format. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	bool bUsesFloorCells = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
    bool bUsesTowerPlacement = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	float FloorCellSize = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	float FloorLayerHeight = 0.0f;

	/** Detects incompatible changes to the sampled floor layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	FGuid FloorLayoutId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Spawn")
	TArray<FMD_TrashFloorCellState> FloorCells;

	bool IsStructurallyValid() const;
};

/** In-memory day travel only. File saves deliberately retain the count/cell ledger instead. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashItemPlacement
{
    GENERATED_BODY()
    UPROPERTY() FName KindId;
    UPROPERTY() FName VariantId;
    UPROPERTY() FIntVector OriginCell = FIntVector::ZeroValue;
    UPROPERTY() FTransform Transform = FTransform::Identity;
    UPROPERTY() bool bWasAwake = false;
    UPROPERTY() FVector LinearVelocity = FVector::ZeroVector;
    UPROPERTY() FVector AngularVelocity = FVector::ZeroVector;
};

USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashGeneratorPlacement
{
    GENERATED_BODY()
    UPROPERTY() TArray<FMD_TrashItemPlacement> Items;
    bool MatchesState(const FMD_TrashGeneratorState& State) const;
};
