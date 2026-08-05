// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "Net/UnrealNetwork.h"

UMD_CharacterAttributeSet::UMD_CharacterAttributeSet()
{
	InitMaxHealth(100.0f);
	InitHealth(100.0f);
}

void UMD_CharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	//모든 클라에 나타냄
	DOREPLIFETIME_CONDITION_NOTIFY(UMD_CharacterAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMD_CharacterAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
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

void UMD_CharacterAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	//체력 설정, 변경 판정은 서버에서만 한다.
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMD_CharacterAttributeSet, Health, OldValue);
}

void UMD_CharacterAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMD_CharacterAttributeSet, MaxHealth, OldValue);
}
