#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "AudioCaptureBlueprintLibrary.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_AudioSettingCategory.generated.h"

class IVoiceCapture;

// 출력 장치 전환 완료 결과와 실제 사용 중인 장치 ID를 UI에 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnAudioOutputDeviceChanged, bool, bWasSuccessful, FString, CurrentDeviceId);

// 실제 입력 장치 전환 결과와 적용된 장치 ID 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMD_OnAudioInputDeviceChanged, bool, bWasSuccessful, FString, CurrentDeviceId);

// 소리 설정 데이터와 유효 범위 검증을 담당한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_AudioSettingCategory : public UMD_SettingCategoryBase
{
	GENERATED_BODY()


public:
	virtual void BeginDestroy() override;

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

	// 실제 음성 캡처 교체 완료 결과 전달
	UPROPERTY(BlueprintAssignable, Category = "MarineDemolition|Settings|Audio")
	FMD_OnAudioInputDeviceChanged OnInputDeviceChanged;

protected:
	// 적용 버튼 또는 저장값 복원으로 확정된 출력 선택
	FString RequestedOutputDeviceId;
	// 현재 전환 중인 출력 선택과 다음 적용 선택의 분리
	FString ActiveOutputDeviceId;
	// 마지막으로 오디오 스레드에서 확인한 실제 출력 장치
	FString CurrentOutputDeviceId;
	// 트래블 전 출력 요청의 실제 대상 월드
	TWeakObjectPtr<UWorld> OutputRequestWorld;
	// 미적용 편집값과 분리된 입력 선택
	FString RequestedInputDeviceId;
	// 마지막으로 실제 적용된 입력 장치 식별자
	FString AppliedInputDeviceId;
	// 음성 엔진 재생성 시 입력 재적용을 위한 약한 캡처 참조
	TWeakPtr<IVoiceCapture> AppliedVoiceCapture;
	// 일시정지 중에도 출력 전환 완료를 확인하는 코어 틱 핸들
	FTSTicker::FDelegateHandle OutputDeviceTickerHandle;
	// 오래된 비동기 출력 응답을 구분하는 요청 번호
	uint32 OutputDeviceRequestSerial = 0;
	// 실제 장치 확인이 끝나지 않은 출력 요청 상태
	bool bOutputDevicePending = false;
	// 오디오 스레드 조회의 중복 예약 방지 상태
	bool bOutputDeviceQueryPending = false;
	// 엔진이 접수한 요청의 중복 전환 방지 상태
	bool bOutputDeviceRequestAccepted = false;
	// 장치 응답 유실에 대한 무한 대기 방지 시각
	double OutputDeviceRequestStartTime = 0.0;

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

	// 실제 음성 처리나 저장 없이 입력 감도 설정값만 갱신
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings|Audio")
	void SetVoiceInputSensitivity(float Sensitivity);

	// 실제 송신 모드 변경이나 저장 없이 Push to Talk 설정값만 갱신
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|Settings|Audio")
	void SetPushToTalkEnabled(bool bEnabled);

	// 현재 선택된 출력 장치 ID를 직접 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	FORCEINLINE FString GetOutputDeviceId() { return Settings.OutputDeviceId; }

	// 현재 선택된 입력 장치 ID를 직접 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	FORCEINLINE FString GetInputDeviceId() { return Settings.InputDeviceId; }

	// 현재 음성 입력 감도 설정값 반환
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	FORCEINLINE float GetVoiceInputSensitivity() { return Settings.VoiceInputSensitivity; }

	// 현재 Push to Talk 사용 설정값 반환
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|Settings|Audio")
	FORCEINLINE bool IsPushToTalkEnabled() { return Settings.bUsePushToTalk; }

	// 월드 오디오 장치 준비 이후 확정된 입출력 선택 재적용
	void RefreshAudioDevices();

	// 로컬 음성 엔진의 지연 초기화 이후 확정된 입력 선택 재적용
	void ApplyInputDeviceSettings();

protected:
	// 오디오 스레드에서 확인한 실제 출력 장치 상태 반영
	void HandleOutputDeviceChecked(uint32 RequestSerial, bool bSuccessful, bool bAccepted, FString CurrentDeviceId);

	// 실제 출력 장치 ID를 확인하며 미접수 요청을 제한 시간 내 재시도
	bool TickOutputDeviceSwap(float DeltaTime);
};
