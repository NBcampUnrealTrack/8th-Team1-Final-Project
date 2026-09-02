#include "Gameplay/Settings/MD_ControlSettingCategory.h"

void UMD_ControlSettingCategory::ApplySettings()
{
	// 마우스 감도 배율을 게임에서 지원하는 범위로 제한한다.
	Settings.MouseSensitivity = FMath::Clamp(Settings.MouseSensitivity, 0.1f, 10.0f);
}

void UMD_ControlSettingCategory::ResetToDefaults()
{
	// 조작 설정값을 구조체에 정의된 프로젝트 기본값으로 교체한다.
	Settings = FMD_ControlSettings();
}

FInstancedStruct UMD_ControlSettingCategory::MakeSaveData()
{
	// 현재 조작 설정과 실제 구조체 타입을 포함하는 InstancedStruct를 생성한다.
	FInstancedStruct InstancedSaveData = FInstancedStruct::Make(Settings);
	// 생성한 저장 데이터를 설정 매니저에 전달한다.
	return InstancedSaveData;
}

bool UMD_ControlSettingCategory::LoadSaveData(FInstancedStruct SaveData)
{
	// 저장 데이터가 조작 설정 형식인지 확인한다.
	bool bIsControlData = SaveData.GetScriptStruct() == FMD_ControlSettings::StaticStruct();
	// 다른 카테고리의 저장 데이터이면 복원을 중단한다.
	if (bIsControlData == false)
	{
		return false;
	}
	// 검증한 InstancedStruct에서 조작 설정 데이터를 읽는다.
	FMD_ControlSettings* ControlSaveData = SaveData.GetMutablePtr<FMD_ControlSettings>();
	// 저장 데이터 주소를 얻지 못하면 복원을 중단한다.
	if (ControlSaveData == nullptr)
	{
		return false;
	}
	// 저장된 조작 설정을 현재 값으로 복원한다.
	Settings = *ControlSaveData;
	// 조작 설정 복원이 완료되었음을 반환한다.
	return true;
}

EMD_SettingsCategory UMD_ControlSettingCategory::GetCategory()
{
	// 이 인스턴스가 조작 설정을 담당함을 직접 반환한다.
	return EMD_SettingsCategory::Controls;
}
