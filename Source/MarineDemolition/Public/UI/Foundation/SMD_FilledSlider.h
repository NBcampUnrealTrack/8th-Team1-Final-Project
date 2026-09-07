#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SSlider.h"

// 기본 슬라이더 입력과 접근성을 유지하는 채움 슬라이더
class MARINEDEMOLITION_API SMD_FilledSlider : public SSlider
{
public:
	SLATE_BEGIN_ARGS(SMD_FilledSlider) {}
		// 기본 슬라이더의 스타일과 입력 및 이벤트 설정
		SLATE_ARGUMENT(SSlider::FArguments, SliderArguments)
	SLATE_END_ARGS()

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	// 채움 영역에 곱할 색상
	FLinearColor FillColor = FLinearColor::White;
	// 기본 상태의 채움 브러시 소유
	FSlateBrush NormalFillImage;
	// 호버 상태의 채움 브러시 소유
	FSlateBrush HoveredFillImage;
	// 비활성 또는 잠금 상태의 채움 브러시 소유
	FSlateBrush DisabledFillImage;

public:
	// 기본 슬라이더 설정을 전달하는 초기화
	void Construct(const FArguments& InArgs);
	// 브러시 복사와 페인트 무효화를 통한 외형 갱신
	void SetFillAppearance(FLinearColor InColor, FSlateBrush InNormal, FSlateBrush InHovered, FSlateBrush InDisabled);
};
