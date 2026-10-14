#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "MD_GA_Crowbar.generated.h"

/** Uses the player's owned interaction RPC; the door itself is not client-owned. */
UCLASS()
class MARINEDEMOLITION_API UMD_GA_Crowbar : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UMD_GA_Crowbar();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};

/** Retains exactly one item-scoped ability while the permanent tool is in the hotbar. */
UCLASS()
class MARINEDEMOLITION_API UMD_GE_CrowbarGrant : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UMD_GE_CrowbarGrant();
};
