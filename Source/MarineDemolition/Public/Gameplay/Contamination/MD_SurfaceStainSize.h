#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/** Shared integer cleaning budget. Visible area, not diameter, is proportional to this value. */
struct MARINEDEMOLITION_API FMD_SurfaceStainSize
{
	static constexpr int32 Minimum = 100;
	static constexpr int32 Maximum = 10000;
	static constexpr int32 Reference = 300;
	static constexpr int32 DefaultRemoval = 100;

	static bool IsValidRemaining(int32 Size) { return Size >= 0 && Size <= Maximum; }
	static int32 ClampInitial(int32 Size) { return FMath::Clamp(Size, Minimum, Maximum); }
	static float LinearScale(int32 Size) { return FMath::Sqrt(float(FMath::Clamp(Size, 0, Maximum)) / Reference); }
	static int32 FromLegacyStage(uint8 Stage) { return Stage <= 3 ? int32(Stage) * DefaultRemoval : INDEX_NONE; }
	static int32 RandomInitial(int32 Min, int32 Max, int32 Seed)
	{
		Min = ClampInitial(Min);
		Max = ClampInitial(Max);
		if (Min > Max) Swap(Min, Max);
		return FRandomStream(Seed).RandRange(Min, Max);
	}
};
