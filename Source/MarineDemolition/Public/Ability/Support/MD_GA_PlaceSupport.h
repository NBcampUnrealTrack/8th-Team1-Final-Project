#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "MD_GA_PlaceSupport.generated.h"

class AMD_SupportBrace;
class UStaticMeshComponent;

struct FMD_SupportPlacement
{
	FTransform Transform = FTransform::Identity;
	FVector FloorLocation = FVector::ZeroVector;
	FVector FloorNormal = FVector::UpVector;
	TArray<TWeakObjectPtr<AMD_CuttableWall>> Walls;
	FText FailureReason;
	bool bHasFloor = false;
	bool bCanPlace = false;
};

/** One placement query shared by the local hologram and authoritative installation. */
UCLASS()
class MARINEDEMOLITION_API UMD_GA_PlaceSupport : public UMD_GA_WallBase
{
	GENERATED_BODY()
public:
	UMD_GA_PlaceSupport();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** Wall presence never affects validity; collect links only for authoritative installation. */
	void EvaluatePlacement(APawn* Pawn, FMD_SupportPlacement& OutPlacement,
		bool bCollectSupportedWalls = false) const;
	const UStaticMeshComponent* GetBraceMeshTemplate() const;
	float GetSupportRadius() const { return SupportRadiusCm; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Support")
	TSubclassOf<AMD_SupportBrace> BraceClass;
	/** Horizontal reach from the pawn; distant aim is clamped before the floor query. */
	UPROPERTY(EditDefaultsOnly, Category = "Support", meta = (ClampMin = "100", ClampMax = "1000", Units = "cm"))
	float MaxPlacementDistanceCm = 350.f;
	UPROPERTY(EditDefaultsOnly, Category = "Support", meta = (ClampMin = "50", ClampMax = "1000", Units = "cm"))
	float SupportRadiusCm = 250.f;
	UPROPERTY(EditDefaultsOnly, Category = "Support", meta = (ClampMin = "50", ClampMax = "500", Units = "cm"))
	float FloorSearchDepthCm = 300.f;
	UPROPERTY(EditDefaultsOnly, Category = "Support", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
	float MaxFloorSlopeDegrees = 12.f;
	/** Allowed angle between the supporting floor and a contacted ceiling underside. */
	UPROPERTY(EditDefaultsOnly, Category = "Support|Ceiling Contact", meta = (ClampMin = "0", ClampMax = "15", Units = "deg"))
	float CeilingParallelToleranceDegrees = 3.f;
};
