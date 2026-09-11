// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_InstantItemDefinition.h"
#include "MD_SupplyItemDefinition.generated.h"

// 설치 GA가 사용하는 지지대 수량 저장소로 즉시 지급하는 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_SupplyItemDefinition : public UMD_InstantItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;
};
