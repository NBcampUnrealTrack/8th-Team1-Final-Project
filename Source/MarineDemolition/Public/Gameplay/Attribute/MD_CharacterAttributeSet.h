// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "MD_CharacterAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class MARINEDEMOLITION_API UMD_CharacterAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
	
public:
	UMD_CharacterAttributeSet();

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Attribute")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UMD_CharacterAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "MD|Attribute")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UMD_CharacterAttributeSet, MaxHealth)

private:
private:
	/* 두 경로가 같은 규칙을 쓰도록 클램프를 한 함수로 모은다. */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};
