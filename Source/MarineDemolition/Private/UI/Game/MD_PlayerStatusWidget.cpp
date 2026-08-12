// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_PlayerStatusWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/ProgressBar.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Player/MD_PlayerState.h"

void UMD_PlayerStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
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
	RefreshOxygen();
}

void UMD_PlayerStatusWidget::RefreshOxygen()
{
	const AMD_PlayerState* MDPlayerState = GetOwningPlayerState<AMD_PlayerState>();

	const UMD_CharacterAttributeSet* Attributes =
		IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	if (!IsValid(Pb_Oxygen) || !IsValid(Attributes))
	{
		return;
	}

	const float MaxOxygenValue = Attributes->GetMaxOxygen();

	// RefreshHealth와 같은 방식. 0으로 나누는 것만 막고 0~1 비율을 넘긴다
	Pb_Oxygen->SetPercent(
		MaxOxygenValue > 0.f ? Attributes->GetOxygen() / MaxOxygenValue : 0.f);
}
