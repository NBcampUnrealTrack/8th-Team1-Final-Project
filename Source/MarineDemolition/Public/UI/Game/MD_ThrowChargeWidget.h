// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ThrowChargeWidget.generated.h"

class AMD_Character;
class APawn;
class UProgressBar;

/** Updates the designer-authored charge gauge for the owning player's throw. */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_ThrowChargeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMD_ThrowChargeWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Throw|Charge")
	void SetHUDPresentationEnabled(bool bEnabled);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 기존 ProgressBar 방식 위젯의 호환을 위한 선택적 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "Throw|Charge", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Progress_ThrowCharge = nullptr;

	UFUNCTION(BlueprintImplementableEvent, Category = "Throw|Charge")
	void OnChargeStateChanged(bool bCharging, bool bFullyCharged);

	// 현재 충전 상태와 비율을 이미지 표시용 Blueprint에 전달
	UFUNCTION(BlueprintImplementableEvent, Category = "Throw|Charge")
	void OnChargeUpdated(bool bCharging, float NormalizedCharge);

	// 같은 충전값의 반복적인 머티리얼 갱신 방지를 위한 마지막 표시 비율
	float LastChargeFraction = -1.0f;

private:
	void UpdateThrowChargeUI();

	TWeakObjectPtr<APawn> CachedOwningPawn;
	TWeakObjectPtr<AMD_Character> CachedCharacter;
	bool bHUDPresentationEnabled = true;
	bool bThrowCharging = false;
	bool bThrowFullyCharged = false;
};
