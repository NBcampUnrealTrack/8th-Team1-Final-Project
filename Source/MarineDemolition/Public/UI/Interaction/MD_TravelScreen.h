// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_TravelScreen.generated.h"

class AMD_TravelConsole;

// 항해 화면에 상호작용 콘솔을 전달하는 초기화 계약
UINTERFACE(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_TravelScreen : public UInterface
{
    GENERATED_BODY()
};

// 화면 구성과 관계없이 콘솔 초기화를 수신하는 인터페이스
class MARINEDEMOLITION_API IMD_TravelScreen
{
    GENERATED_BODY()

public:
    // 화면 활성화 전에 새 항해 탭에 사용할 콘솔 전달
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "MD|Travel")
    void InitializeTravelScreen(AMD_TravelConsole* InConsole);
};
