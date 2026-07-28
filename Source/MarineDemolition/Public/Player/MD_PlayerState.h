// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "MD_PlayerState.generated.h"

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	AMD_PlayerState();
	
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

};
