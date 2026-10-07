// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Game/MD_ThrowChargeWidget.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_ThrowComponent.h"
#include "Components/ProgressBar.h"
#include "GameFramework/PlayerController.h"

UMD_ThrowChargeWidget::UMD_ThrowChargeWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
}

void UMD_ThrowChargeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
	UpdateThrowChargeUI();
}

void UMD_ThrowChargeWidget::NativeDestruct()
{
	bHUDPresentationEnabled = false;
	UpdateThrowChargeUI();
	CachedOwningPawn.Reset();
	CachedCharacter.Reset();
	Super::NativeDestruct();
}

void UMD_ThrowChargeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateThrowChargeUI();
}

void UMD_ThrowChargeWidget::SetHUDPresentationEnabled(bool bEnabled)
{
	if (bHUDPresentationEnabled == bEnabled)
	{
		return;
	}

	bHUDPresentationEnabled = bEnabled;
	UpdateThrowChargeUI();
}

void UMD_ThrowChargeWidget::UpdateThrowChargeUI()
{
	if (IsDesignTime())
	{
		return;
	}

	APawn* const Pawn = GetOwningPlayerPawn();
	if (CachedOwningPawn.Get() != Pawn)
	{
		CachedOwningPawn = Pawn;
		CachedCharacter = Cast<AMD_Character>(Pawn);
	}

	const APlayerController* OwningController = GetOwningPlayer();
	const UMD_ThrowComponent* ThrowComponent = CachedCharacter.IsValid()
		? CachedCharacter->GetThrowComponent() : nullptr;
	const bool bCharging = bHUDPresentationEnabled
		&& IsValid(OwningController) && OwningController->IsLocalController()
		&& CachedCharacter.IsValid() && CachedCharacter->IsLocallyControlled()
		&& IsValid(ThrowComponent) && ThrowComponent->IsCharging();
	const float RawFraction = bCharging ? ThrowComponent->GetChargeFraction() : 0.0f;
	const float Fraction = FMath::IsFinite(RawFraction) ? FMath::Clamp(RawFraction, 0.0f, 1.0f) : 0.0f;
	const bool bFullyCharged = bCharging && Fraction >= 1.0f;

	if (IsValid(Progress_ThrowCharge))
	{
		if (Progress_ThrowCharge->GetPercent() != Fraction)
		{
			Progress_ThrowCharge->SetPercent(Fraction);
		}
		// Keep the authored layout and the wrapper's tick while the gauge is idle.
		const ESlateVisibility GaugeVisibility = bCharging
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
		if (Progress_ThrowCharge->GetVisibility() != GaugeVisibility)
		{
			Progress_ThrowCharge->SetVisibility(GaugeVisibility);
		}
	}

	if (bThrowCharging != bCharging || bThrowFullyCharged != bFullyCharged)
	{
		bThrowCharging = bCharging;
		bThrowFullyCharged = bFullyCharged;
		OnChargeStateChanged(bCharging, bFullyCharged);
	}
}
