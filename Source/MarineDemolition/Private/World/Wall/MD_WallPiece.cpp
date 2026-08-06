// Fill out your copyright notice in the Description page of Project Settings.

#include "World/Wall/MD_WallPiece.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "Util/MD_MaterialTintUtility.h"

AMD_WallPiece::AMD_WallPiece()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetSimulatePhysics(true);

	// 조각벽은 모두 같은 방식으로 운반한다. BP 지정에 맡기면 비어 있는 조각을 못 집는다
	PickupMethodTag = MDGameplayTags::Interaction_Pickup_Physics;
	StorageContentTag = MDGameplayTags::Resource_Storage_Content_WallPiece;
}

void AMD_WallPiece::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMD_WallPiece, bContaminated);
}

FGameplayTag AMD_WallPiece::GetTrashCategory_Implementation() const
{
	// 기획 02 §11 매핑표: 조각벽(깨끗) → 안타는 쓰레기 / 오염된 조각벽 → 오염된 쓰레기
	return bContaminated
		? MDGameplayTags::Score_Sorting_Contaminated
		: MDGameplayTags::Score_Sorting_NonBurnable;
}

void AMD_WallPiece::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();

	if (PickupMethodTag.IsValid()
		&& PickupMethodTag.MatchesTag(MDGameplayTags::Interaction_Pickup_Physics))
	{
		TagContainer.AddTag(PickupMethodTag);
	}

	if (StorageContentTag.IsValid()
		&& StorageContentTag.MatchesTag(MDGameplayTags::Resource_Storage_Content))
	{
		TagContainer.AddTag(StorageContentTag);
	}
}

void AMD_WallPiece::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_WallPiece::SetContaminated(bool bInContaminated)
{
	bContaminated = bInContaminated;

	// 클라이언트는 OnRep_Contaminated가 같은 일을 한다
	ApplyContaminationVisual();
}

void AMD_WallPiece::OnRep_Contaminated()
{
	ApplyContaminationVisual();
}

void AMD_WallPiece::ApplyContaminationVisual()
{
	if (!IsValid(Mesh)) return;

	// 깨끗 = 밝은 회백색, 오염 = 보라 (격벽 스캔 색과 겹치지 않게)
	if (UMaterialInstanceDynamic* PieceMID = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MDMaterialTint::SetVectorParameterChecked(PieceMID, TEXT("Color"),
			bContaminated ? FLinearColor(0.45f, 0.1f, 0.5f) : FLinearColor(0.85f, 0.85f, 0.8f));
	}
}
