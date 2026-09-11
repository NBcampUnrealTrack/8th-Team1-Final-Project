#include "World/Sample/MD_QuestInteractionPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"

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
	// Interact는 서버에서 실행된다 (RequiresServerInteraction 기본값 true)
	if (HasAuthority() == false)
	{
		return;
	}

	if (QuestDefinitionId.IsValid() == false)
	{
		return;
	}

	UMD_MerchantSubsystem* Merchant = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
	if (IsValid(Merchant) == false)
	{
		return;
	}

	bool bSuccess = Merchant->NotifyInteractionProgress(QuestDefinitionId, PhaseIndex);
	if (bSuccess == true)
	{
		Destroy();
	}
}
