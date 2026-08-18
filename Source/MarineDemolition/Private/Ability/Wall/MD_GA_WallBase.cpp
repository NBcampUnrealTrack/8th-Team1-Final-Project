// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallBase.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "World/Wall/MD_CuttableWall.h"

UMD_GA_WallBase::UMD_GA_WallBase()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
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

	if (!ActorInfo)
	{
		return false;
	}

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn || !Pawn->GetController())
	{
		return false;
	}
	
	FVector ViewLoc;
	FRotator ViewRot;
	Pawn->GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
	
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	if (Pawn->GetWorld()->LineTraceSingleByChannel(
			Hit,
			ViewLoc,
			ViewLoc + ViewRot.Vector() * TraceDistance,
			ECC_Visibility,
			Params))
	{
		OutWall = Cast<AMD_CuttableWall>(Hit.GetActor());
		if (OutWall)
		{
			OutHit = Hit;
			return true;
		}
	}
	return false;
}
