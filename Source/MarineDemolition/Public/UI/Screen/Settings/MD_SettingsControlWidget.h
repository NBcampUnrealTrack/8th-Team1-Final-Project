#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "UI/Screen/Settings/MD_SettingsCategoryWidgetBase.h"
#include "MD_SettingsControlWidget.generated.h"

class UCheckBox;
class UInputKeySelector;
class USlider;
class UTextBlock;

// 조작 설정의 미적용 편집값과 카테고리별 저장을 관리하는 화면
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_SettingsControlWidget : public UMD_SettingsCategoryWidgetBase
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	FORCEINLINE virtual EMD_SettingsCategory GetSettingsCategory() override { return EMD_SettingsCategory::Controls; }
	virtual bool CommitPendingSettings() override;
	virtual bool ReloadPendingSettings() override;
	virtual void ResetPendingSettings() override;

protected:
	// WBP 마우스 감도 슬라이더 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<USlider> Slider_MouseSensitivity;

	// WBP 세로 시점 반전 선택 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> TB_Reversal;

	// 상호작용 키를 표시하거나 BP에서 변경할 선택기
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UInputKeySelector> KeySelector_Interact;

	// 점프 키를 표시하거나 BP에서 변경할 선택기
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UInputKeySelector> KeySelector_Jump;

	// 도구 사용 키를 표시하거나 BP에서 변경할 선택기
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UInputKeySelector> KeySelector_UseItem;

	// 음성 채팅 키의 선택적 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UInputKeySelector> KeySelector_PushToTalk;

	// 현재 마우스 감도 배율의 숫자 표시
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Value_MouseSensitivity;

	// 적용 전까지 실제 카테고리와 분리된 화면 기준값
	UPROPERTY(Transient)
	FMD_ControlSettings PendingSettings;

public:
	// 생략된 컨트롤의 기준값을 보존한 현재 화면 선택값 수집
	UFUNCTION(BlueprintPure, Category = "MD|Settings|Controls")
	FMD_ControlSettings GetPendingSettings();

protected:
	// 실제 설정 변경 없는 컨트롤 표시 갱신
	void RefreshDisplay();

	// 키 선택기 부재 시 기존 값을 보존하는 단일 입력 키 수집
	FKey GetSelectedKey(UInputKeySelector* KeySelector, FKey PreviousKey);

	// 단일 입력 키를 수정 키 없는 선택 상태로 표시
	void RefreshKeySelector(UInputKeySelector* KeySelector, FKey Key);
};
