// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "GameplayTagContainer.h"
#include "MD_RepairItemDefinition.generated.h"

// Hotbar에 보관한 뒤 기존 인프라 수리 입력으로 사용하는 소모품 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_RepairItemDefinition : public UMD_ConsumeItemDefinition
{
	GENERATED_BODY()

public:
	// 수리 가능한 인프라 종류를 식별하는 기능 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Repair")
	FGameplayTag RepairToolTag;
};
