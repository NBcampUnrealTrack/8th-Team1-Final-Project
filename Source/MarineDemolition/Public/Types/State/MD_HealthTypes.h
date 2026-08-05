// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MD_HealthTypes.generated.h"

/* 캐릭터 상태. HP 100~31 건강. 30 ~ 1 부상, 0 사망 */

UENUM(BlueprintType)
enum class EMD_HealthState : uint8
{
	Healthy UMETA(DisplayName = "건강"),
	Injured UMETA(DisplayName = "부상"),
	Dead    UMETA(DisplayName = "사망")
};