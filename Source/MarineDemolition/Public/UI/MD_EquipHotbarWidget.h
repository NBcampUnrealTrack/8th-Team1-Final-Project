// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_EquipHotbarWidget.generated.h"

class UMD_EquipHotbarComponent;
class UMD_ToolDefinition;

UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_EquipHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	UFUNCTION(BlueprintImplementableEvent, Category ="Hotbar")
	void OnHotbarRefreshed();

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetSlotCount() const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetCurrentSlotIndex() const;

	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ToolDefinition* GetToolAtSlot(int32 SlotIndex) const;

private:
	UFUNCTION()
	void HandleHotbarChanged();

	UPROPERTY()
	TObjectPtr<UMD_EquipHotbarComponent> BoundHotbar;
};
