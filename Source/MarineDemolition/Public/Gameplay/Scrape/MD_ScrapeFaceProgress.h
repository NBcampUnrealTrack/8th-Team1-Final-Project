#pragma once

#include "CoreMinimal.h"

/** 앞면과 뒷면의 제거율을 전체 진행도와 철거 규칙으로 합치는 순수 계산 유틸리티. */
class MARINEDEMOLITION_API FMD_ScrapeFaceProgress final
{
public:
	/** 잔해량/점수용 양면 전체 평균 제거 비율(0~1). */
	static float CalculateOverallCleanRatio(
		float FrontCleanRatio,
		float BackCleanRatio);

	/** 철거 준비도 표시용으로 두 면 중 더 높은 제거 비율(0~1)을 반환한다. */
	static float CalculateDemolitionReadyCleanRatio(
		float FrontCleanRatio,
		float BackCleanRatio);

	/** 앞면 또는 뒷면 중 하나라도 철거 기준을 만족하는지 검사한다. */
	static bool IsEitherFaceReadyForCut(
		float FrontCleanRatio,
		float BackCleanRatio,
		float RequiredCleanRatio);

private:
	FMD_ScrapeFaceProgress() = delete;

	static float SanitizeCleanRatio(float CleanRatio);
};
