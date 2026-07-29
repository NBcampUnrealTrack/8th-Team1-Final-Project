// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerState.h"
#include "AbilitySystemComponent.h"
#include "Hotbar/MD_HotbarComponent.h"

AMD_PlayerState::AMD_PlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	HotbarComponent = CreateDefaultSubobject<UMD_HotbarComponent>(TEXT("HotbarComponent"));
}

UAbilitySystemComponent* AMD_PlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
