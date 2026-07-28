// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerState.h"
#include "AbilitySystemComponent.h"

AMD_PlayerState::AMD_PlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
}

UAbilitySystemComponent* AMD_PlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
