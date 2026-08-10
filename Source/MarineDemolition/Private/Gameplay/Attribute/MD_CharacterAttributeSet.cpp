// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//기획의 매직넘버 금지 규칙에 따라 Init값은 상수로 적용
namespace MD_AttributeDefaults
{
	static constexpr float DefaultMaxHealth = 100.0f;

	/** 기획 02 §6.1 기본 산소 지속시간 300초(5분) */
	static constexpr float DefaultMaxOxygen = 300.0f;

	/** MaxOxygen이 0이 되면 게이지 나눗셈이 0으로 나누기가 되므로 하한을 둔다 */
	static constexpr float MinMaxOxygen = 1.0f;
}

UMD_CharacterAttributeSet::UMD_CharacterAttributeSet()
{
	InitMaxHealth(MD_AttributeDefaults::DefaultMaxHealth);
	InitHealth(MD_AttributeDefaults::DefaultMaxHealth);

	//산소는 시작시 완충상태
	InitMaxOxygen(MD_AttributeDefaults::DefaultMaxOxygen);
	InitOxygen(MD_AttributeDefaults::DefaultMaxOxygen);
}

void UMD_CharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	//모든 클라에 나타냄
	DOREPLIFETIME_CONDITION_NOTIFY(UMD_CharacterAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMD_CharacterAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);

	//산소값 복제
	DOREPLIFETIME_CONDITION_NOTIFY(UMD_CharacterAttributeSet, Oxygen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMD_CharacterAttributeSet, MaxOxygen, COND_None, REPNOTIFY_Always);
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

void UMD_CharacterAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	//값 보정은 서버에서
	const AActor* OwningActor = GetOwningActor();
	if (!IsValid(OwningActor) || !OwningActor->HasAuthority())
	{
		return;
	}

	//산소탱크 착용하면 비율에 따라 현재 산소량도 증가되도록 함.
	//산소 150/300(50%)에서 탱크를 끼면 → 210/420(50%)
	if (Attribute == GetMaxOxygenAttribute())
	{
		if (OldValue > 0.f && !FMath::IsNearlyEqual(OldValue, NewValue))
		{
			SetOxygen(FMath::Clamp(GetOxygen() * NewValue / OldValue, 0.f, NewValue));
		}
	}
}

void UMD_CharacterAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	//현재값이 바뀌기 전 호출하여 체력 강제
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}

	//산소포화도 0 ~ MaxOxygen으로 강제
	else if (Attribute == GetOxygenAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxOxygen());
	}

	//최대치가 0 이하로 떨어지는것 예외처리
	else if (Attribute == GetMaxOxygenAttribute())
	{
		NewValue = FMath::Max(NewValue, MD_AttributeDefaults::MinMaxOxygen);
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

// 클라이언트가 복제받은 값을 GAS내부에 반영시킴
void UMD_CharacterAttributeSet::OnRep_Oxygen(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMD_CharacterAttributeSet, Oxygen, OldValue);
}

void UMD_CharacterAttributeSet::OnRep_MaxOxygen(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMD_CharacterAttributeSet, MaxOxygen, OldValue);
}
