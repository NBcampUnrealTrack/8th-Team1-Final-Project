#pragma once

#include "CoreMinimal.h"
#include "Components/Slider.h"
#include "MD_FilledSlider.generated.h"

// Slate 채움 슬라이더를 디자이너와 Blueprint에 제공하는 래퍼
UCLASS(meta = (DisplayName = "MD Filled Slider"))
class MARINEDEMOLITION_API UMD_FilledSlider : public USlider
{
	GENERATED_BODY()

public:
	// 리소스가 필요 없는 기본 채움 외형 초기화
	UMD_FilledSlider(const FObjectInitializer& ObjectInitializer);
	virtual void SynchronizeProperties() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

public:
	// 현재 값까지 채워지는 영역의 색상과 투명도
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFillColor, Category = "Appearance")
	FLinearColor FillColor = FLinearColor::White;

	// 기본 상태의 채움 브러시
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNormalFillImage, Category = "Style|Style")
	FSlateBrush NormalFillImage;

	// 마우스 호버 상태의 채움 브러시
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHoveredFillImage, Category = "Style|Style")
	FSlateBrush HoveredFillImage;

	// 비활성 또는 잠금 상태의 채움 브러시
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDisabledFillImage, Category = "Style|Style")
	FSlateBrush DisabledFillImage;

public:
	// 채움 색상 변경과 Slate 갱신
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Appearance")
	void SetFillColor(FLinearColor InColor);

	// 기본 채움 브러시 변경과 Slate 갱신
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Appearance")
	void SetNormalFillImage(FSlateBrush InBrush);

	// 호버 채움 브러시 변경과 Slate 갱신
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Appearance")
	void SetHoveredFillImage(FSlateBrush InBrush);

	// 비활성 채움 브러시 변경과 Slate 갱신
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Appearance")
	void SetDisabledFillImage(FSlateBrush InBrush);

protected:
	// 현재 외형을 생성된 Slate 위젯에 반영
	void ApplyFillAppearance();
};
