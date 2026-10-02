// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MD_GA_Scrape.generated.h"

/** Existing Blueprint ability base for one finite placed-contaminant hit. */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_GA_Scrape : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMD_GA_Scrape();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void FinishStainStroke();
	FTimerHandle StainStrokeTimer;
	bool bStainStroke = false;
};
