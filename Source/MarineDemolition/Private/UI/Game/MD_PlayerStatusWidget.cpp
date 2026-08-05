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

	// 구독 전에 복제된 값 즉시 반영
	RefreshHealth();
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
