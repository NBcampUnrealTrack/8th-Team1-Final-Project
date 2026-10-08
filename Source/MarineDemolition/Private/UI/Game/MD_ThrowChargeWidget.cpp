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
	// 재구성 시 충전 상태와 이미지 표시를 다시 동기화하기 위한 캐시 초기화
	LastChargeFraction = -1.0f;
	// 현재 소유 플레이어의 실제 충전 상태를 Blueprint에 반영
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

	// 시작과 종료 또는 충전 비율 변화가 있을 때만 이미지 갱신 요청
	bool bChargeChanged = bThrowCharging != bCharging || LastChargeFraction != Fraction;
	if (bThrowCharging != bCharging || bThrowFullyCharged != bFullyCharged)
	{
		bThrowCharging = bCharging;
		bThrowFullyCharged = bFullyCharged;
		OnChargeStateChanged(bCharging, bFullyCharged);
	}

	// Blueprint의 기존 충전 시작과 갱신 및 종료 함수 연결
	if (bChargeChanged == true)
	{
		// 다음 프레임의 중복 알림을 차단할 표시 비율 저장
		LastChargeFraction = Fraction;
		// 이미지와 머티리얼 처리를 담당하는 이벤트 호출
		OnChargeUpdated(bCharging, Fraction);
	}
}
