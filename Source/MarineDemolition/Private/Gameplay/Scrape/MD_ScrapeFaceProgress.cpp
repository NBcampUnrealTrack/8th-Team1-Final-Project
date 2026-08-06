#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"

float FMD_ScrapeFaceProgress::CalculateOverallCleanRatio(
	const float FrontCleanRatio,
	const float BackCleanRatio)
{
	return (SanitizeCleanRatio(FrontCleanRatio)
		+ SanitizeCleanRatio(BackCleanRatio)) * 0.5f;
}

float FMD_ScrapeFaceProgress::CalculateDemolitionReadyCleanRatio(
	const float FrontCleanRatio,
	const float BackCleanRatio)
{
	return FMath::Max(
		SanitizeCleanRatio(FrontCleanRatio),
		SanitizeCleanRatio(BackCleanRatio));
}

bool FMD_ScrapeFaceProgress::IsEitherFaceReadyForCut(
	const float FrontCleanRatio,
	const float BackCleanRatio,
	const float RequiredCleanRatio)
{
	const float SafeRequiredCleanRatio = FMath::IsFinite(RequiredCleanRatio)
		? FMath::Clamp(RequiredCleanRatio, 0.0f, 1.0f)
		: 1.0f;

	return SanitizeCleanRatio(FrontCleanRatio) >= SafeRequiredCleanRatio
		|| SanitizeCleanRatio(BackCleanRatio) >= SafeRequiredCleanRatio;
}

float FMD_ScrapeFaceProgress::SanitizeCleanRatio(const float CleanRatio)
{
	return FMath::IsFinite(CleanRatio)
		? FMath::Clamp(CleanRatio, 0.0f, 1.0f)
		: 0.0f;
}
