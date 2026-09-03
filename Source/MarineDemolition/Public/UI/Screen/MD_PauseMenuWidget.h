#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "MD_PauseMenuWidget.generated.h"

class AMD_GameStateBase;
class UMD_PlayerInfoWidget;
class UTextBlock;
class UVerticalBox;

/** 일시 정지 화면에서 현재 접속 플레이어 목록을 구성하는 위젯 */
UCLASS()
class MARINEDEMOLITION_API UMD_PauseMenuWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;

protected:
	// 현재 세션의 참가용 방 코드 표시
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_RoomCode;

	// 현재 접속 플레이어 정보 카드의 세로 배치
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VB_PlayerList;

	// 플레이어별 정보 카드 Blueprint 클래스 저장
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Pause Menu")
	TSubclassOf<UMD_PlayerInfoWidget> PlayerInfoWidgetClass;

	// GameState 교체 이벤트를 구독한 월드의 약한 참조
	TWeakObjectPtr<UWorld> BoundWorld;

	// 플레이어 목록 변경 이벤트를 구독한 프로젝트 GameState의 약한 참조
	TWeakObjectPtr<AMD_GameStateBase> BoundGameState;

public:
	// 현재 GameState의 PlayerArray 기준 플레이어 정보 카드 목록 재구성
	UFUNCTION(BlueprintCallable, Category = "MD|Pause Menu")
	void BuildPlayerList();

	// 현재 멀티플레이 세션의 방 코드 갱신
	UFUNCTION(BlueprintCallable, Category = "MD|Pause Menu")
	void RefreshRoomCode();

protected:
	// 현재 월드와 GameState의 플레이어 목록 변경 이벤트 구독
	void BindPlayerListEvents();

	// 구독 중인 월드와 GameState 이벤트 해제
	void UnbindPlayerListEvents();

	// 월드의 GameState 설정 또는 교체 시 목록 이벤트 구독 대상 갱신
	void HandleGameStateSet(AGameStateBase* GameState);
};
