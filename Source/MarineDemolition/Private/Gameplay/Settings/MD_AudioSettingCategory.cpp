#include "Gameplay/Settings/MD_AudioSettingCategory.h"

#include "AudioMixerBlueprintLibrary.h"
#include "VoiceChat.h"

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
	// 비동기 출력 장치 전환 결과를 현재 인스턴스에서 처리할 델리게이트를 생성한다.
	FOnCompletedDeviceSwap OnCompletedDeviceSwap;
	// 실제 전환 결과와 사용 장치를 반영할 완료 함수를 연결한다.
	OnCompletedDeviceSwap.BindDynamic(this, &UMD_AudioSettingCategory::HandleOutputDeviceSwapCompleted);
	// 선택한 출력 장치로 전환하며 빈 ID이면 시스템 기본 출력 장치를 사용한다.
	UAudioMixerBlueprintLibrary::SwapAudioOutputDevice(this, Settings.OutputDeviceId, OnCompletedDeviceSwap);
	// 현재 실행 환경에 등록된 VoiceChat 구현을 찾는다.
	IVoiceChat* VoiceChat = IVoiceChat::Get();
	// VoiceChat 구현이 제공되는 경우 선택한 입력 장치로 실제 캡처 장치를 전환한다.
	if (VoiceChat != nullptr)
	{
		VoiceChat->SetInputDeviceId(Settings.InputDeviceId);
	}
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

void UMD_AudioSettingCategory::GetAvailableOutputDevices(FOnAudioOutputDevicesObtained OnDevicesObtained)
{
	// AudioMixer에 현재 운영체제의 출력 장치 목록을 비동기로 요청한다.
	UAudioMixerBlueprintLibrary::GetAvailableAudioOutputDevices(this, OnDevicesObtained);
}

void UMD_AudioSettingCategory::GetAvailableInputDevices(FOnAudioInputDevicesObtained OnDevicesObtained)
{
	// AudioCapture에 현재 운영체제의 입력 장치 목록을 비동기로 요청한다.
	UAudioCaptureBlueprintLibrary::GetAvailableAudioInputDevices(this, OnDevicesObtained);
}

void UMD_AudioSettingCategory::SetOutputDeviceId(FString DeviceId)
{
	// UI에서 선택한 출력 장치 식별자를 현재 설정에 보관한다.
	Settings.OutputDeviceId = MoveTemp(DeviceId);
}

void UMD_AudioSettingCategory::SetInputDeviceId(FString DeviceId)
{
	// UI에서 선택한 입력 장치 식별자를 현재 설정에 보관한다.
	Settings.InputDeviceId = MoveTemp(DeviceId);
}

void UMD_AudioSettingCategory::HandleOutputDeviceSwapCompleted(const FSwapAudioOutputResult& SwapResult)
{
	// AudioMixer가 요청한 출력 장치 전환에 성공했는지 확인한다.
	bool bWasSuccessful = SwapResult.Result == ESwapAudioOutputDeviceResultState::Success;
	// 전환 실패 시 저장값이 실제 사용 중인 출력 장치를 나타내도록 복구한다.
	if (bWasSuccessful == false)
	{
		Settings.OutputDeviceId = SwapResult.CurrentDeviceId;
	}
	// 출력 장치 전환 결과와 실제 사용 중인 장치를 UI에 알린다.
	OnOutputDeviceChanged.Broadcast(bWasSuccessful, SwapResult.CurrentDeviceId);
}
