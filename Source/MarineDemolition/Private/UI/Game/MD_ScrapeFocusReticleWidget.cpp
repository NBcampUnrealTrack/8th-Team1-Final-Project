// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Game/MD_ScrapeFocusReticleWidget.h"

#include "Character/MD_Character.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"

namespace
{
	constexpr int32 ReticleCircleSegments = 32;

	void DrawReticleLine(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& Geometry,
		const int32 LayerId,
		const FVector2D& Start,
		const FVector2D& End,
		const FLinearColor& Color,
		const float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(2);
		Points.Add(UE::Slate::CastToVector2f(Start));
		Points.Add(UE::Slate::CastToVector2f(End));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			Color,
			true,
			Thickness);
	}
}

UMD_ScrapeFocusReticleWidget::UMD_ScrapeFocusReticleWidget(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 파생 Widget Blueprint가 장식용 child를 추가해도 포인터 입력을 가로채지 않는다.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
	// 포인터 위치와 Focus/aim 상태는 별도 UMG 속성 변경 없이 Component에서 갱신된다.
	// Invalidation cache 안에서도 NativePaint가 매 프레임 다시 실행되게 한다.
	ForceVolatile(true);
}

int32 UMD_ScrapeFocusReticleWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);

	const APlayerController* const PlayerController = GetOwningPlayer();
	const AMD_Character* const Character = IsValid(PlayerController)
		? Cast<AMD_Character>(PlayerController->GetPawn())
		: nullptr;
	const UMD_ScrapeToolComponent* const ScrapeTool = IsValid(Character)
		? Character->GetScrapeToolComponent()
		: nullptr;
	if (!IsValid(Character))
	{
		return BaseLayer;
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= UE_SMALL_NUMBER || LocalSize.Y <= UE_SMALL_NUMBER)
	{
		return BaseLayer;
	}

	// Keep the center marker independent of equipped tools and the movable focus pointer.
	const FVector2D ScreenCenter = LocalSize * 0.5;
	TArray<FVector2f> CenterCircle;
	CenterCircle.Reserve(ReticleCircleSegments + 1);
	for (int32 Index = 0; Index <= ReticleCircleSegments; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / ReticleCircleSegments;
		CenterCircle.Add(UE::Slate::CastToVector2f(ScreenCenter + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 3.0));
	}
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 1, AllottedGeometry.ToPaintGeometry(),
		CenterCircle, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.8f), true, 3.0f);
	FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 2, AllottedGeometry.ToPaintGeometry(),
		CenterCircle, ESlateDrawEffect::None, FLinearColor::White, true, 1.25f);
	if (!IsValid(ScrapeTool) || !ScrapeTool->IsScrapeFocusViewActive()) return BaseLayer + 2;

	const FVector2D Pointer = ScrapeTool->GetScrapeFocusPointerNormalized();
	const FVector2D Center(
		FMath::Clamp(Pointer.X, 0.0, 1.0) * LocalSize.X,
		FMath::Clamp(Pointer.Y, 0.0, 1.0) * LocalSize.Y);

	const FLinearColor ReticleColor =
		ScrapeTool->GetScrapeFocusState() == EMD_ScrapeFocusState::FocusPending
			? PendingColor
			: (ScrapeTool->IsScrapeFocusReady()
				&& ScrapeTool->IsScrapeFocusAimValid()
					? ActionableColor
					: InvalidColor);
	const int32 ReticleLayer = BaseLayer + 3;

	TArray<FVector2f> CirclePoints;
	CirclePoints.Reserve(ReticleCircleSegments + 1);
	for (int32 SegmentIndex = 0; SegmentIndex <= ReticleCircleSegments; ++SegmentIndex)
	{
		const float Angle = UE_TWO_PI
			* static_cast<float>(SegmentIndex)
			/ static_cast<float>(ReticleCircleSegments);
		const FVector2D Point = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle))
			* ReticleRadius;
		CirclePoints.Add(UE::Slate::CastToVector2f(Point));
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		ReticleLayer,
		AllottedGeometry.ToPaintGeometry(),
		CirclePoints,
		ESlateDrawEffect::None,
		ReticleColor,
		true,
		ReticleThickness);

	const float Inner = ReticleRadius + ReticleGap;
	const float Outer = Inner + ReticleArmLength;
	DrawReticleLine(
		OutDrawElements,
		AllottedGeometry,
		ReticleLayer,
		Center + FVector2D(-Outer, 0.0),
		Center + FVector2D(-Inner, 0.0),
		ReticleColor,
		ReticleThickness);
	DrawReticleLine(
		OutDrawElements,
		AllottedGeometry,
		ReticleLayer,
		Center + FVector2D(Inner, 0.0),
		Center + FVector2D(Outer, 0.0),
		ReticleColor,
		ReticleThickness);
	DrawReticleLine(
		OutDrawElements,
		AllottedGeometry,
		ReticleLayer,
		Center + FVector2D(0.0, -Outer),
		Center + FVector2D(0.0, -Inner),
		ReticleColor,
		ReticleThickness);
	DrawReticleLine(
		OutDrawElements,
		AllottedGeometry,
		ReticleLayer,
		Center + FVector2D(0.0, Inner),
		Center + FVector2D(0.0, Outer),
		ReticleColor,
		ReticleThickness);

	return ReticleLayer;
}
