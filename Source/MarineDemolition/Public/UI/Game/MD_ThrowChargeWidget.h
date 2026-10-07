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

	UPROPERTY(BlueprintReadOnly, Category = "Throw|Charge", meta = (BindWidget))
	TObjectPtr<UProgressBar> Progress_ThrowCharge = nullptr;

	UFUNCTION(BlueprintImplementableEvent, Category = "Throw|Charge")
	void OnChargeStateChanged(bool bCharging, bool bFullyCharged);

private:
	void UpdateThrowChargeUI();

	TWeakObjectPtr<APawn> CachedOwningPawn;
	TWeakObjectPtr<AMD_Character> CachedCharacter;
	bool bHUDPresentationEnabled = true;
	bool bThrowCharging = false;
	bool bThrowFullyCharged = false;
};
