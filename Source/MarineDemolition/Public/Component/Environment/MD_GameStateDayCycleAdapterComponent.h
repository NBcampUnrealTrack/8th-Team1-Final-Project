#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Environment/MD_DayCycleTimeSource.h"
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

	/**
	 * When true, sky follows AMD_GameStateBase wall-clock (08:00→18:00).
	 * Converted to sky hours where 12 = midnight (08:00 wall → 20).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|DayCycle")
	bool bDriveFromGameState = true;

	virtual bool IsDayCycleActive_Implementation() const override;
	virtual float GetTimeOfDayHours_Implementation() const override;
	virtual float GetDayCycleProgress_Implementation() const override;

private:
	AMD_GameStateBase* ResolveGameState() const;
};
