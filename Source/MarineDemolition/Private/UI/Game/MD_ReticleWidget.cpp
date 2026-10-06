// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Game/MD_ReticleWidget.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_ThrowComponent.h"
#include "Components/Image.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

UMD_ReticleWidget::UMD_ReticleWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
}

void UMD_ReticleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
	bThrowChargeVisible = false;
	ForceVolatile(false);
	UpdateCenterReticleVisibility();
	UpdateThrowChargeVisibility();
}

void UMD_ReticleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateCenterReticleVisibility();
	UpdateThrowChargeVisibility();
}

int32 UMD_ReticleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 LastLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const UMD_ThrowComponent* ThrowComponent = CachedCharacter.IsValid()
		? CachedCharacter->GetThrowComponent() : nullptr;
	if (!bThrowChargeVisible || !IsValid(ThrowComponent) || !ThrowComponent->IsCharging()
		|| !FMath::IsFinite(ThrowChargeBarWidth) || ThrowChargeBarWidth <= 0.0f
		|| !FMath::IsFinite(ThrowChargeBarThickness) || ThrowChargeBarThickness <= 0.0f
		|| !FMath::IsFinite(ThrowChargeOffset.X) || !FMath::IsFinite(ThrowChargeOffset.Y))
	{
		return LastLayer;
	}

	FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5;
	if (IsValid(Img_CenterReticle))
	{
		const FGeometry& ReticleGeometry = Img_CenterReticle->GetCachedGeometry();
		const FVector2D ReticleSize = ReticleGeometry.GetLocalSize();
		if (ReticleSize.X > 0.0 && ReticleSize.Y > 0.0)
		{
			Center = AllottedGeometry.AbsoluteToLocal(
				ReticleGeometry.LocalToAbsolute(ReticleSize * 0.5));
		}
	}

	const float RawFraction = ThrowComponent->GetChargeFraction();
	const float Fraction = FMath::IsFinite(RawFraction) ? FMath::Clamp(RawFraction, 0.0f, 1.0f) : 0.0f;
	const FVector2f BarSize(ThrowChargeBarWidth, ThrowChargeBarThickness);
	const FVector2f BarPosition(Center + ThrowChargeOffset - FVector2D(BarSize) * 0.5);
	const FSlateLayoutTransform BarTransform(BarPosition);
	const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const ESlateDrawEffect Effects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	FSlateDrawElement::MakeBox(OutDrawElements, LastLayer + 1,
		AllottedGeometry.ToPaintGeometry(BarSize, BarTransform), Brush, Effects,
		ThrowChargeBackgroundColor * WidgetTint);
	if (Fraction > 0.0f)
	{
		const FLinearColor FillColor = Fraction >= 1.0f ? ThrowChargeFullColor : ThrowChargeFillColor;
		FSlateDrawElement::MakeBox(OutDrawElements, LastLayer + 2,
			AllottedGeometry.ToPaintGeometry(FVector2f(BarSize.X * Fraction, BarSize.Y), BarTransform),
			Brush, Effects, FillColor * WidgetTint);
		return LastLayer + 2;
	}
	return LastLayer + 1;
}

void UMD_ReticleWidget::UpdateCenterReticleVisibility()
{
	APawn* const Pawn = GetOwningPlayerPawn();
	if (CachedOwningPawn.Get() != Pawn)
	{
		CachedOwningPawn = Pawn;
		CachedCharacter = Cast<AMD_Character>(Pawn);
	}

	if (!IsValid(Img_CenterReticle))
	{
		return;
	}

	const bool bVisible = CachedCharacter.IsValid()
		&& IsValid(Img_CenterReticle->GetBrush().GetResourceObject());
	// Hidden preserves the authored layout when the owning pawn is unavailable.
	const ESlateVisibility CenterVisibility = bVisible
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
	if (Img_CenterReticle->GetVisibility() != CenterVisibility)
	{
		Img_CenterReticle->SetVisibility(CenterVisibility);
	}
}

void UMD_ReticleWidget::UpdateThrowChargeVisibility()
{
	const UMD_ThrowComponent* ThrowComponent = CachedCharacter.IsValid()
		? CachedCharacter->GetThrowComponent() : nullptr;
	const bool bVisible = bShowThrowCharge && IsValid(ThrowComponent) && ThrowComponent->IsCharging();
	if (bThrowChargeVisible != bVisible)
	{
		bThrowChargeVisible = bVisible;
		// Animate while charging, then return the reticle to Slate's cached path.
		ForceVolatile(bVisible);
		InvalidateLayoutAndVolatility();
	}
}
