#include "Gameplay/Scrape/MD_ContaminationNoise.h"

namespace
{
	constexpr int32 FractalOctaveCount = 3;
	constexpr float FractalPersistence = 0.5f;
	constexpr uint32 OctaveSeedStep = 0x9E3779B9u;

	uint32 MixHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

    float HashLatticeZ(const uint32 XYPrefix, const int32 Z)
    {
        const uint32 Hash = MixHash(XYPrefix ^ static_cast<uint32>(Z));
		return static_cast<float>(Hash & 0x00FFFFFFu)
			/ static_cast<float>(0x00FFFFFFu);
	}

	float SmoothInterpolationAlpha(const float Alpha)
	{
		const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return SafeAlpha * SafeAlpha * (3.0f - 2.0f * SafeAlpha);
	}
}

float FMD_ContaminationNoise::ValueNoise3D(
	const FVector& Position,
	const int32 Seed)
{
	if (Position.ContainsNaN())
	{
		return 0.0f;
	}

	const int32 X0 = FMath::FloorToInt(Position.X);
	const int32 Y0 = FMath::FloorToInt(Position.Y);
	const int32 Z0 = FMath::FloorToInt(Position.Z);
	const int32 X1 = X0 + 1;
	const int32 Y1 = Y0 + 1;
	const int32 Z1 = Z0 + 1;
	const float XAlpha = SmoothInterpolationAlpha(
		static_cast<float>(Position.X - static_cast<double>(X0)));
	const float YAlpha = SmoothInterpolationAlpha(
		static_cast<float>(Position.Y - static_cast<double>(Y0)));
	const float ZAlpha = SmoothInterpolationAlpha(
		static_cast<float>(Position.Z - static_cast<double>(Z0)));

    // Nearby samples reuse the same eight lattice corners. Each worker owns four
    // fixed entries; full keys make seed-slot collisions harmless. Interpolation
    // and float conversion are unchanged, including negative lattice coordinates.
    struct FLatticeCorners
    {
        int32 X = 0, Y = 0, Z = 0, Seed = 0;
        float Values[8] = {};
        bool bValid = false;
    };
    static thread_local FLatticeCorners Cache[4];
    auto& Entry = Cache[static_cast<uint32>(Seed) & 3u];
    if (!Entry.bValid || Entry.X != X0 || Entry.Y != Y0 || Entry.Z != Z0 || Entry.Seed != Seed)
    {
        const uint32 SeedPrefix = MixHash(static_cast<uint32>(Seed));
        const uint32 X0Prefix = MixHash(SeedPrefix ^ static_cast<uint32>(X0));
        const uint32 X1Prefix = MixHash(SeedPrefix ^ static_cast<uint32>(X1));
        const uint32 XY00 = MixHash(X0Prefix ^ static_cast<uint32>(Y0));
        const uint32 XY10 = MixHash(X1Prefix ^ static_cast<uint32>(Y0));
        const uint32 XY01 = MixHash(X0Prefix ^ static_cast<uint32>(Y1));
        const uint32 XY11 = MixHash(X1Prefix ^ static_cast<uint32>(Y1));
        Entry.Values[0] = HashLatticeZ(XY00, Z0);
        Entry.Values[1] = HashLatticeZ(XY10, Z0);
        Entry.Values[2] = HashLatticeZ(XY01, Z0);
        Entry.Values[3] = HashLatticeZ(XY11, Z0);
        Entry.Values[4] = HashLatticeZ(XY00, Z1);
        Entry.Values[5] = HashLatticeZ(XY10, Z1);
        Entry.Values[6] = HashLatticeZ(XY01, Z1);
        Entry.Values[7] = HashLatticeZ(XY11, Z1);
        Entry.X = X0; Entry.Y = Y0; Entry.Z = Z0; Entry.Seed = Seed;
        Entry.bValid = true;
    }
    const float X00 = FMath::Lerp(Entry.Values[0], Entry.Values[1], XAlpha);
    const float X10 = FMath::Lerp(Entry.Values[2], Entry.Values[3], XAlpha);
    const float X01 = FMath::Lerp(Entry.Values[4], Entry.Values[5], XAlpha);
    const float X11 = FMath::Lerp(Entry.Values[6], Entry.Values[7], XAlpha);
	const float Y0Value = FMath::Lerp(X00, X10, YAlpha);
	const float Y1Value = FMath::Lerp(X01, X11, YAlpha);
	return FMath::Clamp(
		FMath::Lerp(Y0Value, Y1Value, ZAlpha),
		0.0f,
		1.0f);
}

float FMD_ContaminationNoise::FractalNoise3D(
	const FVector& WorldPositionCm,
	const float NoiseScaleCm,
	const int32 Seed)
{
	if (WorldPositionCm.ContainsNaN()
		|| !FMath::IsFinite(NoiseScaleCm)
		|| NoiseScaleCm <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	float Total = 0.0f;
	float WeightSum = 0.0f;
	float Frequency = 1.0f;
	float Weight = 1.0f;
	for (int32 Octave = 0; Octave < FractalOctaveCount; ++Octave)
	{
		const FVector SamplePosition =
			(WorldPositionCm / static_cast<double>(NoiseScaleCm))
			* static_cast<double>(Frequency);
		const uint32 OctaveSeed = static_cast<uint32>(Seed)
			+ static_cast<uint32>(Octave) * OctaveSeedStep;
		Total += ValueNoise3D(
			SamplePosition,
			static_cast<int32>(OctaveSeed)) * Weight;
		WeightSum += Weight;
		Frequency *= 2.0f;
		Weight *= FractalPersistence;
	}

	return WeightSum > UE_SMALL_NUMBER
		? FMath::Clamp(Total / WeightSum, 0.0f, 1.0f)
		: 0.0f;
}

bool FMD_ContaminationNoise::TryCalculateInitialContamination(
	const FVector& WorldPositionCm,
	const float BaseAmount,
	const float NoiseScaleCm,
	const int32 Seed,
	const float Coverage,
	const float MinimumAmount,
	float& OutInitialContamination)
{
	OutInitialContamination = 0.0f;
	if (WorldPositionCm.ContainsNaN()
		|| !FMath::IsFinite(BaseAmount)
		|| !FMath::IsFinite(NoiseScaleCm)
		|| !FMath::IsFinite(Coverage)
		|| !FMath::IsFinite(MinimumAmount)
		|| NoiseScaleCm <= UE_SMALL_NUMBER
		|| Coverage < 0.0f
		|| Coverage > 1.0f
		|| MinimumAmount < 0.0f
		|| MinimumAmount > 1.0f)
	{
		return false;
	}

	const float SafeBaseAmount = FMath::Clamp(BaseAmount, 0.0f, 1.0f);
	if (SafeBaseAmount <= UE_SMALL_NUMBER || Coverage <= UE_SMALL_NUMBER)
	{
		return true;
	}

	const float NoiseValue = FractalNoise3D(
		WorldPositionCm,
		NoiseScaleCm,
		Seed);
	const float CoverageThreshold = 1.0f - Coverage;
	if (NoiseValue <= CoverageThreshold)
	{
		return true;
	}

	const float CoverageAlpha = FMath::Clamp(
		(NoiseValue - CoverageThreshold) / Coverage,
		0.0f,
		1.0f);
	const float ShapedAlpha = SmoothInterpolationAlpha(CoverageAlpha);
	OutInitialContamination = SafeBaseAmount * FMath::Lerp(
		MinimumAmount,
		1.0f,
		ShapedAlpha);
	return true;
}
