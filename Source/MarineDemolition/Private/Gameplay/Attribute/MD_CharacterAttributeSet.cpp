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

	//값 바뀌기 전 호출하여 체력 강제
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
}
