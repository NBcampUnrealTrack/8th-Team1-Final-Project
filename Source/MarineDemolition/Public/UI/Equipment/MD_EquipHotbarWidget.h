// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_EquipHotbarWidget.generated.h"

class UMD_EquipHotbarComponent;
class UMD_ItemDefinition;

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

	// 도구와 소모품 등 모든 슬롯 아이템을 BP UI에 제공
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UMD_ItemDefinition* GetItemAtSlot(int32 SlotIndex);

	// BP 슬롯 UI에 현재 보관 수량 제공
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetItemCountAtSlot(int32 SlotIndex);
private:

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleHotbarChanged();

	void BindToPawn(APawn* NewPawn);

	void UnbindHotbar();

	UPROPERTY()
	TObjectPtr<UMD_EquipHotbarComponent> BoundHotbar;
};
