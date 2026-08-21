// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "MD_WidgetStack.generated.h"

/** 화면 제거 시 내부 위젯 풀까지 초기화할 수 있는 프로젝트 전용 Stack이다. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_WidgetStack : public UCommonActivatableWidgetStack
{
	GENERATED_BODY()

public:
	// 현재 화면을 즉시 제거하고 다음 Push가 새 위젯 인스턴스를 생성하도록 내부 풀을 초기화한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	void ClearWidgetsAndResetPool();
};
