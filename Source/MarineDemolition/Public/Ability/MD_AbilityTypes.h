// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MD_AbilityTypes.generated.h"

class UGameplayAbility;

/**
 * 캐릭터가 시작할 때 지급할 Ability 한 개의 설정.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_StartupAbilityEntry
{
	GENERATED_BODY()

public:
	FMD_StartupAbilityEntry() 
	: AbilityClass(nullptr), AbilityLevel(1) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Ability")
	TSubclassOf<UGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Ability", meta = (ClampMin = "1"))
	int32 AbilityLevel;
};
