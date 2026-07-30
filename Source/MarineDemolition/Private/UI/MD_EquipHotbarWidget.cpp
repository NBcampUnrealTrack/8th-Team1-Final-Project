// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MD_EquipHotbarWidget.h"
#include "Character/MD_Character.h"
#include "Component/MD_EquipHotbarComponent.h"

void UMD_EquipHotbarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	AMD_Character* OwningCharacter = Cast<AMD_Character>(GetOwningPlayerPawn());
	if (!IsValid(OwningCharacter))
	{
		return;
	}

	BoundHotbar = OwningCharacter->GetHotbarComponent();
	
	BoundHotbar->OnHotbarChanged.AddDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarChanged);
	HandleHotbarChanged();

}

void UMD_EquipHotbarWidget::NativeDestruct()
{
	if (IsValid(BoundHotbar))
	{
		BoundHotbar->OnHotbarChanged.RemoveDynamic(this, &UMD_EquipHotbarWidget::HandleHotbarChanged);
		BoundHotbar = nullptr;
	}

	Super::NativeDestruct();
}

void UMD_EquipHotbarWidget::HandleHotbarChanged()
{
	OnHotbarRefreshed();
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