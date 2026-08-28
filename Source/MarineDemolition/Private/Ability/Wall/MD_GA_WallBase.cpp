// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallBase.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "World/Wall/MD_CuttableWall.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTags/MD_GameplayTags.h"

UMD_GA_WallBase::UMD_GA_WallBase()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 기본 대상은 "자를 수 있는 것". 퀘스트 오브젝트도 이 태그만 붙이면 대상이 된다
	RequiredTargetTags.AddTag(MDGameplayTags::Interaction_Cuttable);
}

bool UMD_GA_WallBase::ActorHasAnyTargetTag(const AActor* Actor, const FGameplayTagContainer& Tags)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	// 태그 조건이 비어 있으면 검사하지 않는다
	if (Tags.IsEmpty())
	{
		return true;
	}

	// 1) GameplayTagAssetInterface — C++ 액터 / ASC 보유 액터
	if (const IGameplayTagAssetInterface* TagAsset = Cast<const IGameplayTagAssetInterface>(Actor))
	{
		FGameplayTagContainer Owned;
		TagAsset->GetOwnedGameplayTags(Owned);
		if (Owned.HasAny(Tags))
		{
			return true;
		}
	}

	// 2) AActor::Tags — 블루프린트 디테일 패널에서 문자열로 붙인 태그
	for (const FGameplayTag& Tag : Tags)
	{
		if (Actor->ActorHasTag(Tag.GetTagName()))
		{
			return true;
		}
	}

	return false;
}

bool UMD_GA_WallBase::TryGetAimedHit(const FGameplayAbilityActorInfo* ActorInfo, FHitResult& OutHit) const
{
	OutHit = FHitResult();

	if (!ActorInfo)
	{
		return false;
	}

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn || !Pawn->GetController() || !Pawn->GetWorld())
	{
		return false;
	}

	FVector ViewLoc;
	FRotator ViewRot;
	Pawn->GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);

	return Pawn->GetWorld()->LineTraceSingleByChannel(
		OutHit,
		ViewLoc,
		ViewLoc + ViewRot.Vector() * TraceDistance,
		ECC_Visibility,
		Params);
}

bool UMD_GA_WallBase::TryGetAimedTargetHit(
	const FGameplayAbilityActorInfo* ActorInfo,
	AActor*& OutActor,
	FHitResult& OutHit) const
{
	OutActor = nullptr;

	FHitResult Hit;
	if (!TryGetAimedHit(ActorInfo, Hit))
	{
		return false;
	}

	AActor* HitActor = Hit.GetActor();
	if (!ActorHasAnyTargetTag(HitActor, RequiredTargetTags))
	{
		return false;
	}

	OutActor = HitActor;
	OutHit = Hit;
	return true;
}

AMD_CuttableWall* UMD_GA_WallBase::GetAimedWall(const FGameplayAbilityActorInfo* ActorInfo) const
{
	AMD_CuttableWall* Wall = nullptr;
	FHitResult Hit;
	TryGetAimedWallHit(ActorInfo, Wall, Hit);
	return Wall;
}

bool UMD_GA_WallBase::TryGetAimedWallHit(
	const FGameplayAbilityActorInfo* ActorInfo,
	AMD_CuttableWall*& OutWall,
	FHitResult& OutHit) const
{
	OutWall = nullptr;
	OutHit = FHitResult();

	FHitResult Hit;
	if (!TryGetAimedHit(ActorInfo, Hit))
	{
		return false;
	}

	OutWall = Cast<AMD_CuttableWall>(Hit.GetActor());
	if (!OutWall)
	{
		return false;
	}

	OutHit = Hit;
	return true;
}
