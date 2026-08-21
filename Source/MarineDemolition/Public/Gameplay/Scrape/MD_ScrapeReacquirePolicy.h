#pragma once

#include "CoreMinimal.h"

/** 조준을 놓쳤다가 다시 잡았을 때 진행 중이던 stroke를 어떻게 할지. */
enum class EMD_ScrapeReacquireDecision : uint8
{
	/** 아직 표면을 못 찾았다. 세션은 유지한 채 기다린다. */
	KeepWaiting,

	/** 끊긴 구간을 이어 붙여 같은 stroke를 계속한다. */
	ContinueBridge,

	/** 이어 붙이기 조건을 못 채웠다. 새 stroke로 시작한다. */
	StartNewStroke,

	/** 너무 오래 놓쳤다. 집중 세션 자체를 끝낸다. */
	TerminateFocus
};

/** 재획득 판정 결과. 결정과 함께 그 근거가 된 중간 판단도 남긴다. */
struct FMD_ScrapeReacquireEvaluation
{
	EMD_ScrapeReacquireDecision Decision =
		EMD_ScrapeReacquireDecision::KeepWaiting;

	/** 같은 면에서 충분히 가까운 곳을 다시 잡았는가. */
	bool bIsReconnectCandidate = false;

	/** 후보인 데다 가시성·시간·거리 조건까지 모두 만족했는가. */
	bool bCanBridge = false;
};

/**
 * 끊긴 조준을 다시 잡았을 때의 처리를 결정하는 순수 정책.
 *
 * 트레이스 결과와 시간·거리 수치만 받으므로 월드나 컴포넌트를 참조하지 않는다.
 * 판정 기준을 여기 한곳에 모아 두면 "왜 stroke가 끊겼는가"를 테스트로 고정할 수 있다.
 */
class MARINEDEMOLITION_API FMD_ScrapeReacquirePolicy final
{
public:
	static FMD_ScrapeReacquireEvaluation Evaluate(
		bool bHasValidSurface,
		bool bHasPriorStroke,
		bool bSameSurfaceAndFace,
		double InvalidElapsedSeconds,
		double ReconnectDistanceCm,
		double CandidateMaxCm,
		double EffectiveBridgeMaxCm,
		bool bBridgeVisibilityValid,
		bool bSequenceGapKnown,
		int32 MissingAimSequences,
		int32 MaximumMissingAimSequences,
		double GraceSeconds,
		double PersistentExitSeconds);

private:
	FMD_ScrapeReacquirePolicy() = delete;
};
