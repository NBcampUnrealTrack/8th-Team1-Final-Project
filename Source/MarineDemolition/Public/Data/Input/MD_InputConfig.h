// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_InputConfig.generated.h"

class UInputAction;
class UMD_ThrowSettings;
class UMD_GlassMarkerSettings;

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

	// 폼건 조준(RMB 홀드). 누르는 동안 궤적 프리뷰와 조준 자세를 켠다. 키는 IMC에서 매핑한다.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AimAction;

	/** Boolean action: Started charges, Completed releases, Canceled aborts. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Throw")
	TObjectPtr<UInputAction> ThrowAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Throw")
	TObjectPtr<UInputAction> CancelThrowAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Throw")
	TObjectPtr<UMD_ThrowSettings> ThrowSettings;

	/** Hold secondary use to present the marker's rear eraser. Keys are authored in the IMC. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Glass Marker")
	TObjectPtr<UInputAction> GlassMarkerEraseAction;

	/** Press to select the next marker ink color; its key is authored in the IMC. */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Glass Marker")
	TObjectPtr<UInputAction> GlassMarkerColorCycleAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Glass Marker")
	TObjectPtr<UMD_GlassMarkerSettings> GlassMarkerSettings;

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
