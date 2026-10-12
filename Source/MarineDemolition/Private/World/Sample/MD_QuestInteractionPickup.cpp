#include "World/Sample/MD_QuestInteractionPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"

AMD_QuestInteractionPickup::AMD_QuestInteractionPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
	Mesh->SetSimulatePhysics(false);
}

void AMD_QuestInteractionPickup::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh) == true)
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

FText AMD_QuestInteractionPickup::GetInteractionDisplayName_Implementation() const
{
	return DisplayName;
}

void AMD_QuestInteractionPickup::Interact_Implementation(AActor* Interactor)
{
	// 서버에서 상호작용한 일회용 월드 오브젝트 회수
	if (HasAuthority() == true)
	{
		// 퀘스트 진행도와 무관한 오브젝트 제거
		Destroy();
	}
}
