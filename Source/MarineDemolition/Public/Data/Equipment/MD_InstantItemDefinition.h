// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "MD_InstantItemDefinition.generated.h"

// Hotbar 보관 없이 구매 즉시 적용하는 아이템의 공통 부모
UCLASS(Abstract, BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_InstantItemDefinition : public UMD_ItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
};
