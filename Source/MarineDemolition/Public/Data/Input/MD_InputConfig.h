// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_InputConfig.generated.h"

class UInputAction;

UCLASS()
class MARINEDEMOLITION_API UMD_InputConfig : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AbilityAction;

	/** Optional user-authored Boolean IA. Bind R in the mapping context. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ScannerModeSwitchAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SwapEquipmentAction;

	// 핫바 슬롯 숫자키로 직접 선택.
	// 배열로 핫바 슬롯 증감.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TArray<TObjectPtr<UInputAction>> HotbarSlotActions;

	//UPROPERTY(EditDefaultsOnly, Category = "Input")
	//TObjectPtr<UInputAction> WallCutAction;

	//UPROPERTY(EditDefaultsOnly, Category = "Input")
	//TObjectPtr<UInputAction> WallScanAction;
};
