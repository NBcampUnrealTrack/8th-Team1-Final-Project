// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ability/Scrape/MD_GA_Scrape.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Scrape/MD_StainScraperComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/World.h"
#include "TimerManager.h"

UMD_GA_Scrape::UMD_GA_Scrape()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	bServerRespectsRemoteAbilityCancellation = true;
}

void UMD_GA_Scrape::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AMD_Character* Character = Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
	UMD_StainScraperComponent* StainScraper = Character ? Character->GetStainScraperComponent() : nullptr;
	const UMD_ToolDefinition* SourceTool = ActorInfo ? Cast<UMD_ToolDefinition>(GetSourceObject(Handle, ActorInfo)) : nullptr;
	// A client prediction never authorizes a different inventory item's ability on the server.
	if (!Character || !StainScraper || !IsValid(SourceTool) || !GetWorld()
		|| !Character->GetHotbarComponent() || Character->GetHotbarComponent()->GetCurrentTool() != SourceTool
		|| !CommitAbility(Handle, ActorInfo, ActivationInfo) || !StainScraper->StartStroke(Handle))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	bStainStroke = true;
	GetWorld()->GetTimerManager().SetTimer(StainStrokeTimer, this,
		&UMD_GA_Scrape::FinishStainStroke, StainScraper->GetStrokeDuration(), false);
}

void UMD_GA_Scrape::FinishStainStroke()
{
	if (IsActive()) EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UMD_GA_Scrape::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	if (bStainStroke)
	{
		bStainStroke = false;
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(StainStrokeTimer);
		AMD_Character* Character = Cast<AMD_Character>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
		if (Character && Character->GetStainScraperComponent()) Character->GetStainScraperComponent()->FinishStroke(Handle);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
