// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_PlayerStatusWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "TimerManager.h"

#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Player/MD_PlayerState.h"

void UMD_PlayerStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 위젯이 다시 Construct될 때 현재 Oxygen 비율로 모든 Child를 다시 동기화한다.
	PreviousOxygenIndex = INDEX_NONE;

	TryBindToAbilitySystem();
}

void UMD_PlayerStatusWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
	}

	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(
				UMD_CharacterAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(
				UMD_CharacterAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetOxygenAttribute()).Remove(OxygenChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetMaxOxygenAttribute()).Remove(MaxOxygenChangedHandle);
	}

	Super::NativeDestruct();
}

void UMD_PlayerStatusWidget::TryBindToAbilitySystem()
{
	AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	UAbilitySystemComponent* ASC =
			IsValid(MDPlayerState) ? MDPlayerState->GetAbilitySystemComponent() : nullptr;

	if (!IsValid(ASC) || !IsValid(MDPlayerState->GetCharacterAttributeSet()))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
					BindRetryTimerHandle,
					this,
					&UMD_PlayerStatusWidget::TryBindToAbilitySystem,
					0.5f,
					false
			);
		}
		return;
	}

	BoundASC = ASC;

	HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetHealthAttribute())
			.AddUObject(this, &UMD_PlayerStatusWidget::HandleHealthChanged);

	MaxHealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			UMD_CharacterAttributeSet::GetMaxHealthAttribute())
			.AddUObject(this, &UMD_PlayerStatusWidget::HandleHealthChanged);

	OxygenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMD_CharacterAttributeSet::GetOxygenAttribute())
		.AddUObject(this, &UMD_PlayerStatusWidget::HandleOxygenChanged);
	MaxOxygenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMD_CharacterAttributeSet::GetMaxOxygenAttribute())
		.AddUObject(this, &UMD_PlayerStatusWidget::HandleOxygenChanged);

	// 구독 전에 복제된 값 즉시 반영
	RefreshHealth();
	RefreshOxygen();
}

void UMD_PlayerStatusWidget::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	RefreshHealth();
}

void UMD_PlayerStatusWidget::RefreshHealth()
{
	const AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	const UMD_CharacterAttributeSet* Attributes =
			IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	if (!IsValid(HealthBar) || !IsValid(Attributes))
	{
		return;
	}

	const float MaxHealth = Attributes->GetMaxHealth();

	HealthBar->SetPercent(MaxHealth > 0.f ? Attributes->GetHealth() / MaxHealth : 0.f);
}

void UMD_PlayerStatusWidget::HandleOxygenChanged(const FOnAttributeChangeData& Data)
{
	// 주기 GE가 최대치에서 같은 값을 전달하면 불필요한 HUD 갱신을 생략한다.
	if (FMath::IsNearlyEqual(Data.OldValue, Data.NewValue) == true)
	{
		return;
	}

	// 실제 산소량이 변경된 경우에만 게이지를 갱신한다.
	RefreshOxygen();
}

void UMD_PlayerStatusWidget::RefreshOxygen()
{
	const AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	const UMD_CharacterAttributeSet* Attributes =
		IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	// 산소 단계 컨테이너가 바인딩되지 않은 경우 갱신 중단
	if (IsValid(HB_OxygenContainer) == false)
	{
		return;
	}

	// 산소 어트리뷰트가 준비되지 않은 경우 갱신 중단
	if (IsValid(Attributes) == false)
	{
		return;
	}

	// 최대 산소값을 사용하여 현재 산소 비율을 계산한다.
	float MaxOxygenValue = Attributes->GetMaxOxygen();
	float OxygenRatio = MaxOxygenValue > 0.0f ? OxygenRatio = Attributes->GetOxygen() / MaxOxygenValue : 0.0f;

	// 비정상적인 어트리뷰트 값이 Child Index 범위를 벗어나지 않도록 제한한다.
	OxygenRatio = FMath::Clamp(OxygenRatio, 0.0f, 1.0f);

	// 현재 구성된 산소 위젯 개수를 저장한다.
	int32 OxygenWidgetCount = HB_OxygenContainer->GetChildrenCount();

	// 표시할 산소 위젯이 없는 경우 갱신을 종료한다.
	if (OxygenWidgetCount <= 0)
	{
		return;
	}

	// 산소 비율을 실제 Child 개수에 맞는 단계로 변환한다.
	int32 NewOxygenIndex = FMath::FloorToInt(OxygenRatio * OxygenWidgetCount);

	// 산소가 가득 찬 경우에도 마지막 Child Index를 넘지 않도록 제한한다.
	NewOxygenIndex = FMath::Clamp(NewOxygenIndex, 0, OxygenWidgetCount - 1);

	UpdateOxygenWidget(NewOxygenIndex);

	//// RefreshHealth와 같은 방식. 0으로 나누는 것만 막고 0~1 비율을 넘긴다
	//Pb_Oxygen->SetPercent(
	//	MaxOxygenValue > 0.f ? Attributes->GetOxygen() / MaxOxygenValue : 0.f);
}
