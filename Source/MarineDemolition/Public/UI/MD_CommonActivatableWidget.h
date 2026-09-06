// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "MD_CommonActivatableWidget.generated.h"

/** 프로젝트 화면의 공통 활성화 정책을 제공하는 Blueprint 기반 클래스다. */
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_CommonActivatableWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	// World Travel 시작 시 이 화면을 부모에서 제거할지 결정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|UI|Navigation")
	bool RemoveOnWorldTravel = true;

public:
	// World Travel 시작 시 화면 제거 여부를 반환한다.
	UFUNCTION(BlueprintPure, Category = "MD|UI|Navigation")
	FORCEINLINE bool ShouldRemoveOnWorldTravel() const { return RemoveOnWorldTravel; }
};
