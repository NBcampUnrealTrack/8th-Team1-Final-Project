// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

// 호출 지점의 위치 정보를 문자열을 반환하는 매크로
// Ex) [AMDMyActor::BeginPlay:10]
#define UE_CALL_LOCATIONINFO FString::Printf(TEXT("[%s:%d]"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)

// 호출 시점의 로컬 시간 정보를 문자열을 반환하는 매크로
// Ex) [14:35:10.527]
#define UE_CALL_TIMEINFO FDateTime::Now().ToString(TEXT("[%H:%M:%S.%s]"))

DECLARE_LOG_CATEGORY_EXTERN(MarineDemolition, Log, All);
