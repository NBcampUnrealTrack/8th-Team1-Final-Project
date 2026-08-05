// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"

UMD_CharacterAttributeSet::UMD_CharacterAttributeSet()
{
	InitMaxHealth(100.0f);
	InitHealth(100.0f);
}

void UMD_CharacterAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	//현재값이 바뀌기 전 호출하여 체력 강제
	ClampAttribute(Attribute, NewValue);
}

void UMD_CharacterAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	//베이스값이 바뀔때 호출
	Super::PreAttributeBaseChange(Attribute, NewValue);

	ClampAttribute(Attribute, NewValue);
}

void UMD_CharacterAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	//현재값이 바뀌기 전 호출하여 체력 강제
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
}
