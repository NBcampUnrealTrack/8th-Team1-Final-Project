// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Interaction/MD_DayEndConsole.h"

#include "Character/MD_Character.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "UI/Interaction/MD_DayEndConsoleWidget.h"


AMD_DayEndConsole::AMD_DayEndConsole()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_DayEndConsole::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_DayEndConsole::Interact_Implementation(AActor* Interactor)
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

	UMD_DayEndConsoleWidget* Widget = CreateWidget<UMD_DayEndConsoleWidget>(PC, ConsoleWidgetClass);
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
