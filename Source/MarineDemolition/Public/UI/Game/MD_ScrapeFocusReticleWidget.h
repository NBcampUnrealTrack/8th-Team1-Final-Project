// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ScrapeFocusReticleWidget.generated.h"

/**
 * 화면 고정 헤라 Focus의 가상 포인터를 그리는 입력 비소비형 위젯이다.
 *
 * 별도 Blueprint 자산이 없어도 동작하며, normalized 포인터를 현재 위젯 크기에
 * 매 프레임 투영하므로 해상도와 DPI가 바뀌어도 Trace ray와 같은 화면 좌표를 쓴다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeFocusReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMD_ScrapeFocusReticleWidget(const FObjectInitializer& ObjectInitializer);

protected:
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
