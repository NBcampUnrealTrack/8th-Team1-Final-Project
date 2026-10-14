#include "Online/Voice/MD_VoiceDeviceRouting.h"

#include "AudioCaptureCore.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "VoiceEngineImpl.h"
#include "VoiceInterfaceImpl.h"
#include "VoiceModule.h"
#include "VoipListenerSynthComponent.h"

// 엔진 파일 변경 없이 UE 5.8의 보호된 엔진 참조에 접근하는 타입 지정 브리지
class FMD_OnlineVoiceAccess : public FOnlineVoiceImpl
{
public:
	// 가짜 파생 인스턴스 캐스팅 없는 기반 클래스 멤버 포인터 제공
	static auto EngineMember()
	{
		// Steam과 NULL에서 공유하는 기존 음성 엔진 멤버
		auto Member = &FMD_OnlineVoiceAccess::VoiceEngine;
		// 타입 검증된 멤버 포인터 반환
		return Member;
	}
};

// 공통 음성 엔진의 캡처와 원격 재생 버퍼에 한정된 버전 의존 브리지
class FMD_VoiceEngineAccess : public FVoiceEngineImpl
{
public:
	// 기존 네트워크·코덱 상태를 보존할 캡처 멤버 포인터 제공
	static auto CaptureMember()
	{
		// private 저장소 대신 엔진이 제공하는 protected 캡처 접근 함수
		TSharedPtr<IVoiceCapture>& (FVoiceEngineImpl::*Member)() = &FMD_VoiceEngineAccess::GetVoiceCapture;
		// 타입 검증된 멤버 포인터 반환
		return Member;
	}

	// 원격 등록 저장소의 타입을 노출하지 않는 재생 버퍼 접근 브리지
	static auto RemoteBuffersMember()
	{
		// 기존 엔진 인스턴스에 적용할 보호된 원격 버퍼 접근 함수 반환
		return &FMD_VoiceEngineAccess::GetRemoteTalkerBuffers;
	}
};

TSharedPtr<IVoiceCapture>& MDVoiceDeviceRouting::GetCapture(FVoiceEngineImpl& Engine)
{
	// 실제 기반 인스턴스에 적용할 캡처 멤버 조회
	auto Member = FMD_VoiceEngineAccess::CaptureMember();
	// 코덱과 원격 음성 버퍼를 유지한 캡처 참조 조회
	TSharedPtr<IVoiceCapture>& Capture = (Engine.*Member)();
	// 교체 대상 캡처 참조 반환
	return Capture;
}

TSharedPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> MDVoiceDeviceRouting::GetEngine(FOnlineVoiceImpl& Voice)
{
	// 기존 온라인 음성 객체의 엔진 멤버 조회
	auto Member = FMD_OnlineVoiceAccess::EngineMember();
	// Steam 및 NULL이 생성하는 FVoiceEngineImpl 파생 엔진 참조
	TSharedPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> Engine = StaticCastSharedPtr<FVoiceEngineImpl>(Voice.*Member);
	// 기존 등록 사용자와 코덱을 보존한 엔진 반환
	return Engine;
}

void MDVoiceDeviceRouting::ResetWorldRemoteTalkers(UWorld* World, FVoiceEngineImpl& Engine)
{
	// 종료 월드를 확인할 수 없는 호출의 원격 재생 변경 차단
	if (IsValid(World) == false)
	{
		// 유효한 종료 월드가 없는 정리 요청 종료
		return;
	}
	// 등록 사용자와 디코더를 보존한 기존 엔진의 원격 재생 버퍼 순회
	for (auto& RemoteTalkerPair : (Engine.*FMD_VoiceEngineAccess::RemoteBuffersMember())())
	{
		// 현재 원격 플레이어의 등록 정보를 유지할 재생 데이터 참조
		FRemoteTalkerDataImpl& RemoteTalkerData = RemoteTalkerPair.Value;
		// 실제 음성 재생에 사용 중인 원격 합성 컴포넌트 조회
		UVoipListenerSynthComponent* SynthComponent = RemoteTalkerData.VoipSynthComponent.Get();
		// 이미 정리되었거나 아직 생성되지 않은 재생 대상 제외
		if (IsValid(SynthComponent) == false)
		{
			// 재생 객체가 없는 원격 플레이어의 등록 상태 유지
			continue;
		}
		// 합성 컴포넌트와 별도로 월드에 등록될 수 있는 내부 출력 조회
		UAudioComponent* AudioComponent = SynthComponent->GetAudioComponent();
		// 종료 대상 월드에 속한 합성 또는 내부 출력만 선택
		if (SynthComponent->GetWorld() == World || (IsValid(AudioComponent) == true && AudioComponent->GetWorld() == World))
		{
			// 오디오 장치 해제 전에 재생을 중지하고 원격 등록과 디코더 및 캡처 유지
			RemoteTalkerData.Reset();
		}
	}
}

bool MDVoiceDeviceRouting::ReplaceCapture(TSharedPtr<IVoiceCapture>& CurrentCapture, TSharedPtr<IVoiceCapture> NewCapture)
{
	// 실패 시 기존 캡처를 유지하는 기본 결과
	bool bReplaced = false;
	// 초기화에 실패한 새 장치의 반영 차단
	if (NewCapture.IsValid() == false)
	{
		// 기존 입력 유지 결과 반환
		return bReplaced;
	}
	// PTT 키 상태와 항상 켜기 모드를 보존할 실제 캡처 상태
	bool bWasCapturing = false;
	// 기존 캡처가 생성된 경우에만 송신 상태 조회
	if (CurrentCapture.IsValid() == true)
	{
		// 장치 교체 이전 녹음 상태 보관
		bWasCapturing = CurrentCapture->IsCapturing();
	}
	// 기존 장치가 녹음 중인 경우에만 새 장치 시작
	if (bWasCapturing == true)
	{
		// 새 입력 시작 실패 시 기존 입력을 유지할 결과 확인
		bool bStarted = NewCapture->Start();
		// 기존 캡처 중단 전 새 캡처의 정상 시작 검증
		if (bStarted == false)
		{
			// 기존 입력 유지 결과 반환
			return bReplaced;
		}
		// 새 장치 시작이 보장된 이후 기존 입력 중지
		CurrentCapture->Stop();
	}
	// 기존 엔진과 네트워크 등록 상태를 유지한 캡처 교체
	CurrentCapture = MoveTemp(NewCapture);
	// 장치 교체 완료 결과 보관
	bReplaced = true;
	// 실제 교체 결과 반환
	return bReplaced;
}

MDVoiceDeviceRouting::EApplyResult MDVoiceDeviceRouting::ApplyInputDevice(UWorld* World, FString DeviceId,
	FString& AppliedDeviceId, TWeakPtr<IVoiceCapture>& AppliedCapture, TWeakPtr<FVoiceEngineImpl, ESPMode::ThreadSafe>& AppliedEngine)
{
	// 월드별 PIE 인스턴스와 실제 게임의 온라인 서브시스템 조회
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(World);
	// 온라인 초기화 이전 요청의 지연 적용
	if (Subsystem == nullptr)
	{
		// 다음 월드 시작 또는 음성 송신 시점의 재시도 결과
		EApplyResult Result = EApplyResult::NotReady;
		// 초기화 대기 결과 반환
		return Result;
	}
	// 엔진 상속 구조가 검증된 구현만 브리지에 전달
	FName SubsystemName = Subsystem->GetSubsystemName();
	// Steam 외 구현의 별도 검증
	if (SubsystemName != STEAM_SUBSYSTEM)
	{
		// 다른 구현의 잘못된 기반 타입 캐스팅 차단
		if (SubsystemName != NULL_SUBSYSTEM)
		{
			// 미지원 음성 백엔드 결과 보관
			EApplyResult Result = EApplyResult::Unsupported;
			// 지원하지 않는 구현 결과 반환
			return Result;
		}
	}
	// 실제 플레이어가 사용하는 온라인 음성 인터페이스 조회
	IOnlineVoicePtr Voice = Subsystem->GetVoiceInterface();
	// 음성 비활성화 또는 초기화 실패 시 안전한 대기
	if (Voice.IsValid() == false)
	{
		// 생성되지 않은 음성 엔진 상태 보관
		EApplyResult Result = EApplyResult::NotReady;
		// 초기화 대기 결과 반환
		return Result;
	}
	// 검증된 Steam 및 NULL 공통 기반 객체 조회
	FOnlineVoiceImpl& VoiceImpl = static_cast<FOnlineVoiceImpl&>(*Voice);
	// 새 음성 엔진을 만들지 않는 기존 엔진 조회
	TSharedPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> Engine = GetEngine(VoiceImpl);
	// 전용 서버 등 캡처 엔진 없는 환경의 접근 방지
	if (Engine.IsValid() == false)
	{
		// 캡처 엔진 생성 대기 결과 보관
		EApplyResult Result = EApplyResult::NotReady;
		// 초기화 대기 결과 반환
		return Result;
	}
	// 입력 장치 변경 실패와 무관하게 종료 월드 정리에 사용할 기존 엔진 보관
	AppliedEngine = Engine;
	// 네트워크 음성에서 실제 읽는 캡처 참조 조회
	TSharedPtr<IVoiceCapture>& CurrentCapture = GetCapture(*Engine);
	// 장치 ID뿐 아니라 음성 엔진 재생성 여부를 포함한 중복 적용 검사
	TSharedPtr<IVoiceCapture> LastCapture = AppliedCapture.Pin();
	// 이전에 적용한 캡처가 여전히 존재하는 경우의 빠른 경로
	if (LastCapture.IsValid() == true)
	{
		// 동일 엔진 캡처 확인
		if (LastCapture == CurrentCapture)
		{
			// 동일 입력 선택에 대한 재시작 방지
			if (AppliedDeviceId == DeviceId)
			{
				// 기존 장치 유지 성공 결과 보관
				EApplyResult Result = EApplyResult::Applied;
				// 중복 적용 없는 성공 반환
				return Result;
			}
		}
	}
	// 기본 입력은 레거시 캡처의 빈 장치 이름으로 지정
	FString DeviceName;
	// 특정 장치 선택의 영속 ID와 런타임 이름 대응
	bool bUseDefaultDevice = DeviceId.IsEmpty();
	// 실제 입력 장치를 선택한 경우에만 목록 조회
	if (bUseDefaultDevice == false)
	{
		// 입력 장치를 열지 않는 장치 열거 객체
		Audio::FAudioCapture Enumerator;
		// 현재 연결된 장치 목록 보관
		TArray<Audio::FCaptureDeviceInfo> Devices;
		// UI와 동일한 장치 ID 체계의 목록 조회
		Enumerator.GetCaptureDevicesAvailable(Devices);
		// 일치하는 장치의 이름 검색
		for (Audio::FCaptureDeviceInfo& Device : Devices)
		{
			// 표시 이름 대신 고유 ID를 기준으로 한 선택 확인
			if (Device.DeviceId == DeviceId)
			{
				// 레거시 음성 캡처 API가 받는 장치 이름 보관
				DeviceName = Device.DeviceName;
				// 장치 매핑 완료 후 탐색 종료
				break;
			}
		}
		// 연결 해제된 장치를 임의의 기본 장치로 간주하는 동작 차단
		bool bDeviceMissing = DeviceName.IsEmpty();
		// 선택한 입력을 찾지 못한 경우 기존 캡처 유지
		if (bDeviceMissing == true)
		{
			// 실제 전환 실패 결과 보관
			EApplyResult Result = EApplyResult::Failed;
			// 전환 실패 결과 반환
			return Result;
		}
	}
	// 기존 네트워크 코덱과 동일한 입력 포맷 조회
	int32 SampleRate = UVOIPStatics::GetVoiceSampleRate();
	// 기존 코덱과 일치하는 채널 수 조회
	int32 NumChannels = UVOIPStatics::GetVoiceNumChannels();
	// 기존 입력을 유지한 상태에서 선택 장치의 초기화 가능 여부 검증
	TSharedPtr<IVoiceCapture> NewCapture = FVoiceModule::Get().CreateVoiceCapture(DeviceName, SampleRate, NumChannels);
	// PTT 상태 보존 및 실패 시 기존 장치 유지
	bool bReplaced = ReplaceCapture(CurrentCapture, MoveTemp(NewCapture));
	// 교체 실패 결과의 기본값
	EApplyResult Result = EApplyResult::Failed;
	// 실제 캡처 교체 성공 시에만 적용 상태 갱신
	if (bReplaced == true)
	{
		// 중복 재시작 방지를 위한 실제 캡처 보관
		AppliedCapture = CurrentCapture;
		// 적용된 입력 장치 ID 보관
		AppliedDeviceId = MoveTemp(DeviceId);
		// 실제 교체 성공 결과 보관
		Result = EApplyResult::Applied;
	}
	// 실제 입력 전환 결과 반환
	return Result;
}
