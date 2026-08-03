// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MD_EquipHotbarWidget.h"
#include "Character/MD_Character.h"
#include "Component/MD_EquipHotbarComponent.h"
#include "GameFramework/PlayerController.h"

void UMD_EquipHotbarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	APlayerController* OwningController = GetOwningPlayer();
	if (!IsValid(OwningController))
	{
		return;
	}


	OwningController->OnPossessedPawnChanged.AddDynamic(this, &UMD_EquipHotbarWidget::HandlePossessedPawnChanged);
	
	BindToPawn(OwningController->GetPawn());

}

void UMD_EquipHotbarWidget::NativeDestruct()
{
	if (APlayerController* OwningController = GetOwningPlayer())
	{
		OwningController->OnPossessedPawnChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandlePossessedPawnChanged);
	}

	UnbindHotbar();

	Super::NativeDestruct();
}

void UMD_EquipHotbarWidget::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	BindToPawn(NewPawn);
}

void UMD_EquipHotbarWidget::HandleHotbarChanged()
{
	OnHotbarRefreshed();
}

void UMD_EquipHotbarWidget::BindToPawn(APawn* NewPawn)
{
	UnbindHotbar();

	AMD_Character* OwningCharacter = Cast<AMD_Character>(NewPawn);
	if (!IsValid(OwningCharacter))
	{
		HandleHotbarChanged();
		return;
	}

	UMD_EquipHotbarComponent* Hotbar = OwningCharacter->GetHotbarComponent();
	if (!IsValid(Hotbar))
	{
		HandleHotbarChanged();
		return;
	}

	BoundHotbar = Hotbar;
	BoundHotbar->OnHotbarChanged.AddDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarChanged);

	HandleHotbarChanged();
}

void UMD_EquipHotbarWidget::UnbindHotbar()
{
	if (IsValid(BoundHotbar))
	{
		BoundHotbar->OnHotbarChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarChanged);
	}

	BoundHotbar = nullptr;
}

int32 UMD_EquipHotbarWidget::GetSlotCount() const
{
	return IsValid(BoundHotbar) ? BoundHotbar->GetSlotCount() : 0;
}

int32 UMD_EquipHotbarWidget::GetCurrentSlotIndex() const
{
	return IsValid(BoundHotbar) ? BoundHotbar->GetCurrentSlotIndex() : INDEX_NONE;
}

UMD_ToolDefinition* UMD_EquipHotbarWidget::GetToolAtSlot(int32 SlotIndex) const
{
	return IsValid(BoundHotbar) ? BoundHotbar->GetToolAtSlot(SlotIndex) : nullptr;
}