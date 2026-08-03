// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ability/MD_GA_Scrape.h"

#include "Character/MD_Character.h"
#include "Demolition/Scrape/MD_ScrapeToolComponent.h"

UMD_GA_Scrape::UMD_GA_Scrape()
{
	// 소유 클라이언트는 조준/축 고정을 즉시 예측하고, 같은 Ability를 서버에서도 실행해
	// 실제 오염 상태 변경은 서버 컴포넌트만 수행한다.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
}

void UMD_GA_Scrape::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AMD_Character* Character =
		Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	UMD_ScrapeToolComponent* ScrapeTool =
		Character ? Character->GetScrapeToolComponent() : nullptr;
	if (!ScrapeTool)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ScrapeTool->StartScraping();
}

void UMD_GA_Scrape::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	AMD_Character* Character =
		Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	if (UMD_ScrapeToolComponent* ScrapeTool =
			Character ? Character->GetScrapeToolComponent() : nullptr)
	{
		ScrapeTool->StopScraping();
	}

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}
