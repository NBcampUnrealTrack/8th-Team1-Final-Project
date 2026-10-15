#include "Gameplay/Settings/MD_AudioSettingCategory.h"

#include "AudioMixerBlueprintLibrary.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Settings/MD_AudioOutputRequestState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "Net/VoiceConfig.h"
#include "Online/Voice/MD_VoiceChatComponent.h"
#include "Online/Voice/MD_VoiceDeviceRouting.h"
#include "Util/MD_LogUtility.h"

void UMD_AudioSettingCategory::BeginDestroy()
{
	// 소유 인스턴스 종료 이후 월드 이벤트와 오디오 작업의 실행 차단
	ShutdownAudioDevices();
	Super::BeginDestroy();
}

void UMD_AudioSettingCategory::ApplySettings()
{
	// 반복 적용에도 한 번만 유지할 심리스 이동 시작 구독
	if (SeamlessTravelStartHandle.IsValid() == false)
	{
		// 음성 엔진 정리 전 현재 인스턴스의 장치 요청 중단 연결
		SeamlessTravelStartHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &UMD_AudioSettingCategory::HandleSeamlessTravelStart);
	}
	// 일반 월드 이동과 종료의 중복 없는 정리 구독
	if (WorldBeginTearDownHandle.IsValid() == false)
	{
		// 월드 해제 전 대기 중인 플랫폼 작업 취소 연결
		WorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UMD_AudioSettingCategory::HandleWorldBeginTearDown);
	}
	// 월드 정리 직전의 중복 없는 최종 음성 정리 구독
	if (WorldCleanupHandle.IsValid() == false)
	{
		// 심리스 이동의 옛 월드 정리 검증 전에 남은 원격 재생 해제 연결
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UMD_AudioSettingCategory::HandleWorldCleanup);
	}

	// 송신 모드 변경으로 시작될 캡처에도 동일한 입력 선택 적용
	RequestedInputDeviceId = Settings.InputDeviceId;
	// 적용 버튼에서 확정된 출력 선택 보관
	RequestedOutputDeviceId = Settings.OutputDeviceId;
	// 전체 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.MasterVolume = FMath::Clamp(Settings.MasterVolume, 0.0f, 1.0f);
	// 배경 음악 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.MusicVolume = FMath::Clamp(Settings.MusicVolume, 0.0f, 1.0f);
	// 효과음 음량을 UI에서 허용하는 범위로 제한한다.
	Settings.EffectsVolume = FMath::Clamp(Settings.EffectsVolume, 0.0f, 1.0f);
	// 구조체 직접 편집값과 저장 데이터의 입력 감도 범위 보정
	Settings.VoiceInputSensitivity = FMath::Clamp(Settings.VoiceInputSensitivity, 0.0f, 1.0f);
	// 출력 장치 적용 요청과 함께 확인할 사용자 설정값 기록
	UE_LOG(MarineDemolition, Display, TEXT("Audio settings apply requested: output='%s', input='%s', master=%.2f, music=%.2f, effects=%.2f"),
		*Settings.OutputDeviceId, *Settings.InputDeviceId, Settings.MasterVolume, Settings.MusicVolume, Settings.EffectsVolume);
	// 현재 월드의 오디오 처리 서브시스템 조회
	UWorld* World = GetWorld();
	// 전체 음량과 태그 기반 배경 음악 및 효과음의 개별 음량 적용
	if (IsValid(World) == true)
	{
		UMD_AudioSubsystem* AudioSubsystem = World->GetSubsystem<UMD_AudioSubsystem>();
		if (IsValid(AudioSubsystem) == true)
		{
			AudioSubsystem->ApplyUserVolumes(Settings.MasterVolume, Settings.MusicVolume, Settings.EffectsVolume);
		}
	}
	// 최대 감도에서 가장 낮아지는 음성 활성화 임계값 변환
	float VoiceActivationThreshold = FMath::Lerp(0.006f, 0.0f, Settings.VoiceInputSensitivity);
	// Steam 음성 캡처의 무음 감지 임계값 적용
	UVOIPStatics::SetMicThreshold(VoiceActivationThreshold);
	// 무음 감지는 통과했지만 고정 노이즈 게이트에 차단되는 저음량 입력 방지
	float VoiceNoiseGateThreshold = FMath::Lerp(0.008f, 0.0f, Settings.VoiceInputSensitivity);
	IConsoleVariable* NoiseGateVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicNoiseGateThreshold"));
	if (NoiseGateVariable != nullptr)
	{
		// 프로젝트 INI 기본값보다 우선하는 사용자 입력 감도 적용
		NoiseGateVariable->Set(VoiceNoiseGateThreshold, ECVF_SetByCode);
	}
	// 현재 로컬 플레이어의 음성 송신 모드 적용
	if (IsValid(World) == true)
	{
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (IsValid(PlayerController) == true)
		{
			// 다른 사용자의 컨트롤러에 로컬 마이크 설정을 적용하는 동작 방지
			bool bIsLocalController = PlayerController->IsLocalController();
			if (bIsLocalController == true)
			{
				// 실제 로컬 음성 송신을 담당하는 컴포넌트 조회
				UMD_VoiceChatComponent* VoiceComponent = PlayerController->FindComponentByClass<UMD_VoiceChatComponent>();
				if (IsValid(VoiceComponent) == true)
				{
					// 현재 송신 상태를 포함한 Push to Talk 모드 전환
					VoiceComponent->SetPushToTalk(Settings.bUsePushToTalk);
				}
			}
		}
	}
	// 현재 게임 오디오 장치와 온라인 음성 캡처에 선택 반영
	RefreshAudioDevices();
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

void UMD_AudioSettingCategory::SetVoiceInputSensitivity(float Sensitivity)
{
	// UI 입력값을 정규화 범위로 제한한 감도 설정값 보관
	Settings.VoiceInputSensitivity = FMath::Clamp(Sensitivity, 0.0f, 1.0f);
}

void UMD_AudioSettingCategory::SetPushToTalkEnabled(bool bEnabled)
{
	// UI에서 선택한 Push to Talk 사용 여부를 현재 편집값으로 보관
	Settings.bUsePushToTalk = bEnabled;
}

void UMD_AudioSettingCategory::RefreshAudioDevices()
{
	// 월드 전환과 종료 중 새로운 장치 작업 생성 차단
	if (bAudioDevicesSuspended == true)
	{
		// 목적지 월드의 입력 복원 전 적용 요청 종료
		return;
	}
	// 설정 적용 시점의 심리스 전환 여부 확인
	UWorld* World = GetWorld();
	// 이동 이벤트 등록 이전에 시작된 트래블의 장치 변경 차단
	if (IsValid(World) == true && World->IsInSeamlessTravel() == true)
	{
		// 기존 작업까지 포함한 트래블 중 장치 접근 중단
		SuspendAudioDeviceRequests();
		// 목적지 월드 시작까지 장치 적용 연기
		return;
	}
	// 준비된 게임 월드의 실제 온라인 음성 입력 캡처 적용
	ApplyInputDeviceSettings();
	// 대상 월드와 무관하게 한 번에 한 출력 선택만 처리
	if (bOutputDevicePending == true)
	{
		// 진행 중인 요청 종료 후 최신 확정 선택의 순차 적용 대기
		return;
	}
	// 종료된 이전 요청의 토큰과 틱 및 응답 상태 정리
	CancelOutputDeviceRequest();
	// 진행 중인 출력 선택의 고정 스냅샷 보관
	ActiveOutputDeviceId = RequestedOutputDeviceId;
	// 월드 초기화 전에도 유실되지 않는 최초 출력 적용 예약
	bOutputDevicePending = true;
	// 게임 스레드 취소와 플랫폼 접수 사이에 공유할 새 요청 토큰 생성
	OutputDeviceRequestState = MakeShared<FMD_AudioOutputRequestState, ESPMode::ThreadSafe>();
	// 비동기 출력 전환 시작 상태 기록
	UE_LOG(MarineDemolition, Display, TEXT("Audio output device refresh started: requested='%s'"), *ActiveOutputDeviceId);
	// 월드와 오디오 장치 준비를 기다리는 출력 완료 확인 등록
	OutputDeviceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMD_AudioSettingCategory::TickOutputDeviceSwap), 0.1f);
}

void UMD_AudioSettingCategory::SuspendAudioDeviceRequests()
{
	// 목적지 월드가 준비될 때까지 입출력 변경 요청 차단
	bAudioDevicesSuspended = true;
	// 아직 플랫폼에 접수되지 않은 오디오 작업과 후속 응답 취소
	CancelOutputDeviceRequest();
}

void UMD_AudioSettingCategory::RestoreWorldInputDevice()
{
	// 입력 캡처를 복원할 현재 게임 월드 조회
	UWorld* World = GetWorld();
	// 월드 준비 전과 에디터 및 전용 서버의 장치 복원 제외
	if (IsValid(World) == false || World->IsGameWorld() == false || World->GetNetMode() == NM_DedicatedServer)
	{
		// 실제 플레이 월드의 시작 시점까지 장치 변경 차단 유지
		return;
	}
	// 심리스 전환 도중 생성된 중간 월드의 캡처 재시작 차단
	if (World->IsInSeamlessTravel() == true)
	{
		// 최종 목적지 월드 시작까지 입력 복원 보류
		return;
	}
	// 목적지 월드에서 사용자가 장치를 변경할 수 있도록 차단 해제
	bAudioDevicesSuspended = false;
	// 새 음성 엔진에 확정된 입력 선택만 복원
	ApplyInputDeviceSettings();
}

void UMD_AudioSettingCategory::ShutdownAudioDevices()
{
	// 종료된 카테고리의 심리스 이동 이벤트 구독 제거
	FWorldDelegates::OnSeamlessTravelStart.Remove(SeamlessTravelStartHandle);
	// 반복 종료에 대비한 심리스 이동 구독 핸들 초기화
	SeamlessTravelStartHandle.Reset();
	// 종료된 카테고리의 월드 해제 이벤트 구독 제거
	FWorldDelegates::OnWorldBeginTearDown.Remove(WorldBeginTearDownHandle);
	// 반복 종료에 대비한 월드 해제 구독 핸들 초기화
	WorldBeginTearDownHandle.Reset();
	// 종료된 카테고리의 월드 정리 이벤트 구독 제거
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	// 반복 종료에 대비한 월드 정리 구독 핸들 초기화
	WorldCleanupHandle.Reset();
	// 월드 구독 해제 이후 장치 작업과 비동기 응답 일괄 중단
	SuspendAudioDeviceRequests();
}

void UMD_AudioSettingCategory::CancelOutputDeviceRequest()
{
	// 취소 이후 도착하는 기존 게임 스레드 응답 무효화
	++OutputDeviceRequestSerial;
	// 추가 오디오 작업을 예약할 수 있는 코어 틱 제거
	FTSTicker::GetCoreTicker().RemoveTicker(OutputDeviceTickerHandle);
	// 완료되거나 제거된 코어 틱 핸들 초기화
	OutputDeviceTickerHandle.Reset();
	// 실행 대기 중인 오디오 작업이 공유하는 요청 토큰 확인
	if (OutputDeviceRequestState.IsValid() == true)
	{
		// 플랫폼 접근과 취소 상태 변경을 같은 잠금으로 직렬화
		FScopeLock RequestLock(&OutputDeviceRequestState->Mutex);
		// 잠금 획득 이후 아직 접수되지 않은 플랫폼 작업 차단
		OutputDeviceRequestState->bCancelled = true;
	}
	// 오디오 작업 캡처와 분리된 카테고리의 토큰 소유권 해제
	OutputDeviceRequestState.Reset();
	// 취소된 출력 선택의 후속 처리 종료
	bOutputDevicePending = false;
	// 이전 오디오 조회의 응답 대기 해제
	bOutputDeviceQueryPending = false;
	// 다음 요청으로 전달하지 않을 플랫폼 접수 상태 초기화
	bOutputDeviceRequestAccepted = false;
	// 이전 요청을 새 월드에 적용하지 않도록 대상 참조 해제
	OutputRequestWorld.Reset();
	// 새 요청의 월드 준비 전 대기 상태 복원
	bOutputRequestWorldAssigned = false;
	// 로딩 시간을 다음 출력 요청 제한 시간에서 제외
	OutputDeviceRequestStartTime = 0.0;
}

void UMD_AudioSettingCategory::HandleSeamlessTravelStart(UWorld* World, const FString& DestinationUrl)
{
	// 여러 PIE 인스턴스 중 현재 카테고리 소유 게임 인스턴스 조회
	UGameInstance* GameInstance = GetTypedOuter<UGameInstance>();
	// 다른 게임 인스턴스의 심리스 이동에 따른 장치 취소 차단
	if (IsValid(World) == false || IsValid(GameInstance) == false || World->GetGameInstance() != GameInstance)
	{
		// 소유 게임 인스턴스와 관계없는 심리스 이동 제외
		return;
	}
	// 초기 출력 예약까지 포함한 심리스 이동 중 장치 작업 중단
	SuspendAudioDeviceRequests();
}

void UMD_AudioSettingCategory::HandleWorldBeginTearDown(UWorld* World)
{
	// 여러 PIE 인스턴스 중 현재 카테고리 소유 게임 인스턴스 조회
	UGameInstance* GameInstance = GetTypedOuter<UGameInstance>();
	// 다른 플레이어 인스턴스의 월드 이벤트가 현재 장치를 취소하는 동작 차단
	if (IsValid(World) == false || IsValid(GameInstance) == false || World->GetGameInstance() != GameInstance)
	{
		// 소유 게임 인스턴스와 관계없는 월드 이벤트 제외
		return;
	}
	// 최초 임시 게임 월드의 미귀속 출력 예약을 제외한 모든 장치 작업 중단
	if (bOutputDevicePending == false || bOutputRequestWorldAssigned == true || bHasOutputRequestStarted == true
		|| World->IsGameWorld() == false || World->HasBegunPlay() == true || World != GetWorld())
	{
		// 음성 엔진과 월드 오디오 정리 이전에 장치 작업 중단
		SuspendAudioDeviceRequests();
	}
	// 종료 월드의 오디오 장치가 유효한 동안 원격 재생 객체 해제
	ReleaseWorldRemoteVoice(World);
}

void UMD_AudioSettingCategory::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// 여러 PIE 인스턴스 중 현재 카테고리 소유 게임 인스턴스 조회
	UGameInstance* GameInstance = GetTypedOuter<UGameInstance>();
	// 다른 플레이어 인스턴스의 월드 정리 제외
	if (IsValid(World) == false || IsValid(GameInstance) == false || World->GetGameInstance() != GameInstance)
	{
		// 소유 게임 인스턴스와 관계없는 월드 이벤트 제외
		return;
	}
	// 심리스 이동은 PostLoadMap 정리가 옛 월드 소멸 이후에 오므로 등록 검증 전에 직접 해제
	ReleaseWorldRemoteVoice(World);
}

void UMD_AudioSettingCategory::ReleaseWorldRemoteVoice(UWorld* World)
{
	// 입력 설정 이전에도 정리할 수 있도록 서브시스템에서 실제 음성 엔진 조회
	TSharedPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> VoiceEngine = MDVoiceDeviceRouting::FindEngine(World);
	// 서브시스템 조회 실패 시 입력 설정 과정에서 확보한 엔진으로 대체
	if (VoiceEngine.IsValid() == false)
	{
		// 종료 중에도 유지되는 약한 참조 승격
		VoiceEngine = AppliedVoiceEngine.Pin();
	}
	// 엔진 버퍼가 추적하는 원격 재생의 등록 유지 정리
	if (VoiceEngine.IsValid() == true)
	{
		// 원격 등록과 디코더를 보존한 재생 객체 해제
		MDVoiceDeviceRouting::ResetWorldRemoteTalkers(World, *VoiceEngine);
	}
	// 엔진이 놓친 채 월드에 등록된 합성 컴포넌트의 최종 해제
	int32 ReleasedCount = MDVoiceDeviceRouting::ReleaseWorldVoiceSynthComponents(World);
	// 심리스 이동 크래시 원인 추적용 해제 기록
	if (ReleasedCount > 0)
	{
		// 월드 정리 검증 전에 해제한 잔류 컴포넌트 수 기록
		UE_LOG(MarineDemolition, Display, TEXT("Released %d leftover VOIP synth components from world %s"), ReleasedCount, *World->GetName());
	}
}

void UMD_AudioSettingCategory::ApplyInputDeviceSettings()
{
	// 월드 정리와 이동 중 마이크 캡처 전환 차단
	if (bAudioDevicesSuspended == true)
	{
		// 목적지 월드의 입력 복원까지 캡처 변경 보류
		return;
	}
	// 실제 플레이어가 속한 월드 조회
	UWorld* World = GetWorld();
	// 월드 초기화 이전 입력 요청의 지연 적용
	if (IsValid(World) == false)
	{
		// 유효한 게임 월드 생성 대기
		return;
	}
	// 이동 시작 알림 이전 호출까지 포함한 심리스 전환 중 입력 변경 차단
	if (World->IsInSeamlessTravel() == true)
	{
		// 음성 엔진 정리와 겹칠 수 있는 캡처 교체 생략
		return;
	}
	// 에디터 미리보기에서 마이크를 여는 동작 방지
	bool bGameWorld = World->IsGameWorld();
	// 실제 게임 월드에만 입력 장치 적용
	if (bGameWorld == false)
	{
		// 디자인 환경의 장치 요청 종료
		return;
	}
	// 서버 전용 실행에서 음성 인터페이스 초기화 방지
	ENetMode NetMode = World->GetNetMode();
	// 캡처 장치가 필요 없는 서버 요청 제외
	if (NetMode == NM_DedicatedServer)
	{
		// 전용 서버 입력 처리 종료
		return;
	}
	// Steam 및 PIE NULL 음성 엔진의 실제 캡처 전환
	MDVoiceDeviceRouting::EApplyResult Result = MDVoiceDeviceRouting::ApplyInputDevice(
		World, RequestedInputDeviceId, AppliedInputDeviceId, AppliedVoiceCapture, AppliedVoiceEngine);
	// 온라인 엔진 초기화 이전 요청의 실패 오인 방지
	if (Result == MDVoiceDeviceRouting::EApplyResult::NotReady)
	{
		// 로컬 송신 또는 월드 시작 시점의 재적용 대기
		return;
	}
	// 실제 입력 교체 성공 여부 보관
	bool bSuccessful = Result == MDVoiceDeviceRouting::EApplyResult::Applied;
	// 지원하지 않는 백엔드 또는 장치 초기화 실패 기록
	if (bSuccessful == false)
	{
		// 저장된 사용자 선택을 유지한 런타임 실패 알림
		UE_LOG(MarineDemolition, Warning, TEXT("Audio input device switch failed: %s (result=%d)"),
			*RequestedInputDeviceId, static_cast<int32>(Result));
	}
	// 실제 적용된 입력 장치와 전환 결과 전달
	OnInputDeviceChanged.Broadcast(bSuccessful, AppliedInputDeviceId);
}

bool UMD_AudioSettingCategory::TickOutputDeviceSwap(float DeltaTime)
{
	// 완료 또는 월드 전환 차단 이후 추가 오디오 작업 생성 방지
	if (bOutputDevicePending == false || bAudioDevicesSuspended == true)
	{
		// 남은 토큰과 틱 및 비동기 응답 정리
		CancelOutputDeviceRequest();
		// 종료된 요청의 반복 예약 중단
		return false;
	}
	// 현재 게임 인스턴스가 사용하는 월드 조회
	UWorld* World = GetWorld();
	// 이미 귀속된 요청이 새 월드의 오디오 장치에 전달되는 동작 차단
	if (bOutputRequestWorldAssigned == true && (IsValid(World) == false || OutputRequestWorld.Get() != World))
	{
		// 기존 대상 월드의 출력 요청과 응답 취소
		CancelOutputDeviceRequest();
		// 변경된 월드에 대한 자동 출력 재적용 생략
		return false;
	}
	// GameInstance 초기화 중 아직 없는 최초 게임 월드 대기
	if (IsValid(World) == false)
	{
		// 로딩 시간을 제한 시간에 포함하지 않는 예약 유지
		return true;
	}
	// 전용 서버의 하드웨어 출력 초기화 제외
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		// 필요 없는 출력 예약과 공유 토큰 정리
		CancelOutputDeviceRequest();
		// 전용 서버의 출력 확인 반복 중단
		return false;
	}
	// 에디터 초기 월드를 지나 실제 게임 월드가 준비될 때까지 대기
	if (World->IsGameWorld() == false)
	{
		// 게임 환경 준비 전 플랫폼 접근 없는 예약 유지
		return true;
	}
	// 심리스 이동 시작 알림 누락 시에도 장치 전환 중단
	if (World->IsInSeamlessTravel() == true)
	{
		// 음성 엔진 정리 중인 월드의 모든 장치 작업 차단
		SuspendAudioDeviceRequests();
		// 목적지 월드로 기존 출력 요청을 전달하지 않는 반복 종료
		return false;
	}
	// 긴 맵 로딩과 BeginPlay 이전의 출력 요청 제한 시간 소모 방지
	if (World->HasBegunPlay() == false)
	{
		// 재생 준비를 마친 게임 월드의 시작 대기
		return true;
	}
	// 최초 준비된 게임 월드에만 현재 출력 요청 귀속
	if (bOutputRequestWorldAssigned == false)
	{
		// 이후 월드 변경을 검출할 고정 대상 보관
		OutputRequestWorld = World;
		// 월드 준비 전 대기와 대상 소멸의 구분 확정
		bOutputRequestWorldAssigned = true;
		// 이후 월드 종료에서 최초 임시 월드 예약 예외의 재사용 차단
		bHasOutputRequestStarted = true;
		// 게임 월드 준비 이후부터 장치 전환 제한 시간 측정
		OutputDeviceRequestStartTime = FPlatformTime::Seconds();
	}
	// 준비된 월드에서 오디오 장치 응답이 없는 무한 대기 차단
	if (FPlatformTime::Seconds() - OutputDeviceRequestStartTime > 8.0)
	{
		// 실패 원인을 현재 전환 대상 장치 ID와 함께 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Audio output device switch timed out: %s"), *ActiveOutputDeviceId);
		// 제한 시간 이후 실행될 수 있는 플랫폼 작업과 응답 취소
		CancelOutputDeviceRequest();
		// 대기 중 새 선택이 있는 경우 최신 선택의 전환 시작
		if (ActiveOutputDeviceId != RequestedOutputDeviceId)
		{
			// 이전 요청 실패와 무관한 최신 선택 재적용
			RefreshAudioDevices();
		}
		else
		{
			// 저장된 사용자 선택을 변경하지 않는 실패 이벤트
			OnOutputDeviceChanged.Broadcast(false, CurrentOutputDeviceId);
		}
		// 제한 시간이 지난 요청의 추가 예약 중단
		return false;
	}
	// 같은 요청의 중복 오디오 스레드 조회 방지
	if (bOutputDeviceQueryPending == true)
	{
		// 이미 예약된 조회의 응답 대기
		return true;
	}
	// 오디오 스레드 처리 중에도 유지할 현재 월드의 장치 수명 확보
	FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
	// 초기화 중 생성되지 않은 출력 장치 준비 대기
	if (AudioDevice.IsValid() == false)
	{
		// 준비된 게임 월드에서 장치 생성 확인 반복
		return true;
	}
	// 취소된 요청이 토큰 없이 플랫폼에 접근하는 동작 차단
	if (OutputDeviceRequestState.IsValid() == false)
	{
		// 토큰이 없는 요청의 나머지 상태 정리
		CancelOutputDeviceRequest();
		// 취소된 요청의 오디오 예약 종료
		return false;
	}
	// 오디오 스레드 조회의 중복 예약 방지 상태 설정
	bOutputDeviceQueryPending = true;
	// 최신 응답만 반영할 요청 번호 복사
	uint32 RequestSerial = OutputDeviceRequestSerial;
	// 작업 스레드 접근에서 분리한 사용자 선택 복사
	FString RequestedDeviceId = ActiveOutputDeviceId;
	// 접수된 전환의 중복 요청 방지를 위한 상태 복사
	bool bAccepted = bOutputDeviceRequestAccepted;
	// 작업 실행 전 취소 여부를 공유할 요청 토큰 수명 확보
	TSharedPtr<FMD_AudioOutputRequestState, ESPMode::ThreadSafe> RequestState = OutputDeviceRequestState;
	// 게임 스레드 응답 시점의 카테고리 생존 확인용 참조
	TWeakObjectPtr<UMD_AudioSettingCategory> WeakThis(this);
	// 플랫폼 오디오 장치 접근을 오디오 스레드로 제한
	FAudioThread::RunCommandOnAudioThread([AudioDevice, RequestSerial, RequestedDeviceId, bAccepted, RequestState, WeakThis]() mutable
	{
		// 실제 전환 여부의 기본 결과 준비
		bool bSuccessful = false;
		// 결과 이벤트에 전달할 현재 하드웨어 식별자 준비
		FString CurrentDeviceId;
		{
			// 게임 스레드 취소와 플랫폼 조회 및 전환 접수를 하나의 임계 영역으로 보호
			FScopeLock RequestLock(&RequestState->Mutex);
			// 월드 종료 전에 취소된 작업의 실제 장치 접근 차단
			if (RequestState->bCancelled == true)
			{
				// 취소된 작업의 플랫폼 조회와 게임 스레드 응답 생략
				return;
			}
			// UE AudioMixer가 사용하는 플랫폼 출력 인터페이스 조회
			Audio::FMixerDevice* Mixer = static_cast<Audio::FMixerDevice*>(AudioDevice.GetAudioDevice());
			// 실제 하드웨어 출력 플랫폼 조회
			Audio::IAudioMixerPlatformInterface* Platform = Mixer->GetAudioMixerPlatform();
			// 지원 플랫폼의 실제 장치 상태 확인
			if (Platform != nullptr)
			{
				// 전환 요청 시점이 아닌 현재 실제 장치 정보 조회
				Audio::FAudioPlatformDeviceInfo CurrentDevice = Platform->GetPlatformDeviceInfo();
				// 실제 하드웨어 장치 식별자 보관
				CurrentDeviceId = CurrentDevice.DeviceId;
				// 명시적으로 선택한 장치 ID를 기준으로 완료 판정
				FString TargetDeviceId = RequestedDeviceId;
				// 빈 사용자 선택을 실제 기본 출력 장치로 변환
				if (TargetDeviceId.IsEmpty() == true)
				{
					// 캐시를 제공하는 플랫폼의 기본 장치 조회
					Audio::IAudioPlatformDeviceInfoCache* Cache = Platform->GetDeviceInfoCache();
					// 장치 열거 캐시가 준비된 플랫폼 처리
					if (Cache != nullptr)
					{
						// 게임 오디오용 기본 출력 엔드포인트 조회
						FName DefaultId = Cache->GetDefaultOutputDevice(Audio::EAudioDeviceRole::Console);
						// 실제 기본 출력 ID 보관
						TargetDeviceId = DefaultId.ToString();
					}
					else
					{
						// 캐시 없는 플랫폼의 기본 출력 인덱스 준비
						uint32 DefaultIndex = 0;
						// 유효한 기본 출력 인덱스의 장치 정보 조회
						if (Platform->GetDefaultOutputDeviceIndex(DefaultIndex) == true)
						{
							// 기본 출력 장치 정보 보관 공간 준비
							Audio::FAudioPlatformDeviceInfo DefaultDevice;
							// 실제 기본 장치 식별자 조회
							if (Platform->GetOutputDeviceInfo(DefaultIndex, DefaultDevice) == true)
							{
								// 기본 선택의 완료 판정 기준 지정
								TargetDeviceId = DefaultDevice.DeviceId;
							}
						}
					}
				}
				// 실제 출력이 존재하고 요청 장치와 일치하는 경우에만 성공 판정
				bSuccessful = CurrentDeviceId.IsEmpty() == false && CurrentDeviceId == TargetDeviceId;
				// 실제 출력이 다르고 아직 접수되지 않은 요청만 플랫폼 전달
				if (bSuccessful == false && bAccepted == false)
				{
					// 엔진 기본 경로와 동일한 비강제 전환으로 지연된 강제 재시작 방지
					bAccepted = Platform->RequestDeviceSwap(RequestedDeviceId, false, TEXT("MD user audio settings"));
				}
			}
		}
		// 잠금 해제 이후 응답 전달로 PIE의 즉시 콜백 취소 재진입 교착 방지
		FAudioThread::RunCommandOnGameThread([WeakThis, RequestSerial, bSuccessful, bAccepted, CurrentDeviceId]()
		{
			// 비동기 작업 이후 카테고리 인스턴스 조회
			UMD_AudioSettingCategory* Category = WeakThis.Get();
			// 파괴된 카테고리의 응답 무시
			if (IsValid(Category) == true)
			{
				// 실제 출력 상태의 최신 요청 반영
				Category->HandleOutputDeviceChecked(RequestSerial, bSuccessful, bAccepted, CurrentDeviceId);
			}
		});
	});
	// 출력 확인 반복 유지
	return true;
}

void UMD_AudioSettingCategory::HandleOutputDeviceChecked(uint32 RequestSerial, bool bSuccessful, bool bAccepted, FString CurrentDeviceId)
{
	// 취소와 새 요청 및 완료 이후 도착한 이전 응답 무시
	if (RequestSerial != OutputDeviceRequestSerial || bAudioDevicesSuspended == true || bOutputDevicePending == false)
	{
		// 최신 선택 보존
		return;
	}
	// 출력 응답이 귀속된 현재 게임 월드 조회
	UWorld* World = GetWorld();
	// 새 월드와 종료 중인 월드에 이전 출력 결과를 반영하는 동작 차단
	if (IsValid(World) == false || bOutputRequestWorldAssigned == false || OutputRequestWorld.Get() != World)
	{
		// 대상 월드가 사라진 요청의 나머지 작업 정리
		CancelOutputDeviceRequest();
		// 다른 월드에 대한 완료 이벤트와 음량 재적용 생략
		return;
	}
	// 게임 종료와 전용 서버 및 심리스 이동 중 출력 완료 처리 제외
	if (World->IsGameWorld() == false || World->GetNetMode() == NM_DedicatedServer || World->IsInSeamlessTravel() == true)
	{
		// 재생 환경을 벗어난 출력 요청 취소
		CancelOutputDeviceRequest();
		// 다음 월드에 이전 요청을 전달하지 않는 응답 종료
		return;
	}
	// 완료된 오디오 스레드 조회의 대기 해제
	bOutputDeviceQueryPending = false;
	// 성공 여부와 무관한 실제 출력 장치 추적
	CurrentOutputDeviceId = CurrentDeviceId;
	// 접수된 전환의 중복 요청 방지 상태 반영
	bOutputDeviceRequestAccepted = bAccepted;
	// 실제 출력 ID 확인 성공 시 요청 종료
	if (bSuccessful == true)
	{
		// 확인을 마친 출력 요청의 토큰과 틱 및 응답 상태 정리
		CancelOutputDeviceRequest();
		// 이전 전환 중 새로 적용된 선택의 순차 처리
		if (ActiveOutputDeviceId != RequestedOutputDeviceId)
		{
			// 가장 최근 적용한 출력 장치의 전환 시작
			RefreshAudioDevices();
			// 이전 선택의 완료 이벤트 생략
			return;
		}
		// 유효한 게임 월드에 한정한 음량 재적용
		if (IsValid(World) == true)
		{
			// 현재 월드의 오디오 처리 서브시스템 조회
			UMD_AudioSubsystem* AudioSubsystem = World->GetSubsystem<UMD_AudioSubsystem>();
			// 준비된 오디오 처리 서브시스템의 사용자 음량 복원
			if (IsValid(AudioSubsystem) == true)
			{
				// 전환 완료 장치의 기존 재생 소리와 신규 소리에 대한 사용자 음량 복원
				AudioSubsystem->ApplyUserVolumes(Settings.MasterVolume, Settings.MusicVolume, Settings.EffectsVolume);
			}
		}
		// 실제 출력 장치 전환 완료 상태 기록
		UE_LOG(MarineDemolition, Display, TEXT("Audio output device refresh completed: current='%s'"), *CurrentDeviceId);
		// 전환 완료 시점의 실제 장치 ID 전달
		OnOutputDeviceChanged.Broadcast(true, CurrentDeviceId);
	}
}
