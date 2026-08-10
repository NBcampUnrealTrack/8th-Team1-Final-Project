#include "World/Contamination/MD_ScrapedDebrisBase.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"

AMD_ScrapedDebrisBase::AMD_ScrapedDebrisBase()
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

	// 산출물은 모두 같은 방식으로 운반한다. BP 지정에 맡기면 비어 있는 것을 못 집는다
	PickupMethodTag = MDGameplayTags::Interaction_Pickup_Physics;
}

FGameplayTag AMD_ScrapedDebrisBase::GetTrashCategory_Implementation() const
{
	// 기획 02 §11 매핑표: 오염물(헤라) → 오염된 쓰레기 / 따개비(헤라) → 오염된 쓰레기
	return MDGameplayTags::Score_Sorting_Contaminated;
}

void AMD_ScrapedDebrisBase::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
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

void AMD_ScrapedDebrisBase::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}
