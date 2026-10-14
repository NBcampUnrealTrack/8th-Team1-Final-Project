// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Game/MD_ReticleWidget.h"

#include "Character/MD_Character.h"
#include "Component/Interaction/MD_GlassDrawingFocusComponent.h"
#include "Components/Image.h"

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
	UpdateCenterReticleVisibility();
}

void UMD_ReticleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateCenterReticleVisibility();
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
		&& (!CachedCharacter->GetGlassDrawingFocusComponent()
			|| !CachedCharacter->GetGlassDrawingFocusComponent()->IsFocusActive())
		&& IsValid(Img_CenterReticle->GetBrush().GetResourceObject());
	// Hidden preserves the authored layout when the owning pawn is unavailable.
	const ESlateVisibility CenterVisibility = bVisible
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
	if (Img_CenterReticle->GetVisibility() != CenterVisibility)
	{
		Img_CenterReticle->SetVisibility(CenterVisibility);
	}
}
