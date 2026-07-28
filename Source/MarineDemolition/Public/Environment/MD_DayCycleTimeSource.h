#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_DayCycleTimeSource.generated.h"

/**
 * Optional time source for sky / lighting systems.
 * Implement on a component (e.g. UMD_GameStateDayCycleAdapterComponent) — do not modify GameMode/GameState for sky.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UMD_DayCycleTimeSource : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_DayCycleTimeSource
{
	GENERATED_BODY()

public:
	/** True when this source should drive sky time (e.g. match clock has started). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|DayCycle")
	bool IsDayCycleActive() const;

	/** Fractional hours 0..24 (e.g. 14.5 = 14:30). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|DayCycle")
	float GetTimeOfDayHours() const;

	/** 0..1 progress for orbit / shader day fraction. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|DayCycle")
	float GetDayCycleProgress() const;
};
