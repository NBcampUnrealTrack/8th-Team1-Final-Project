#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "AudioCaptureBlueprintLibrary.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Gameplay/Settings/MD_SettingCategoryBase.h"
#include "Gameplay/Settings/MD_SettingsTypes.h"
#include "MD_AudioSettingCategory.generated.h"

class IVoiceCapture;
class FVoiceEngineImpl;
struct FMD_AudioOutputRequestState;

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
	// 월드 준비 전 대기와 대상 월드 소멸을 구분하는 요청 귀속 상태
	bool bOutputRequestWorldAssigned = false;
	// 최초 게임 월드의 출력 처리를 시작한 이후 초기 임시 월드 예외를 차단하는 상태
	bool bHasOutputRequestStarted = false;
	// 출력 작업 실행과 게임 스레드 취소를 직렬화하는 요청별 공유 상태
	TSharedPtr<FMD_AudioOutputRequestState, ESPMode::ThreadSafe> OutputDeviceRequestState;
	// 미적용 편집값과 분리된 입력 선택
	FString RequestedInputDeviceId;
	// 마지막으로 실제 적용된 입력 장치 식별자
	FString AppliedInputDeviceId;
	// 음성 엔진 재생성 시 입력 재적용을 위한 약한 캡처 참조
	TWeakPtr<IVoiceCapture> AppliedVoiceCapture;
	// 월드 종료 시 새 엔진 생성 없이 기존 원격 음성 재생을 정리할 약한 참조
	TWeakPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> AppliedVoiceEngine;
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
	// 월드 전환과 종료 중 입출력 장치 변경을 차단하는 상태
	bool bAudioDevicesSuspended = false;
	// 현재 게임 인스턴스의 심리스 이동 시작 구독 핸들
	FDelegateHandle SeamlessTravelStartHandle;
	// 현재 게임 인스턴스의 월드 종료 시작 구독 핸들
	FDelegateHandle WorldBeginTearDownHandle;

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

	// 최초 설정과 사용자 확정에 따른 입출력 선택 적용 예약
	void RefreshAudioDevices();

	// 월드 이동 전에 대기 중인 출력 작업 취소와 입출력 변경 차단
	void SuspendAudioDeviceRequests();

	// 목적지 월드 시작 후 장치 변경 차단 해제와 입력 선택 복원
	void RestoreWorldInputDevice();

	// 설정 카테고리 종료 시 월드 구독 해제와 장치 작업 취소
	void ShutdownAudioDevices();

	// 로컬 음성 엔진의 지연 초기화 이후 확정된 입력 선택 재적용
	void ApplyInputDeviceSettings();

protected:
	// 현재 요청의 오디오 작업과 틱 및 비동기 응답 일괄 취소
	void CancelOutputDeviceRequest();

	// 소유 게임 인스턴스의 심리스 이동 시작에 따른 장치 요청 중단
	void HandleSeamlessTravelStart(UWorld* World, const FString& DestinationUrl);

	// 소유 게임 인스턴스의 월드 종료 시작에 따른 장치 요청 중단
	void HandleWorldBeginTearDown(UWorld* World);

	// 오디오 스레드에서 확인한 실제 출력 장치 상태 반영
	void HandleOutputDeviceChecked(uint32 RequestSerial, bool bSuccessful, bool bAccepted, FString CurrentDeviceId);

	// 실제 출력 장치 ID를 확인하며 미접수 요청을 제한 시간 내 재시도
	bool TickOutputDeviceSwap(float DeltaTime);
};
