// Fill out your copyright notice in the Description page of Project Settings.


#include "Wall/MD_GA_WallCut.h"

#include "Wall/MD_CuttableWall.h"

void UMD_GA_WallCut::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
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
		Wall->Cut(ActorInfo->AvatarActor.Get());
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
