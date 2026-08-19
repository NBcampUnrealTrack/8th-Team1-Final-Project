#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"

int32 FMD_ScrapeFaceProgress::CalculateCleanPieceCount(
	const int32 TotalPieceCount,
	const float OverallCleanRatio)
{
	if (TotalPieceCount <= 0)
	{
		return 0;
	}

	// 제거율은 float로 생성되므로 조각 수 곱셈도 같은 정밀도에서 반올림한다.
	// double로 승격하면 0.35f가 0.349999...로 보존돼 3.5가 3으로 내려갈 수 있다.
	const int32 CleanPieceCount = FMath::RoundToInt(
		static_cast<float>(TotalPieceCount)
		* SanitizeCleanRatio(OverallCleanRatio));
	return FMath::Clamp(
		CleanPieceCount,
		0,
		TotalPieceCount);
}

float FMD_ScrapeFaceProgress::SanitizeCleanRatio(const float CleanRatio)
{
	return FMath::IsFinite(CleanRatio)
		? FMath::Clamp(CleanRatio, 0.0f, 1.0f)
		: 0.0f;
}
