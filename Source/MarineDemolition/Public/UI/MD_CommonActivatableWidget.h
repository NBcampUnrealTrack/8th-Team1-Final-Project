// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "GameplayTags.h"
#include "MD_CommonActivatableWidget.generated.h"

/** 프로젝트 화면의 공통 활성화 정책을 제공하는 Blueprint 기반 클래스다. */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_CommonActivatableWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()
	
protected:
	// 화면이 배치될 CommonUI 레이어를 자산별로 설정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|UI|Navigation")
	FGameplayTag TargetLayerTag;

public:
	UFUNCTION(BlueprintPure, Category = "MD|UI|Navigation")
	FORCEINLINE FGameplayTag GetTargetLayerTag() const { return TargetLayerTag; }
};
