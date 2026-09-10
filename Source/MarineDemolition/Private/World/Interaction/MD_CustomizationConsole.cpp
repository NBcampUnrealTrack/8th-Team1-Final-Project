#include "World/Interaction/MD_CustomizationConsole.h"

#include "Character/MD_Character.h"
#include "Component/Customization/MD_CharacterCustomizationComponent.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Components/StaticMeshComponent.h"
#include "UI/Customization/MD_CharacterCustomizationWidget.h"
#include "UI/MD_LocalUIManager.h"
#include "CommonActivatableWidget.h"

AMD_CustomizationConsole::AMD_CustomizationConsole()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_CustomizationConsole::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh) == true)
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

FText AMD_CustomizationConsole::GetInteractionDisplayName_Implementation() const
{
	return NSLOCTEXT("MDCustomization", "ConsoleName", "거울");
}

void AMD_CustomizationConsole::Interact_Implementation(AActor* Interactor)
{
	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);
	if (IsValid(InstigatorCharacter) == false)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(InstigatorCharacter->GetController());
	if (IsValid(PC) == false || PC->IsLocalController() == false)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	UMD_LocalUIManager* LocalUIManager =
		IsValid(LocalPlayer) == true ? LocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	if (IsValid(LocalUIManager) == false || IsValid(CustomizationWidgetClass) == false)
	{
		return;
	}

	// 같은 커스터마이징 화면이 이미 열려 있으면 중복 Push를 막는다.
	UCommonActivatableWidget* ActiveGameMenuWidget =
		LocalUIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameMenu);
	if (IsValid(ActiveGameMenuWidget) == true && ActiveGameMenuWidget->IsA(CustomizationWidgetClass) == true)
	{
		return;
	}

	// 캐릭터의 CustomizationComponent를 위젯에 연결하여 GameMenu 레이어에 Push한다.
	UMD_CharacterCustomizationComponent* CustomizationComp =
		InstigatorCharacter->FindComponentByClass<UMD_CharacterCustomizationComponent>();

	LocalUIManager->PushScreenToLayerInitialized(
		MDGameplayTags::UI_Layer_GameMenu,
		CustomizationWidgetClass,
		[CustomizationComp](UCommonActivatableWidget& Screen)
		{
			UMD_CharacterCustomizationWidget* CustomizationWidget =
				Cast<UMD_CharacterCustomizationWidget>(&Screen);
			if (IsValid(CustomizationWidget) == false)
			{
				return;
			}

			CustomizationWidget->InitializeCustomization(CustomizationComp);
		});
}
