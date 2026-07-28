// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MD_GA_Scrape.generated.h"

class UMD_ScrapeToolComponent;

/**
 * Scrape 동작만 구현하는 Blueprint Ability용 C++ 베이스.
 * 인스턴싱, 네트워크 정책 등 데이터 옵션은 파생 Blueprint에서 구성한다.
 */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_GA_Scrape : public UGameplayAbility
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
