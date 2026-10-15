#pragma once

#include "CoreMinimal.h"
#include "Interfaces/VoiceCapture.h"

class UWorld;
class FOnlineVoiceImpl;
class FVoiceEngineImpl;

// Steam 및 NULL 공통 음성 엔진의 캡처 장치 연결
namespace MDVoiceDeviceRouting
{
	// 온라인 음성 초기화 대기와 실제 전환 실패를 구분하는 결과
	enum class EApplyResult : uint8
	{
		Applied,
		NotReady,
		Failed,
		Unsupported
	};

	// UE 5.8 공통 음성 구현에 한정된 캡처 참조 조회
	TSharedPtr<IVoiceCapture>& GetCapture(FVoiceEngineImpl& Engine);

	// 이미 등록된 음성 엔진의 유지 및 신규 엔진 생성 방지
	TSharedPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> GetEngine(FOnlineVoiceImpl& Voice);

	// 원격 등록과 디코더를 유지한 종료 월드의 음성 재생 객체 정리
	void ResetWorldRemoteTalkers(UWorld* World, FVoiceEngineImpl& Engine);

	// 새 엔진 생성 없이 월드의 온라인 서브시스템에 이미 만들어진 음성 엔진 조회
	TSharedPtr<FVoiceEngineImpl, ESPMode::ThreadSafe> FindEngine(UWorld* World);

	// 엔진 버퍼가 추적하지 않거나 정리 이후 다시 만들어져 종료 월드에 남은 원격 재생 컴포넌트 해제
	int32 ReleaseWorldVoiceSynthComponents(UWorld* World);

	// 새 캡처의 시작 성공 이후에만 기존 캡처를 교체하는 트랜잭션
	bool ReplaceCapture(TSharedPtr<IVoiceCapture>& CurrentCapture, TSharedPtr<IVoiceCapture> NewCapture);

	// 저장된 장치 ID를 실제 레거시 캡처 이름에 매핑한 입력 전환
	EApplyResult ApplyInputDevice(UWorld* World, FString DeviceId, FString& AppliedDeviceId,
		TWeakPtr<IVoiceCapture>& AppliedCapture, TWeakPtr<FVoiceEngineImpl, ESPMode::ThreadSafe>& AppliedEngine);
}
