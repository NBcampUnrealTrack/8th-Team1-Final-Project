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
	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn || !Pawn->GetController())
	{
		return nullptr;
	}
	
	FVector ViewLoc;
	FRotator ViewRot;
	Pawn->GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
	
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);

	if (Pawn->GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + ViewRot.Vector() * TraceDistance, ECC_Visibility, Params))
	{
		return Cast<AMD_CuttableWall>(Hit.GetActor());
	}
	return nullptr;
}
