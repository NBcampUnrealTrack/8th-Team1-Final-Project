// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ScrapeFocusReticleWidget.generated.h"

/**
 * GameHUD의 자식으로 중앙 조준점과 헤라 Focus 포인터를 그리는 입력 비소비형 위젯이다.
 *
 * HUD의 전체 영역에 배치하고 생성과 표시 여부는 부모 HUD에 맡긴다.
 * 플레이어 화면의 normalized 좌표를 위젯 로컬 좌표로 변환하여 SafeZone과 DPI를 반영한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeFocusReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMD_ScrapeFocusReticleWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;

	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle", meta = (ClampMin = "1.0"))
	float ReticleRadius = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle", meta = (ClampMin = "0.0"))
	float ReticleGap = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle", meta = (ClampMin = "1.0"))
	float ReticleArmLength = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle", meta = (ClampMin = "0.5"))
	float ReticleThickness = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle")
	FLinearColor ActionableColor = FLinearColor(0.16f, 1.0f, 0.32f, 0.95f);

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle")
	FLinearColor InvalidColor = FLinearColor(1.0f, 0.22f, 0.12f, 0.88f);

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle")
	FLinearColor PendingColor = FLinearColor(1.0f, 0.68f, 0.12f, 0.88f);
};
