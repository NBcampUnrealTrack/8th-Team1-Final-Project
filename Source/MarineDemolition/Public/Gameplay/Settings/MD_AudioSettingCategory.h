#pragma once

#include "CoreMinimal.h"
#include "AudioCaptureBlueprintLibrary.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_AudioSettingCategory.generated.h"

// 출력 장치 전환 완료 결과와 실제 사용 중인 장치 ID를 UI에 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnAudioOutputDeviceChanged, bool, bWasSuccessful, FString, CurrentDeviceId);

// 소리 설정 데이터와 유효 범위 검증을 담당한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_AudioSettingCategory : public UMD_SettingCategoryBase
{
	GENERATED_BODY()

public:
	// 소리 설정값을 지원 범위로 제한한다.
	virtual void ApplySettings() override;

	// 소리 설정을 구조체 기본값으로 복원한다.
	virtual void ResetToDefaults() override;

	// 현재 소리 값을 InstancedStruct 저장 데이터로 만든다.
	virtual FInstancedStruct MakeSaveData() override;

	// InstancedStruct에서 소리 값을 복원한다.
	virtual bool LoadSaveData(FInstancedStruct SaveData) override;

	// 소리 카테고리 식별자를 반환한다.
	virtual EMD_SettingsCategory GetCategory() override;

public:
	// 현재 편집 중인 소리 설정값을 보관한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MarineDemolition|Settings|Audio")
	FMD_AudioUserSettings Settings;

	// 출력 장치 비동기 전환 결과를 UI에 알린다.
	UPROPERTY(BlueprintAssignable, Category = "MarineDemolition|Settings|Audio")
	FMD_OnAudioOutputDeviceChanged OnOutputDeviceChanged;

public:
	// 현재 시스템에서 사용할 수 있는 출력 장치 목록을 비동기로 요청한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings|Audio")
	void GetAvailableOutputDevices(FOnAudioOutputDevicesObtained OnDevicesObtained);

	// 현재 시스템에서 사용할 수 있는 입력 장치 목록을 비동기로 요청한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings|Audio")
	void GetAvailableInputDevices(FOnAudioInputDevicesObtained OnDevicesObtained);

	// UI에서 선택한 출력 장치 ID를 현재 소리 설정에 저장한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings|Audio")
	void SetOutputDeviceId(FString DeviceId);

	// UI에서 선택한 입력 장치 ID를 현재 소리 설정에 저장한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings|Audio")
	void SetInputDeviceId(FString DeviceId);

	// 현재 선택된 출력 장치 ID를 직접 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	FORCEINLINE FString GetOutputDeviceId() { return Settings.OutputDeviceId; }

	// 현재 선택된 입력 장치 ID를 직접 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	FORCEINLINE FString GetInputDeviceId() { return Settings.InputDeviceId; }

protected:
	// AudioMixer 출력 장치 전환 결과를 반영하고 UI에 알린다.
	UFUNCTION()
	void HandleOutputDeviceSwapCompleted(const FSwapAudioOutputResult& SwapResult);
};
