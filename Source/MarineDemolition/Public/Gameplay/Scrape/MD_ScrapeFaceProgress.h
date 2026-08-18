#pragma once

#include "CoreMinimal.h"

/** 오염 제거율을 벽 조각 개수로 변환하는 순수 계산 유틸리티. */
class MARINEDEMOLITION_API FMD_ScrapeFaceProgress final
{
public:
	/** 전체 제거율에 맞춰 깨끗한 벽 조각 수를 반올림해 계산한다. */
	static int32 CalculateCleanPieceCount(
		int32 TotalPieceCount,
		float OverallCleanRatio);

private:
	FMD_ScrapeFaceProgress() = delete;

	static float SanitizeCleanRatio(float CleanRatio);
};
