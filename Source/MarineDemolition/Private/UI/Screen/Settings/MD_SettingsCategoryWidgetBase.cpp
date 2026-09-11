#include "UI/Screen/Settings/MD_SettingsCategoryWidgetBase.h"

#include "Gameplay/Settings/MD_SettingsManager.h"

void UMD_SettingsCategoryWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// 현재 화면이 속한 게임 인스턴스의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 미리보기 등 설정 매니저가 없는 환경의 등록 생략
	if (IsValid(SettingsManager) == false)
	{
		return;
	}

	// 화면 종료 시 안전한 등록 해제를 위한 약한 참조 보관
	RegisteredSettingsManager = SettingsManager;
	// 현재 화면의 고정 카테고리를 기준으로 편집 화면 등록
	SettingsManager->RegisterSettingsWidget(GetSettingsCategory(), this);
}

void UMD_SettingsCategoryWidgetBase::NativeDestruct()
{
	// 게임 인스턴스 종료 전까지 유지된 설정 매니저 확인
	bool bManagerAvailable = RegisteredSettingsManager.IsValid();
	// 유효한 매니저에 한정한 현재 화면 등록 해제
	if (bManagerAvailable == true)
	{
		// 약한 참조에서 등록 대상 매니저 조회
		UMD_SettingsManager* SettingsManager = RegisteredSettingsManager.Get();
		// 동일 카테고리에 교체 등록된 다른 화면을 보존하는 등록 해제
		SettingsManager->UnregisterSettingsWidget(GetSettingsCategory(), this);
	}
	// 종료한 화면의 매니저 약한 참조 초기화
	RegisteredSettingsManager.Reset();

	Super::NativeDestruct();
}
