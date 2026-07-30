// Fill out your copyright notice in the Description page of Project Settings.


#include "Interaction/MD_TravelConsole.h"

#include "Character/MD_Character.h"
#include "Collision/MD_CollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "UI/MD_TravelConsoleWidget.h"


AMD_TravelConsole::AMD_TravelConsole()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_TravelConsole::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_TravelConsole::Interact_Implementation(AActor* Interactor)
{
	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);

	if (!IsValid(InstigatorCharacter))
	{
		return;
	}
	
	APlayerController* PC = Cast<APlayerController>(InstigatorCharacter->GetController());
	
	// UI는 로컬 플레이어에게만
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}

	if (!ConsoleWidgetClass)
	{
		return;
	}

	// 이미 열려 있으면 중복 생성하지 않음
	if (ActiveWidget.IsValid() && ActiveWidget->IsInViewport())
	{
		return;
	}

	UMD_TravelConsoleWidget* Widget = CreateWidget<UMD_TravelConsoleWidget>(PC, ConsoleWidgetClass);
	if (!IsValid(Widget))
	{
		return;
	}

	Widget->SetOwningConsole(this);
	Widget->AddToViewport();
	ActiveWidget = Widget;

	PC->SetShowMouseCursor(true);
	PC->SetInputMode(FInputModeGameAndUI());
}
