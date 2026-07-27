#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Environment/MD_DayCycleTimeSource.h"
#include "MD_GameStateDayCycleAdapterComponent.generated.h"

class AMD_GameStateBase;

/**
 * Reads AMD_GameStateBase match time without adding sky code to GameState/GameMode.
 * Add to BP_Sky and enable bDriveFromGameState on this adapter (not on GameState/GameMode).
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_GameStateDayCycleAdapterComponent : public UActorComponent, public IMD_DayCycleTimeSource
{
	GENERATED_BODY()

public:
	UMD_GameStateDayCycleAdapterComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|DayCycle")
	bool bDriveFromGameState = false;

	virtual bool IsDayCycleActive_Implementation() const override;
	virtual float GetTimeOfDayHours_Implementation() const override;
	virtual float GetDayCycleProgress_Implementation() const override;

private:
	AMD_GameStateBase* ResolveGameState() const;
};
