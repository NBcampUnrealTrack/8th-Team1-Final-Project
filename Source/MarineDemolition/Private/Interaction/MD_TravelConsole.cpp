// Fill out your copyright notice in the Description page of Project Settings.


#include "Interaction/MD_TravelConsole.h"

#include "Character/MD_Character.h"
#include "Components/StaticMeshComponent.h"
#include "UI/MD_TravelConsoleWidget.h"


AMD_TravelConsole::AMD_TravelConsole()
{
	PrimaryActorTick.bCanEverTick = false;
	
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
}

void AMD_TravelConsole::Interact(AMD_Character* InstigatorCharacter)
{
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
	
	UMD_TravelConsoleWidget* Widget = CreateWidget<UMD_TravelConsoleWidget>(PC, ConsoleWidgetClass);
	if (!IsValid(Widget))
	{
		return;
	}
	
	Widget->SetOwningConsole(this);
	Widget->AddToViewport();

	PC->SetShowMouseCursor(true);
	PC->SetInputMode(FInputModeGameAndUI());
}
