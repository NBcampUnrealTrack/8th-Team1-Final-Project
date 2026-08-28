#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "MD_PauseMenuWidget.generated.h"

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

protected:
	// 현재 세션의 참가용 방 코드를 표시한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_RoomCode;

	// 현재 접속 플레이어의 정보 카드를 세로로 나열한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> VB_PlayerList;

	// 플레이어마다 목록에 생성할 정보 카드 Blueprint 클래스를 저장한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Pause Menu")
	TSubclassOf<UMD_PlayerInfoWidget> PlayerInfoWidgetClass;

public:
	// 현재 GameState의 PlayerArray를 기준으로 플레이어 정보 카드 목록을 다시 구성한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Pause Menu")
	void BuildPlayerList();

	// 현재 멀티플레이 세션의 방 코드를 화면에 다시 표시한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Pause Menu")
	void RefreshRoomCode();
};
