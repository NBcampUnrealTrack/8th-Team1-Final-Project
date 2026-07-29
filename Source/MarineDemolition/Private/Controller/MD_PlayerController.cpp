// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/MD_PlayerController.h"
#include "UI/MD_HUDWidget.h"

#include "Engine/World.h"
#include "Interaction/MD_TravelConsole.h"

void AMD_PlayerController::ServerRequestTravel_Implementation(AMD_TravelConsole* Console)
{
	if (!IsValid(Console))
	{
		return;
	}

	const FString MapPackage = Console->GetTargetLevel().ToSoftObjectPath().GetLongPackageName();

	if (MapPackage.IsEmpty())
	{
		return;
	}

	GetWorld()->ServerTravel(MapPackage + TEXT("?listen"));
}

void AMD_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	if (!HUDWidgetClass)
	{
		return;
	}

	if (IsValid(HUDWidgetInstance))
	{
		return;
	}

	HUDWidgetInstance = CreateWidget<UMD_HUDWidget>(this, HUDWidgetClass);

	if (!IsValid(HUDWidgetInstance))
	{
		return;
	}

	HUDWidgetInstance->AddToViewport();
}
