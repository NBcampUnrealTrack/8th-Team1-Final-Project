// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonActivatableWidget.h"
#include "MD_HUDWidget.generated.h"

class UMD_TalkNotificationWidget;
class UVerticalBox;

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_HUDWidget : public UMD_CommonActivatableWidget
{
	GENERATED_BODY()

public:
	// GameHUD가 최상단 화면일 때 게임플레이 입력을 유지한다.
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

protected:
	virtual void NativeConstruct() override;

	virtual void NativeDestruct() override;

protected:
	// 생성된 발화 알림을 배치하는 전용 목록
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VB_TalkList;

	// HUD에서 대리 생성할 발화자 알림 Blueprint 클래스
	UPROPERTY(EditDefaultsOnly, Category = "Voice Chat")
	TSoftClassPtr<UMD_TalkNotificationWidget> TalkNotificationClass;

public:
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void HandlePlayerJoinAndExit(const FText& PlayerName, bool bIsJoin);

	// 서버 판정 한 줄 알림(슬롯 가득 참·전원 없음 등). WBP_PlayerNotification 계열에 표시하도록 BP에서 구현한다
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void HandleHUDMessage(const FText& Message);

	// 알림 위젯을 생성하여 HUD 목록에 추가한 후 반환
	UMD_TalkNotificationWidget* CreateTalkNotification();
};
