// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_UIBlueprintFunctionLibrary.generated.h"

class UCommonActivatableWidget;

/** Project Settings의 UI 태그 등록 정보를 Blueprint에 제공한다. */
UCLASS()
class MARINEDEMOLITION_API UMD_UIBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 화면 요청 태그에 등록된 CommonActivatableWidget 소프트 클래스를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MD | UI | Settings")
	static TSoftClassPtr<UCommonActivatableWidget> GetWidgetClassByTag(FGameplayTag WidgetTag);
};
