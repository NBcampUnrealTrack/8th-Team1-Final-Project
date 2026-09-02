#include "Gameplay/Settings/MD_AudioSettingCategory.h"

void UMD_AudioSettingCategory::ApplySettings()
{
	// 전체 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.MasterVolume = FMath::Clamp(Settings.MasterVolume, 0.0f, 1.0f);
	// 배경 음악 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.MusicVolume = FMath::Clamp(Settings.MusicVolume, 0.0f, 1.0f);
	// 효과음 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.EffectsVolume = FMath::Clamp(Settings.EffectsVolume, 0.0f, 1.0f);
	// 음성 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.VoiceVolume = FMath::Clamp(Settings.VoiceVolume, 0.0f, 1.0f);
}

void UMD_AudioSettingCategory::ResetToDefaults()
{
	// 소리 설정값을 구조체에 정의된 프로젝트 기본값으로 교체한다.
	Settings = FMD_AudioUserSettings();
}

FInstancedStruct UMD_AudioSettingCategory::MakeSaveData()
{
	// 현재 소리 설정과 실제 구조체 타입을 포함하는 InstancedStruct를 생성한다.
	FInstancedStruct InstancedSaveData = FInstancedStruct::Make(Settings);
	// 생성한 저장 데이터를 설정 매니저에 전달한다.
	return InstancedSaveData;
}

bool UMD_AudioSettingCategory::LoadSaveData(FInstancedStruct SaveData)
{
	// 저장 데이터가 소리 설정 형식인지 확인한다.
	bool bIsAudioData = SaveData.GetScriptStruct() == FMD_AudioUserSettings::StaticStruct();
	// 다른 카테고리의 저장 데이터이면 복원을 중단한다.
	if (bIsAudioData == false)
	{
		return false;
	}
	// 검증한 InstancedStruct에서 소리 설정 데이터를 읽는다.
	FMD_AudioUserSettings* AudioSaveData = SaveData.GetMutablePtr<FMD_AudioUserSettings>();
	// 저장 데이터 주소를 얻지 못하면 복원을 중단한다.
	if (AudioSaveData == nullptr)
	{
		return false;
	}
	// 저장된 소리 설정을 현재 값으로 복원한다.
	Settings = *AudioSaveData;
	// 소리 설정 복원이 완료되었음을 반환한다.
	return true;
}

EMD_SettingsCategory UMD_AudioSettingCategory::GetCategory()
{
	// 이 인스턴스가 소리 설정을 담당함을 직접 반환한다.
	return EMD_SettingsCategory::Audio;
}
