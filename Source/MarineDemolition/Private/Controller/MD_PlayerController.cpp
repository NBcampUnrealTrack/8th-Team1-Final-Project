// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/MD_PlayerController.h"
#include "UI/MD_HUDWidget.h"
#include "UI/MD_EquipHotbarWidget.h"

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

	if (!EquipHotbarWidgetClass || IsValid(EquipHotbarWidgetInstance))
	{
		return;
	}

	EquipHotbarWidgetInstance = CreateWidget<UMD_EquipHotbarWidget>(this, EquipHotbarWidgetClass);

	if (!IsValid(EquipHotbarWidgetInstance))
	{
		return;
	}

	EquipHotbarWidgetInstance->AddToViewport();
}
