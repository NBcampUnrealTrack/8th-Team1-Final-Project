#include "Tools/MD_PropPlacementSettings.h"

#include "Math/RandomStream.h"
#include "Templates/TypeHash.h"

namespace
{
	constexpr uint32 PitchChannel = 0x10C1u;
	constexpr uint32 YawChannel = 0x10C2u;
	constexpr uint32 RollChannel = 0x10C3u;
	constexpr uint32 ScaleChannel = 0x10C4u;

	double FiniteOr(double Value, double Fallback)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	double SampleFraction(int32 Seed, uint32 CandidateIndex, uint32 Channel)
	{
		const uint32 CandidateSeed = HashCombineFast(GetTypeHash(Seed), GetTypeHash(CandidateIndex));
		const FRandomStream Random(static_cast<int32>(HashCombineFast(CandidateSeed, Channel)));
		return Random.GetFraction();
	}
}

void FMD_PropRotationSettings::Sanitize()
{
	AngleDegrees = FRotator::NormalizeAxis(FiniteOr(AngleDegrees, 0.0));
	MinimumDegrees = FiniteOr(MinimumDegrees, -180.0);
	MaximumDegrees = FiniteOr(MaximumDegrees, 180.0);
	if (MinimumDegrees > MaximumDegrees)
	{
		Swap(MinimumDegrees, MaximumDegrees);
	}
}

void FMD_PropRotationSettings::Rotate(double DeltaDegrees)
{
	Sanitize();
	const double Delta = FiniteOr(DeltaDegrees, 0.0) * (bInvert ? -1.0 : 1.0);
	if (bRandomize)
	{
		// Shift the entire range without changing its width or the candidate's random fraction.
		MinimumDegrees += Delta;
		MaximumDegrees += Delta;
	}
	else
	{
		AngleDegrees += Delta;
	}
	Sanitize();
}

void FMD_PropRotationSettings::Freeze(double Fraction)
{
	if (bRandomize)
	{
		// Store the angle before inversion so the option and random range remain reusable.
		const double EffectiveAngle = Evaluate(Fraction);
		AngleDegrees = FRotator::NormalizeAxis(bInvert ? -EffectiveAngle : EffectiveAngle);
		bRandomize = false;
	}
}

double FMD_PropRotationSettings::Evaluate(double Fraction) const
{
	const double Degrees = bRandomize
		? MinimumDegrees * (1.0 - Fraction) + MaximumDegrees * Fraction : AngleDegrees;
	return FRotator::NormalizeAxis(bInvert ? -Degrees : Degrees);
}

bool FMD_PropRotationSettings::operator==(const FMD_PropRotationSettings& Other) const
{
	return bRandomize == Other.bRandomize && AngleDegrees == Other.AngleDegrees
		&& MinimumDegrees == Other.MinimumDegrees && MaximumDegrees == Other.MaximumDegrees && bInvert == Other.bInvert;
}

void FMD_PropPlacementSettings::Sanitize()
{
	LeanAngle = FMath::Clamp(FiniteOr(LeanAngle, 75.0), 15.0, 85.0);
	EdgeSearchDistance = FMath::Clamp(FiniteOr(EdgeSearchDistance, 5.0), 0.5, 25.0);
	Pitch.Sanitize();
	Roll.Sanitize();
	Yaw.Sanitize();
	UniformScale = FMath::Clamp(FiniteOr(UniformScale, 1.0), 0.01, 1000.0);
	MinimumScale = FMath::Clamp(FiniteOr(MinimumScale, 0.9), 0.01, 1000.0);
	MaximumScale = FMath::Clamp(FiniteOr(MaximumScale, 1.1), 0.01, 1000.0);
	if (MinimumScale > MaximumScale)
	{
		Swap(MinimumScale, MaximumScale);
	}
	SurfaceOffset = FiniteOr(SurfaceOffset, 0.0);
	OverlapTolerance = FMath::Max(FiniteOr(OverlapTolerance, 0.1), 0.0);
}

FMD_PropRotationSettings& FMD_PropPlacementSettings::GetRotationAxis(EMD_PropRotationAxis Axis)
{
	switch (Axis)
	{
	case EMD_PropRotationAxis::Pitch:
		return Pitch;
	case EMD_PropRotationAxis::Roll:
		return Roll;
	default:
		return Yaw;
	}
}

bool FMD_PropPlacementSettings::HasRandomizedValues() const
{
	return Pitch.bRandomize || Yaw.bRandomize || Roll.bRandomize || bRandomizeScale;
}

void FMD_PropPlacementSettings::FreezeRandomValues(uint32 CandidateIndex)
{
	Sanitize();
	Pitch.Freeze(SampleFraction(RandomSeed, CandidateIndex, PitchChannel));
	Yaw.Freeze(SampleFraction(RandomSeed, CandidateIndex, YawChannel));
	Roll.Freeze(SampleFraction(RandomSeed, CandidateIndex, RollChannel));
	if (bRandomizeScale)
	{
		UniformScale = EvaluateScale(CandidateIndex);
		bRandomizeScale = false;
	}
}

FQuat FMD_PropPlacementSettings::EvaluateRotation(uint32 CandidateIndex) const
{
	return FRotator(Pitch.Evaluate(SampleFraction(RandomSeed, CandidateIndex, PitchChannel)),
		Yaw.Evaluate(SampleFraction(RandomSeed, CandidateIndex, YawChannel)),
		Roll.Evaluate(SampleFraction(RandomSeed, CandidateIndex, RollChannel))).Quaternion();
}

double FMD_PropPlacementSettings::EvaluateScale(uint32 CandidateIndex) const
{
	return bRandomizeScale ? FMath::Lerp(MinimumScale, MaximumScale,
		SampleFraction(RandomSeed, CandidateIndex, ScaleChannel)) : UniformScale;
}

bool FMD_PropPlacementSettings::operator==(const FMD_PropPlacementSettings& Other) const
{
	return bAlignToSurface == Other.bAlignToSurface && bSmartEdge == Other.bSmartEdge
		&& LeanAngle == Other.LeanAngle && EdgeSearchDistance == Other.EdgeSearchDistance && Pitch == Other.Pitch && Roll == Other.Roll && Yaw == Other.Yaw
		&& bRandomizeScale == Other.bRandomizeScale && UniformScale == Other.UniformScale
		&& MinimumScale == Other.MinimumScale && MaximumScale == Other.MaximumScale
		&& SurfaceOffset == Other.SurfaceOffset && RandomSeed == Other.RandomSeed
		&& bPreventOverlaps == Other.bPreventOverlaps && OverlapTolerance == Other.OverlapTolerance;
}
