// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_InstantItemDefinition.h"
#include "MD_SupplyItemDefinition.generated.h"

// 지지대 수량을 적립하고 최초 구매 때 재사용 가능한 설치 슬롯을 지급한다.
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_SupplyItemDefinition : public UMD_InstantItemDefinition
{
	GENERATED_BODY()

public:
	virtual FGameplayTag GetUseAbilityTag() override;
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;
};
