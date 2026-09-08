#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_SettingsCategoryWidgetBase.generated.h"

class UMD_SettingsManager;

// 카테고리 화면의 미적용 편집값을 설정 매니저와 연결하는 공통 기반
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_SettingsCategoryWidgetBase : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	// 현재 화면을 등록한 게임 인스턴스 설정 매니저의 약한 참조
	TWeakObjectPtr<UMD_SettingsManager> RegisteredSettingsManager;

protected:
	// 현재 화면이 관리하는 고정 설정 카테고리 식별자
	virtual EMD_SettingsCategory GetSettingsCategory() PURE_VIRTUAL(UMD_SettingsCategoryWidgetBase::GetSettingsCategory, return EMD_SettingsCategory::Graphics;);

	// 현재 화면 선택값을 해당 카테고리 인스턴스로 전달
	virtual bool CommitPendingSettings() PURE_VIRTUAL(UMD_SettingsCategoryWidgetBase::CommitPendingSettings, return false;);

	// 해당 카테고리 인스턴스 값으로 화면의 미적용 편집값 복원
	virtual bool ReloadPendingSettings() PURE_VIRTUAL(UMD_SettingsCategoryWidgetBase::ReloadPendingSettings, return false;);

	// 저장과 적용 없이 화면의 미적용 편집값만 프로젝트 기본값으로 복원
	virtual void ResetPendingSettings() PURE_VIRTUAL(UMD_SettingsCategoryWidgetBase::ResetPendingSettings, );

	friend class UMD_SettingsManager;
};
