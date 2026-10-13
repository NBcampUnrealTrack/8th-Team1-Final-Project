#pragma once

#include "CoreMinimal.h"
#include "World/Wall/MD_WallUtility.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "MD_CoolingPump.generated.h"

class UNiagaraComponent;

/** Day3 repair target. Its utility state also controls remote isolation-room hazards. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_CoolingPump : public AMD_WallUtility
{
	GENERATED_BODY()

public:
	AMD_CoolingPump();

	UFUNCTION(BlueprintPure, Category = "MD|Cooling")
	bool IsCoolingRestored() const { return GetUtilityState() != EMD_UtilityState::Broken; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnUtilityStateChangedLocal(EMD_UtilityState NewState) override;

	/** Assign a looping contaminated-water Niagara system; position at the pump's damaged outlet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Cooling")
	TObjectPtr<UNiagaraComponent> ContaminatedWaterFX;

private:
	FMD_AudioHandle LeakAudio;
};
