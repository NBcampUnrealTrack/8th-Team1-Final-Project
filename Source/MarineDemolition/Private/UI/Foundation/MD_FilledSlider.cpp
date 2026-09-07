#include "UI/Foundation/MD_FilledSlider.h"
#include "UI/Foundation/SMD_FilledSlider.h"
#include "Brushes/SlateColorBrush.h"

UMD_FilledSlider::UMD_FilledSlider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 별도 텍스처 없이 표시 가능한 기본 채움 브러시
	FSlateColorBrush DefaultFill(FLinearColor::White);
	// 기본 채움 외형 초기화
	NormalFillImage = DefaultFill;
	// 호버 채움 외형 초기화
	HoveredFillImage = DefaultFill;
	// 비활성 채움 외형 초기화
	DisabledFillImage = DefaultFill;
}

TSharedRef<SWidget> UMD_FilledSlider::RebuildWidget()
{
	// 기본 슬라이더의 스타일과 포커스 및 이벤트 연결 유지
	SSlider::FArguments SliderArguments;
	// UObject가 소유하는 기본 슬라이더 스타일 참조
	SliderArguments.Style(&GetWidgetStyle());
	// 디자이너의 포커스 설정 적용
	SliderArguments.IsFocusable(IsFocusable);
	// 마우스 캡처 시작 이벤트 전달
	SliderArguments.OnMouseCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureBegin));
	// 마우스 캡처 종료 이벤트 전달
	SliderArguments.OnMouseCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureEnd));
	// 컨트롤러 캡처 시작 이벤트 전달
	SliderArguments.OnControllerCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureBegin));
	// 컨트롤러 캡처 종료 이벤트 전달
	SliderArguments.OnControllerCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureEnd));
	// 기존 값 변경 이벤트와 필드 알림 유지
	SliderArguments.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));
	// 입력 중 뷰포트 갱신 설정 전달
	SliderArguments.PreventThrottling(bPreventThrottling);
	// 부모의 값 바인딩과 리소스 해제를 그대로 사용하는 Slate 인스턴스 생성
	MySlider = SNew(SMD_FilledSlider).SliderArguments(SliderArguments);
	// 디자이너에 저장된 채움 외형 적용
	ApplyFillAppearance();
	// 위젯 반환용 참조 변환
	TSharedRef<SWidget> SliderWidget = MySlider.ToSharedRef();
	// 생성된 위젯 전달
	return SliderWidget;
}

void UMD_FilledSlider::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// 디자이너 변경과 런타임 속성 동기화
	ApplyFillAppearance();
}

void UMD_FilledSlider::SetFillColor(FLinearColor InColor)
{
	// 사용자 채움 색상 저장
	FillColor = InColor;
	// 현재 Slate 외형 갱신
	ApplyFillAppearance();
}

void UMD_FilledSlider::SetNormalFillImage(FSlateBrush InBrush)
{
	// 기본 채움 브러시 저장
	NormalFillImage = MoveTemp(InBrush);
	// 현재 Slate 외형 갱신
	ApplyFillAppearance();
}

void UMD_FilledSlider::SetHoveredFillImage(FSlateBrush InBrush)
{
	// 호버 채움 브러시 저장
	HoveredFillImage = MoveTemp(InBrush);
	// 현재 Slate 외형 갱신
	ApplyFillAppearance();
}

void UMD_FilledSlider::SetDisabledFillImage(FSlateBrush InBrush)
{
	// 비활성 채움 브러시 저장
	DisabledFillImage = MoveTemp(InBrush);
	// 현재 Slate 외형 갱신
	ApplyFillAppearance();
}

void UMD_FilledSlider::ApplyFillAppearance()
{
	// Slate 생성 이전 또는 해제 이후의 호출 구분
	bool bHasSlider = MySlider.IsValid();
	// Slate 인스턴스가 없는 경우 저장된 설정만 유지
	if (bHasSlider == false)
	{
		return;
	}
	// RebuildWidget에서 생성한 채움 슬라이더 참조
	TSharedPtr<SMD_FilledSlider> FilledSlider = StaticCastSharedPtr<SMD_FilledSlider>(MySlider);
	// 상태별 브러시와 공통 색상 전달
	FilledSlider->SetFillAppearance(FillColor, NormalFillImage, HoveredFillImage, DisabledFillImage);
}
