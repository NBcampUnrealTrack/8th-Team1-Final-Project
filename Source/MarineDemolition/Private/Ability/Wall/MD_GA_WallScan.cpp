// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallScan.h"

#include "World/Wall/MD_CuttableWall.h"

void UMD_GA_WallScan::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (AMD_CuttableWall* Wall = GetAimedWall(ActorInfo))
	{
		Wall->ScanHighlight(ScanDuration);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
