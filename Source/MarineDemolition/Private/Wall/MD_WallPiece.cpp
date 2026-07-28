// Fill out your copyright notice in the Description page of Project Settings.

#include "Wall/MD_WallPiece.h"

#include "Components/StaticMeshComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"

AMD_WallPiece::AMD_WallPiece()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetSimulatePhysics(true);

	StaticGameplayTags.AddTag(MDGameplayTags::Interaction_Pickup_Physics);
	StaticGameplayTags.AddTag(MDGameplayTags::Item_Type_WallPiece);
}

void AMD_WallPiece::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer = StaticGameplayTags;

	if (bContaminated)
	{
		TagContainer.AddTag(MDGameplayTags::Item_State_Contaminated);
	}
}

void AMD_WallPiece::SetContaminated(bool bInContaminated)
{
	bContaminated = bInContaminated;

	// 깨끗 = 밝은 회백색, 오염 = 보라 (격벽 스캔 색과 겹치지 않게)
	if (UMaterialInstanceDynamic* PieceMID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		PieceMID->SetVectorParameterValue(TEXT("Color"),
			bContaminated ? FLinearColor(0.45f, 0.1f, 0.5f) : FLinearColor(0.85f, 0.85f, 0.8f));
	}
}
