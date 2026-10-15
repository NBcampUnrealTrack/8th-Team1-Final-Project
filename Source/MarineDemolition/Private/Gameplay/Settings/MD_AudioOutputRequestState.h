#pragma once

#include "HAL/CriticalSection.h"

struct FMD_AudioOutputRequestState
{
	// 게임 스레드 취소와 오디오 스레드 플랫폼 접근의 동시 실행 방지
	FCriticalSection Mutex;
	// 플랫폼 전환 접수 전에 확인할 요청 취소 상태
	bool bCancelled = false;
};
