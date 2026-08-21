#include "Gameplay/Scrape/MD_ScrapeReacquirePolicy.h"

FMD_ScrapeReacquireEvaluation FMD_ScrapeReacquirePolicy::Evaluate(
	const bool bHasValidSurface,
	const bool bHasPriorStroke,
	const bool bSameSurfaceAndFace,
	const double InvalidElapsedSeconds,
	const double ReconnectDistanceCm,
	const double CandidateMaxCm,
	const double EffectiveBridgeMaxCm,
	const bool bBridgeVisibilityValid,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences,
	const int32 MaximumMissingAimSequences,
	const double GraceSeconds,
	const double PersistentExitSeconds)
{
	FMD_ScrapeReacquireEvaluation Evaluation;
	const double SafeElapsedSeconds = FMath::IsFinite(InvalidElapsedSeconds)
		? FMath::Max(InvalidElapsedSeconds, 0.0)
		: TNumericLimits<double>::Max();
	const double SafePersistentExitSeconds =
		FMath::IsFinite(PersistentExitSeconds)
		? FMath::Max(PersistentExitSeconds, 0.01)
		: 0.01;
	if (SafeElapsedSeconds >= SafePersistentExitSeconds)
	{
		Evaluation.Decision = EMD_ScrapeReacquireDecision::TerminateFocus;
		return Evaluation;
	}

	if (!bHasValidSurface)
	{
		Evaluation.Decision = EMD_ScrapeReacquireDecision::KeepWaiting;
		return Evaluation;
	}

	if (!bHasPriorStroke)
	{
		Evaluation.Decision = EMD_ScrapeReacquireDecision::StartNewStroke;
		return Evaluation;
	}

	const double SafeDistanceCm = FMath::IsFinite(ReconnectDistanceCm)
		? FMath::Max(ReconnectDistanceCm, 0.0)
		: TNumericLimits<double>::Max();
	const double SafeCandidateMaxCm = FMath::IsFinite(CandidateMaxCm)
		? FMath::Max(CandidateMaxCm, 0.0)
		: 0.0;
	const double SafeBridgeMaxCm = FMath::IsFinite(EffectiveBridgeMaxCm)
		? FMath::Max(EffectiveBridgeMaxCm, 0.0)
		: 0.0;
	const double SafeGraceSeconds = FMath::IsFinite(GraceSeconds)
		? FMath::Max(GraceSeconds, 0.0)
		: 0.0;
	const bool bSequenceGapAllowed = !bSequenceGapKnown
		|| (MissingAimSequences >= 0
			&& MissingAimSequences <= FMath::Max(MaximumMissingAimSequences, 0));
	Evaluation.bIsReconnectCandidate = bSameSurfaceAndFace
		&& bSequenceGapAllowed
		&& SafeDistanceCm <= SafeCandidateMaxCm;
	Evaluation.bCanBridge = Evaluation.bIsReconnectCandidate
		&& bBridgeVisibilityValid
		&& SafeElapsedSeconds <= SafeGraceSeconds
		&& SafeDistanceCm <= SafeBridgeMaxCm;
	Evaluation.Decision = Evaluation.bCanBridge
		? EMD_ScrapeReacquireDecision::ContinueBridge
		: EMD_ScrapeReacquireDecision::StartNewStroke;
	return Evaluation;
}
