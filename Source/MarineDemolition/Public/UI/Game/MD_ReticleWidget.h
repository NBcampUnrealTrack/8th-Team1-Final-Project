// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ReticleWidget.generated.h"

class AMD_Character;
class APawn;
class UImage;

/** Displays the designer-authored center reticle without consuming input. */
UCLASS()
class MARINEDEMOLITION_API UMD_ReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMD_ReticleWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw")
	bool bShowThrowCharge = true;

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw")
	FVector2D ThrowChargeOffset = FVector2D(0.0, 24.0);

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw", meta = (ClampMin = "1.0"))
	float ThrowChargeBarWidth = 72.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw", meta = (ClampMin = "1.0"))
	float ThrowChargeBarThickness = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw")
	FLinearColor ThrowChargeBackgroundColor = FLinearColor(0.08f, 0.10f, 0.13f, 0.8f);

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw")
	FLinearColor ThrowChargeFillColor = FLinearColor(0.25f, 0.8f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Reticle|Throw")
	FLinearColor ThrowChargeFullColor = FLinearColor(0.3f, 1.0f, 0.5f, 1.0f);

private:
	void UpdateCenterReticleVisibility();
	void UpdateThrowChargeVisibility();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_CenterReticle = nullptr;

	TWeakObjectPtr<APawn> CachedOwningPawn;
	TWeakObjectPtr<AMD_Character> CachedCharacter;
	bool bThrowChargeVisible = false;
};
