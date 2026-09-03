#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "MD_TutorialGuideWidget.generated.h"

class AMD_TutorialWallSpawner;
class UTextBlock;
class UWidget;

/**
 * 튜토리얼 진행기가 내보내는 안내 문구를 표시하는 HUD 위젯.
 *
 * UMD_HUDQuestWidget과 같은 구성이다 — HUD 안에 중첩되는 UUserWidget이고,
 * 대상(여기서는 스포너)을 찾을 때까지 주기적으로 다시 시도한다.
 * 진행기가 없는 맵에서는 조용히 접힌 채로 있는다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_TutorialGuideWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * 안내가 바뀔 때. 애니메이션과 서술/조작안내 스타일 분기는 WBP가 붙인다.
	 * bIsNarrative 는 도입부처럼 플레이어 행동을 기다리지 않는 장에서 true 다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Tutorial")
	void OnGuidanceUpdated(const FText& Title, const FText& Body, bool bHasGuidance, bool bIsNarrative);

	/** 일회성 경고가 떴을 때 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Tutorial")
	void OnNoticeShown(const FText& Message);

	/** 화자 이름. 문구에 화자가 없으면(조작 안내) 접힌다 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TB_Speaker;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TB_Title;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TB_Body;

	/** "스페이스바 — 넘기기". 넘길 수 없는 단계에서는 접힌다 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TB_SkipHint;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TB_Notice;

	/** 경고 문구를 감싸는 요소. 없으면 TB_Notice를 직접 숨긴다 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Panel_Notice;

	/** 경고를 띄워 두는 시간 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Tutorial",
		meta = (ClampMin = "0.5", Units = "s"))
	float NoticeDisplaySeconds = 3.0f;

	/** 진행기를 아직 못 찾았을 때 다시 시도하는 주기 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Tutorial",
		meta = (ClampMin = "0.1", Units = "s"))
	float SpawnerBindRetryInterval = 0.5f;

private:
	/** 월드에서 진행기를 찾아 연결한다. 성공하면 재시도 타이머를 끈다 */
	void TryBindSpawner();

	UFUNCTION()
	void HandleGuidanceChanged(const FText& Title, const FText& Body);

	UFUNCTION()
	void HandleNotice(const FText& Message);

	/** 현재 상태를 위젯에 반영. 안내가 없으면 전체를 접는다 */
	void ApplyGuidance(const FText& Title, const FText& Body, bool bHasGuidance);

	void HideNotice();

	UPROPERTY()
	TWeakObjectPtr<AMD_TutorialWallSpawner> BoundSpawner;

	FTimerHandle BindRetryTimerHandle;
	FTimerHandle NoticeHideTimerHandle;
};
