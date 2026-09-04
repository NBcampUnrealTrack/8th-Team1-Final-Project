#include "Gameplay/Settings/MD_AudioSettingCategory.h"

#include "AudioMixerBlueprintLibrary.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/VoiceConfig.h"
#include "Online/Voice/MD_VoiceChatComponent.h"
#include "Online/Voice/MD_VoiceDeviceRouting.h"
#include "Util/MD_LogUtility.h"

void UMD_AudioSettingCategory::BeginDestroy()
{
	// 소유 인스턴스 종료 이후 비동기 응답의 적용 차단
	++OutputDeviceRequestSerial;
	// 종료된 카테고리의 출력 장치 확인 틱 제거
	FTSTicker::GetCoreTicker().RemoveTicker(OutputDeviceTickerHandle);
	Super::BeginDestroy();
}

void UMD_AudioSettingCategory::ApplySettings()
{
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
	// 현재 적용할 게임 월드 조회
	UWorld* World = GetWorld();
	// 초기화 중 월드가 없는 요청의 월드 시작 시점까지 보류
	if (IsValid(World) == false)
	{
		// 오디오 장치 준비 이후 재적용 대기
		return;
	}
	// 에디터 미리보기에서 실제 장치를 변경하는 동작 방지
	bool bGameWorld = World->IsGameWorld();
	// 게임 월드가 아닌 경우 적용 종료
	if (bGameWorld == false)
	{
		// 디자인 시점의 장치 유지
		return;
	}
	// 전용 서버의 로컬 오디오 초기화 방지
	ENetMode NetMode = World->GetNetMode();
	// 음성 재생 장치가 필요 없는 서버 처리 제외
	if (NetMode == NM_DedicatedServer)
	{
		// 전용 서버 요청 종료
		return;
	}
	// 실제 온라인 음성 입력 캡처 전환
	ApplyInputDeviceSettings();
	// 이전 출력 요청과 같은 하드웨어를 공유하는 월드 확인
	UWorld* PreviousWorld = OutputRequestWorld.Get();
	// 진행 중인 전환은 완료 후 최신 선택을 순차 적용
	if (PreviousWorld == World)
	{
		// 되돌아오는 선택이 이전 전환보다 먼저 성공 처리되는 경쟁 방지
		if (bOutputDevicePending == true)
		{
			// 최신 RequestedOutputDeviceId를 유지한 기존 전환 완료 대기
			return;
		}
	}
	// 이번 출력 전환의 대상 월드 보관
	OutputRequestWorld = World;
	// 진행 중인 출력 선택의 고정 스냅샷 보관
	ActiveOutputDeviceId = RequestedOutputDeviceId;
	// 이전 비동기 출력 요청의 결과 무효화
	++OutputDeviceRequestSerial;
	// 새 출력 확인 요청 활성화
	bOutputDevicePending = true;
	// 이전 요청의 조회 대기 상태 초기화
	bOutputDeviceQueryPending = false;
	// 새 선택의 엔진 요청 접수 상태 초기화
	bOutputDeviceRequestAccepted = false;
	// 제한 시간 기준 시각 보관
	OutputDeviceRequestStartTime = FPlatformTime::Seconds();
	// 비동기 출력 전환 시작 상태 기록
	UE_LOG(MarineDemolition, Display, TEXT("Audio output device refresh started: requested='%s'"), *ActiveOutputDeviceId);
	// 기존 출력 틱의 중복 등록 제거
	FTSTicker::GetCoreTicker().RemoveTicker(OutputDeviceTickerHandle);
	// 일시정지 UI에서도 동작하는 출력 완료 확인 등록
	OutputDeviceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMD_AudioSettingCategory::TickOutputDeviceSwap), 0.1f);
}

void UMD_AudioSettingCategory::ApplyInputDeviceSettings()
{
	// 실제 플레이어가 속한 월드 조회
	UWorld* World = GetWorld();
	// 월드 초기화 이전 입력 요청의 지연 적용
	if (IsValid(World) == false)
	{
		// 유효한 게임 월드 생성 대기
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
		World, RequestedInputDeviceId, AppliedInputDeviceId, AppliedVoiceCapture);
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
	// 출력 처리 완료 이후 틱 자동 종료
	if (bOutputDevicePending == false)
	{
		// 종료된 틱 핸들 초기화
		OutputDeviceTickerHandle.Reset();
		// 반복 종료 결과 보관
		bool bContinue = false;
		// 완료된 요청의 반복 종료
		return bContinue;
	}
	// 오디오 스레드 응답이 없는 상태의 무한 대기 차단
	double ElapsedSeconds = FPlatformTime::Seconds() - OutputDeviceRequestStartTime;
	// 최대 대기 시간 초과 시 처리 종료
	if (ElapsedSeconds > 8.0)
	{
		// 시간 초과 뒤 오래된 응답의 상태 변경 차단
		++OutputDeviceRequestSerial;
		// 출력 요청 종료 상태 반영
		bOutputDevicePending = false;
		// 실패 원인을 현재 전환 대상 장치 ID와 함께 기록
		UE_LOG(MarineDemolition, Warning, TEXT("Audio output device switch timed out: %s"), *ActiveOutputDeviceId);
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
		// 제한 시간이 지난 요청의 추가 예약 방지
		bool bContinue = false;
		// 시간 초과 요청의 반복 종료
		return bContinue;
	}
	// 같은 요청의 중복 오디오 스레드 조회 방지
	if (bOutputDeviceQueryPending == false)
	{
		// 현재 월드의 전용 PIE 또는 게임 오디오 장치 조회
		UWorld* World = GetWorld();
		// 트래블 사이의 월드 부재 허용
		if (IsValid(World) == true)
		{
			// 오디오 스레드 처리 중에도 유효한 장치 수명 보장
			FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
			// 초기화 중 생성되지 않은 출력 장치의 재시도
			if (AudioDevice.IsValid() == true)
			{
				// 오디오 스레드 조회 대기 상태 설정
				bOutputDeviceQueryPending = true;
				// 최신 요청만 반영할 요청 번호 복사
				uint32 RequestSerial = OutputDeviceRequestSerial;
				// 작업 스레드 접근에서 분리한 사용자 선택 복사
				FString RequestedDeviceId = ActiveOutputDeviceId;
				// 접수된 전환의 중복 요청 방지를 위한 상태 복사
				bool bAccepted = bOutputDeviceRequestAccepted;
				// 게임 스레드 응답 시점의 카테고리 생존 확인용 참조
				TWeakObjectPtr<UMD_AudioSettingCategory> WeakThis(this);
				// 플랫폼 오디오 장치 접근을 오디오 스레드로 제한
				FAudioThread::RunCommandOnAudioThread([AudioDevice, RequestSerial, RequestedDeviceId, bAccepted, WeakThis]() mutable
				{
					// UE AudioMixer가 사용하는 플랫폼 출력 인터페이스 조회
					Audio::FMixerDevice* Mixer = static_cast<Audio::FMixerDevice*>(AudioDevice.GetAudioDevice());
					// 실제 하드웨어 출력 플랫폼 조회
					Audio::IAudioMixerPlatformInterface* Platform = Mixer->GetAudioMixerPlatform();
					// 실제 전환 여부의 기본 결과
					bool bSuccessful = false;
					// 결과 이벤트에 전달할 현재 하드웨어 식별자
					FString CurrentDeviceId;
					// 지원 플랫폼의 실제 장치 상태 확인
					if (Platform != nullptr)
					{
						// 전환 요청 시점이 아닌 현재 실제 장치 정보 조회
						Audio::FAudioPlatformDeviceInfo CurrentDevice = Platform->GetPlatformDeviceInfo();
						// 실제 하드웨어 장치 식별자 보관
						CurrentDeviceId = CurrentDevice.DeviceId;
						// 명시적으로 선택한 장치 ID를 기준으로 완료 판정
						FString TargetDeviceId = RequestedDeviceId;
						// 기본 장치 선택의 현재 시스템 장치 ID 확인
						bool bDefault = TargetDeviceId.IsEmpty();
						// 빈 사용자 선택을 실제 기본 출력 장치로 변환
						if (bDefault == true)
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
								// 실제 기본 출력 인덱스 조회
								bool bFoundIndex = Platform->GetDefaultOutputDeviceIndex(DefaultIndex);
								// 유효한 기본 장치 인덱스 처리
								if (bFoundIndex == true)
								{
									// 기본 출력 장치 정보 보관 공간
									Audio::FAudioPlatformDeviceInfo DefaultDevice;
									// 플랫폼 기본 장치 정보 조회
									bool bFoundDevice = Platform->GetOutputDeviceInfo(DefaultIndex, DefaultDevice);
									// 실제 기본 장치 ID 보관
									if (bFoundDevice == true)
									{
										// 기본 선택의 완료 판정 기준 지정
										TargetDeviceId = DefaultDevice.DeviceId;
									}
								}
							}
						}
						// 실제 장치가 존재하는 경우에만 성공 판정
						bool bHasDevice = CurrentDeviceId.IsEmpty() == false;
						// 요청한 장치와 현재 장치의 일치 여부 확인
						if (bHasDevice == true)
						{
							// 동일 출력에 대한 불필요한 재시작 방지
							bSuccessful = CurrentDeviceId == TargetDeviceId;
						}
						// 실제 출력이 다른 경우에만 전환 요청
						if (bSuccessful == false)
						{
							// 이미 접수된 요청은 완료까지 조회만 수행
							if (bAccepted == false)
							{
								// 직렬화된 사용자 선택과 엔진 장치 이벤트의 경합을 해소하는 확정 전환
								bAccepted = Platform->RequestDeviceSwap(RequestedDeviceId, true, TEXT("MD user audio settings"));
							}
						}
					}
					// 카테고리와 Blueprint 이벤트 접근을 게임 스레드로 제한
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
			}
		}
	}
	// 완료 상태를 다음 틱에서 확인할 반복 결과 보관
	bool bContinue = true;
	// 출력 확인 반복 유지
	return bContinue;
}

void UMD_AudioSettingCategory::HandleOutputDeviceChecked(uint32 RequestSerial, bool bSuccessful, bool bAccepted, FString CurrentDeviceId)
{
	// 새 적용 요청 이후 도착한 이전 응답 무시
	if (RequestSerial != OutputDeviceRequestSerial)
	{
		// 최신 선택 보존
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
		// 실제 전환 완료 상태 반영
		bOutputDevicePending = false;
		// 이전 전환 중 새로 적용된 선택의 순차 처리
		if (ActiveOutputDeviceId != RequestedOutputDeviceId)
		{
			// 가장 최근 적용한 출력 장치의 전환 시작
			RefreshAudioDevices();
			// 이전 선택의 완료 이벤트 생략
			return;
		}
		// 새 출력 스트림에 대한 전체 및 카테고리 음량 상태 재적용
		UWorld* World = GetWorld();
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
