#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_KeyGuideWidget.generated.h"

class UMD_SettingsManager;
class UTextBlock;

// 적용된 음성 채팅 키를 HUD 안내에 표시하는 위젯 타입
// 기존 KeyGuide Blueprint의 설정 연동 기반
UCLASS()
class MARINEDEMOLITION_API UMD_KeyGuideWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	// 그림자 머티리얼이 적용된 키 표시
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextBlock;

	// 그림자와 동일한 키를 표시할 본문
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextBlock_153;

	// 화면 제거 시 구독을 해제할 설정 매니저
	TWeakObjectPtr<UMD_SettingsManager> RegisteredSettingsManager;

	// 소리 설정 적용 시 확정된 Push to Talk 사용 여부
	bool bUsePushToTalk = true;

	// 음성 입력 시작부터 해제 또는 취소까지의 키 유지 상태
	bool bVoiceChatKeyPressed = false;

protected:
	// 실제 적용된 키를 본문과 그림자에 동시 반영
	void RefreshVoiceChatKey();

	// 소리 설정에 적용된 음성 입력 모드 반영
	void RefreshVoiceChatMode();

	// 음성 입력 모드와 키 유지 상태에 따른 전체 안내 표시 갱신
	void RefreshGuideVisibility();

	// Blueprint 음성 입력 이벤트의 누름 및 해제 상태 반영
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI|KeyGuide", meta = (BlueprintProtected))
	void SetVoiceChatKeyPressed(bool bPressed);

	// 조작 및 소리 설정 적용에 따른 키 안내 갱신
	UFUNCTION()
	void HandleSettingsApplied(EMD_SettingsCategory Category);
};
