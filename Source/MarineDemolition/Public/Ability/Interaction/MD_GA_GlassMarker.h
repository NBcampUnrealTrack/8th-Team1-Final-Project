#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MD_GA_GlassMarker.generated.h"

/** Current-item GAS lifetime shared by the nib and rear eraser held inputs. */
UCLASS()
class MARINEDEMOLITION_API UMD_GA_GlassMarker : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMD_GA_GlassMarker();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
