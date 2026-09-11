// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_TalkNotificationWidget.generated.h"

// 표시할 텍스트를 Blueprint에 전달하는 음성 알림 기반 클래스
UCLASS()
class MARINEDEMOLITION_API UMD_TalkNotificationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// UHT 생성 시그니처에 맞는 상수 참조로 텍스트를 전달하여 Blueprint 이름 영역에 적용
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Voice Chat")
	void SetText(const FText& Text);
};
