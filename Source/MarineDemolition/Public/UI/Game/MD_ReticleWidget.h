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

private:
	void UpdateCenterReticleVisibility();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_CenterReticle = nullptr;

	TWeakObjectPtr<APawn> CachedOwningPawn;
	TWeakObjectPtr<AMD_Character> CachedCharacter;
};
