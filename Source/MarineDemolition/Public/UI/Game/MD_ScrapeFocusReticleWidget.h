// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ScrapeFocusReticleWidget.generated.h"

class UImage;

/**
 * 디자이너에 배치한 중앙 조준점과 헤라 Focus Image를 갱신하는 입력 비소비형 위젯이다.
 *
 * 이름으로 연결된 Image의 역할만 약속하며 부모 패널·슬롯·중첩 배치는 디자이너가 정한다.
 * 중앙 Image의 배치는 유지하고 Focus Image는 실제 Geometry 기준 Render Translation만 갱신한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeFocusReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMD_ScrapeFocusReticleWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void UpdateReticles(const FGeometry& MyGeometry);

	// 기존 BP를 단계적으로 배치할 수 있도록 optional로 두며, 누락 시 경고 후 숨긴다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_CenterReticle = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_FocusReticle = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle")
	FLinearColor ActionableColor = FLinearColor(0.16f, 1.0f, 0.32f, 0.95f);

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle")
	FLinearColor InvalidColor = FLinearColor(1.0f, 0.22f, 0.12f, 0.88f);

	UPROPERTY(EditDefaultsOnly, Category = "MD|Scrape|Reticle")
	FLinearColor PendingColor = FLinearColor(1.0f, 0.68f, 0.12f, 0.88f);
};
