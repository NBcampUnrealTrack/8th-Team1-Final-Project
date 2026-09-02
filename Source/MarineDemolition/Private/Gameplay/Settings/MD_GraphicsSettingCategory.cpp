#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"

#include "GameFramework/GameUserSettings.h"

void UMD_GraphicsSettingCategory::ApplySettings()
{
	// 그래픽 품질 단계를 엔진이 지원하는 범위로 제한한다.
	Settings.OverallQuality = FMath::Clamp(Settings.OverallQuality, 0, 4);
	// 프레임 제한을 무제한 또는 지원 범위 안의 값으로 정리한다.
	Settings.FrameRateLimit = FMath::Clamp(Settings.FrameRateLimit, 0.0f, 360.0f);
	// 현재 플랫폼의 엔진 사용자 설정 객체를 찾는다.
	UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings();
	// 엔진 사용자 설정을 사용할 수 없으면 적용을 중단한다.
	if (IsValid(GameUserSettings) == false)
	{
		return;
	}
	// 선택한 전체 품질 단계를 모든 확장성 항목에 적용한다.
	GameUserSettings->SetOverallScalabilityLevel(Settings.OverallQuality);
	// 선택한 수직 동기화 사용 여부를 적용한다.
	GameUserSettings->SetVSyncEnabled(Settings.bUseVSync);
	// 선택한 최대 프레임 속도를 적용한다.
	GameUserSettings->SetFrameRateLimit(Settings.FrameRateLimit);
	// 변경한 엔진 설정을 실행하고 GameUserSettings 설정 파일에도 저장한다.
	GameUserSettings->ApplySettings(false);
}

void UMD_GraphicsSettingCategory::ResetToDefaults()
{
	// 그래픽 설정값을 구조체에 정의된 프로젝트 기본값으로 교체한다.
	Settings = FMD_GraphicsSettings();
}

FInstancedStruct UMD_GraphicsSettingCategory::MakeSaveData()
{
	// 현재 그래픽 설정과 실제 구조체 타입을 포함하는 InstancedStruct를 생성한다.
	FInstancedStruct InstancedSaveData = FInstancedStruct::Make(Settings);
	// 생성한 저장 데이터를 설정 매니저에 전달한다.
	return InstancedSaveData;
}

bool UMD_GraphicsSettingCategory::LoadSaveData(FInstancedStruct SaveData)
{
	// 저장 데이터가 그래픽 설정 형식인지 확인한다.
	bool bIsGraphicsData = SaveData.GetScriptStruct() == FMD_GraphicsSettings::StaticStruct();
	// 다른 카테고리의 저장 데이터이면 복원을 중단한다.
	if (bIsGraphicsData == false)
	{
		return false;
	}
	// 검증한 InstancedStruct에서 그래픽 설정 데이터를 읽는다.
	FMD_GraphicsSettings* GraphicsSaveData = SaveData.GetMutablePtr<FMD_GraphicsSettings>();
	// 저장 데이터 주소를 얻지 못하면 복원을 중단한다.
	if (GraphicsSaveData == nullptr)
	{
		return false;
	}
	// 저장된 그래픽 설정을 현재 값으로 복원한다.
	Settings = *GraphicsSaveData;
	// 그래픽 설정 복원이 완료되었음을 반환한다.
	return true;
}

EMD_SettingsCategory UMD_GraphicsSettingCategory::GetCategory()
{
	// 이 인스턴스가 그래픽 설정을 담당함을 직접 반환한다.
	return EMD_SettingsCategory::Graphics;
}
