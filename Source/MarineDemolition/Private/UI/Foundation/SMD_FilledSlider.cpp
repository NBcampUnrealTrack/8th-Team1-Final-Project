#include "UI/Foundation/SMD_FilledSlider.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"

void SMD_FilledSlider::Construct(const FArguments& InArgs)
{
	SSlider::Construct(InArgs._SliderArguments);

	// 리소스 없이 표시 가능한 기본 단색 채움 구성
	FSlateColorBrush DefaultFill(FLinearColor::White);
	// 초기 상태별 채움 외형 통일
	SetFillAppearance(FLinearColor::White, DefaultFill, DefaultFill, DefaultFill);
}

void SMD_FilledSlider::SetFillAppearance(FLinearColor InColor, FSlateBrush InNormal, FSlateBrush InHovered, FSlateBrush InDisabled)
{
	// 채움 색상 보관
	FillColor = InColor;
	// 기본 브러시 수명 보장
	NormalFillImage = MoveTemp(InNormal);
	// 호버 브러시 수명 보장
	HoveredFillImage = MoveTemp(InHovered);
	// 비활성 브러시 수명 보장
	DisabledFillImage = MoveTemp(InDisabled);
	// 캐시된 렌더링 결과 갱신 요청
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SMD_FilledSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 기본 슬라이더와 동일한 가로 기준 좌표계 사용
	float Width = Orientation == Orient_Horizontal ? AllottedGeometry.GetLocalSize().X : AllottedGeometry.GetLocalSize().Y;
	// 방향에 따른 트랙 중심축 계산용 높이
	float Height = Orientation == Orient_Horizontal ? AllottedGeometry.GetLocalSize().Y : AllottedGeometry.GetLocalSize().X;
	// 엔진 상태 선택 API의 읽기 전용 브러시 참조
	const FSlateBrush* Thumb = GetThumbImage();
	// 엔진 상태 선택 API의 읽기 전용 트랙 참조
	const FSlateBrush* Bar = GetBarImage();
	// 현재 상태에 대응하는 핸들 크기
	FVector2f HandleSize = Thumb->ImageSize;
	// 기본 슬라이더의 핸들 여백 설정 조회
	bool bIndent = GetIndentHandleAttribute().Get();
	// 기본 입력 좌표 계산과 일치하는 여백
	float Indentation = bIndent == true ? HandleSize.X : 0.0f;
	// 사용자 범위에 대한 정규화 값 조회
	float NormalizedValue = GetNormalizedValue();
	// 외부 바인딩 값의 렌더링 범위 제한
	float Percent = FMath::Clamp(NormalizedValue, 0.0f, 1.0f);
	// 기본 슬라이더와 동일한 핸들 이동 길이
	float TravelLength = Width - Indentation - HandleSize.X;
	// 실제 핸들 중심까지의 채움 계산
	float HandleCenter = HandleSize.X * 0.5f + Indentation * 0.5f + Percent * TravelLength;
	// 트랙 시작점과 핸들 배치에 사용할 중심 높이
	float CenterY = Height * 0.5f;
	// 작은 위젯에서 음수 트랙 크기 방지
	float BarWidth = FMath::Max(Width - HandleSize.X, 0.0f);
	// 음수 두께의 렌더링 방지
	float Thickness = FMath::Max(Style->BarThickness, 0.0f);
	// 트랙 시작 위치
	FVector2f BarPosition(HandleSize.X * 0.5f, CenterY - Thickness * 0.5f);
	// 트랙 전체 크기
	FVector2f BarSize(BarWidth, Thickness);
	// 기본 슬라이더와 일치하는 핸들 위치
	FVector2f ThumbPosition(HandleCenter - HandleSize.X * 0.5f, CenterY - HandleSize.Y * 0.5f);
	// 방향 변환 전의 페인트 지오메트리
	FGeometry PaintGeometry = AllottedGeometry;
	// 세로 방향에서 최솟값을 아래쪽에 배치
	if (Orientation == Orient_Vertical)
	{
		// 가로 좌표계를 반시계 방향으로 회전
		FSlateRenderTransform Rotation = TransformCast<FSlateRenderTransform>(
			Concatenate(Inverse(FVector2f(Width, 0)), FQuat2D(FMath::DegreesToRadians(-90.0f))));
		// 부모 변환을 유지하는 세로 페인트 지오메트리 구성
		PaintGeometry = AllottedGeometry.MakeChild(FVector2f(Width, Height), FSlateLayoutTransform(), Rotation, FVector2f::ZeroVector);
	}
	// 부모 비활성 상태 반영
	bool bEnabled = ShouldBeEnabled(bParentEnabled);
	// 비활성 렌더링 효과 선택
	ESlateDrawEffect Effects = bEnabled == true ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	// 위젯과 부모의 투명도 반영
	FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	// 기존 트랙 색상과 브러시 틴트 조합
	FLinearColor BarTint = Bar->GetTint(InWidgetStyle) * GetSliderBarColorAttribute().Get().GetColor(InWidgetStyle) * WidgetTint;
	// 배경 트랙을 최하단 레이어에 출력
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry.ToPaintGeometry(BarSize, FSlateLayoutTransform(BarPosition)),
		Bar, Effects, BarTint);
	// 상태별 멤버 브러시의 읽기 전용 페인트 참조
	const FSlateBrush* Fill = &NormalFillImage;
	// 잠금 상태 조회
	bool bLocked = IsLocked();
	// 비활성 채움 스타일 우선 적용
	if (bEnabled == false)
	{
		// 비활성 브러시 선택
		Fill = &DisabledFillImage;
	}
	else if (bLocked == true)
	{
		// 기본 슬라이더와 동일한 잠금 스타일 선택
		Fill = &DisabledFillImage;
	}
	else
	{
		// 호버 상태 조회
		bool bHovered = IsHovered();
		// 호버 브러시 적용 여부 확인
		if (bHovered == true)
		{
			// 호버 채움 스타일 선택
			Fill = &HoveredFillImage;
		}
	}
	// 핸들 중심을 넘지 않는 채움 길이
	float FillWidth = FMath::Clamp(HandleCenter - BarPosition.X, 0.0f, BarWidth);
	// 최솟값에서 채움 완전 제거
	if (Percent <= 0.0f)
	{
		// 핸들 여백과 무관한 빈 트랙 표시
		FillWidth = 0.0f;
	}
	// 최댓값에서 트랙 전체 채움
	else if (Percent >= 1.0f)
	{
		// 핸들 여백 뒤쪽까지 전체 채움
		FillWidth = BarWidth;
	}
	// 배경 위의 채움 레이어 확보
	++LayerId;
	// 빈 채움의 불필요한 그리기 방지
	if (FillWidth > 0.0f)
	{
		// 현재 값에 해당하는 채움 크기
		FVector2f FillSize(FillWidth, Thickness);
		// 브러시 틴트와 사용자 색상 및 부모 투명도 조합
		FLinearColor FillTint = Fill->GetTint(InWidgetStyle) * FillColor * WidgetTint;
		// 채움 영역 출력
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry.ToPaintGeometry(FillSize, FSlateLayoutTransform(BarPosition)),
			Fill, Effects, FillTint);
	}
	// 채움 위에 핸들을 배치하는 최상단 레이어 확보
	++LayerId;
	// 기존 핸들 색상과 부모 투명도 반영
	FLinearColor ThumbTint = Thumb->GetTint(InWidgetStyle) * GetSliderHandleColorAttribute().Get().GetColor(InWidgetStyle) * WidgetTint;
	// 기본 슬라이더 핸들 출력
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry.ToPaintGeometry(HandleSize, FSlateLayoutTransform(ThumbPosition)),
		Thumb, Effects, ThumbTint);
	// 마지막 사용 레이어 전달
	return LayerId;
}
