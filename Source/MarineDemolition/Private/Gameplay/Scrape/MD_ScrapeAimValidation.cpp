#include "Gameplay/Scrape/MD_ScrapeAimValidation.h"

float FMD_ScrapeAimValidation::CalculateRotatorErrorDegrees(
	const FRotator& A,
	const FRotator& B)
{
	if (A.ContainsNaN() || B.ContainsNaN())
	{
		return TNumericLimits<float>::Max();
	}
	const double Dot = FMath::Clamp(
		FMath::Abs(A.Quaternion() | B.Quaternion()), 0.0, 1.0);
	return FMath::RadiansToDegrees(
		static_cast<float>(2.0 * FMath::Acos(Dot)));
}

bool FMD_ScrapeAimValidation::IsPositionDiscontinuous(
	const FMD_ScrapeAimQueryResult& PreviousAim,
	const FMD_ScrapeAimQueryResult& CurrentAim,
	const double MaximumDistanceCm)
{
	if (!PreviousAim.IsActionable() || !CurrentAim.IsActionable())
	{
		return false;
	}
	const double SafeMaximumDistanceCm = FMath::IsFinite(MaximumDistanceCm)
		? FMath::Max(MaximumDistanceCm, 0.0)
		: 0.0;
	return PreviousAim.WorldContactLocation.ContainsNaN()
		|| CurrentAim.WorldContactLocation.ContainsNaN()
		|| FVector::Distance(
			PreviousAim.WorldContactLocation,
			CurrentAim.WorldContactLocation) > SafeMaximumDistanceCm;
}

float FMD_ScrapeAimValidation::CalculateSampleDeltaTime(
	const double PreviousClientSeconds,
	const double CurrentClientSeconds,
	const float MaximumSampleAgeSeconds,
	const double MaximumSampleRateHz,
	bool& bOutIsDiscontinuous)
{
	bOutIsDiscontinuous = !FMath::IsFinite(PreviousClientSeconds)
		|| !FMath::IsFinite(CurrentClientSeconds)
		|| CurrentClientSeconds <= PreviousClientSeconds;
	if (bOutIsDiscontinuous)
	{
		return 0.0f;
	}
	const double RawDeltaSeconds =
		CurrentClientSeconds - PreviousClientSeconds;
	const double MaximumContinuousSeconds = static_cast<double>(
		FMath::Max(MaximumSampleAgeSeconds, 0.01f));
	if (RawDeltaSeconds > MaximumContinuousSeconds)
	{
		bOutIsDiscontinuous = true;
		return 0.0f;
	}
	const double SafeSampleRateHz = FMath::IsFinite(MaximumSampleRateHz)
		? FMath::Max(MaximumSampleRateHz, 1.0)
		: 1.0;
	return static_cast<float>(FMath::Clamp(
		RawDeltaSeconds,
		1.0 / SafeSampleRateHz,
		MaximumContinuousSeconds));
}

bool FMD_ScrapeAimValidation::IsDirectionInsideCone(
	const FVector& CameraRelativeDirection,
	const FMD_ScrapeAimAuthorityCone& Cone)
{
	if (CameraRelativeDirection.ContainsNaN())
	{
		return false;
	}
	const FVector Direction = CameraRelativeDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() || Direction.X <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const double SafeMargin = FMath::Clamp(
		static_cast<double>(Cone.PointerSafeMargin), 0.0, 0.49);
	const double MaxScreenCoordinate = 1.0 - (2.0 * SafeMargin);
	if (!FMath::IsFinite(Cone.HorizontalHalfTangent)
		|| !FMath::IsFinite(Cone.VerticalHalfTangent)
		|| Cone.HorizontalHalfTangent <= UE_SMALL_NUMBER
		|| Cone.VerticalHalfTangent <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const double HorizontalLimit =
		static_cast<double>(Cone.HorizontalHalfTangent)
		* MaxScreenCoordinate;
	const double VerticalLimit =
		static_cast<double>(Cone.VerticalHalfTangent)
		* MaxScreenCoordinate;
	const double MarginTangent = FMath::Tan(FMath::DegreesToRadians(
		static_cast<double>(FMath::Max(Cone.MarginDegrees, 0.0f))));
	return FMath::Abs(Direction.Y / Direction.X)
		<= HorizontalLimit + MarginTangent
		&& FMath::Abs(Direction.Z / Direction.X)
			<= VerticalLimit + MarginTangent;
}
