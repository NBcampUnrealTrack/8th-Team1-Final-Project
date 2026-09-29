#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/** Shared integer cleaning budget. Visible area, not diameter, is proportional to this value. */
struct MARINEDEMOLITION_API FMD_SurfaceStainSize
{
	static constexpr int32 Minimum = 100;
	// Existing level actors and saves retain the original accepted range.
	static constexpr int32 Maximum = 10000;
	static constexpr int32 MaximumPlacement = 5000;
	static constexpr int32 Reference = 300;
	// Legacy stage migration must remain 100 units per old stage.
	static constexpr int32 DefaultRemoval = 100;
	static constexpr int32 DefaultToolStrength = 100;
	static constexpr int32 DefaultStrokeRemoval = MaximumPlacement / 5;

	static bool IsValidRemaining(int32 Size) { return Size >= 0 && Size <= Maximum; }
	static int32 ClampInitial(int32 Size) { return FMath::Clamp(Size, Minimum, Maximum); }
	static int32 ClampPlacement(int32 Size) { return FMath::Clamp(Size, Minimum, MaximumPlacement); }
	/** Tool strength is independent of stored size: 100 strength removes 1000 size units. */
	static int32 RemovalFromToolStrength(int32 Strength)
	{
		if (Strength <= 0) return 0;
		return int32(FMath::Min<int64>(int64(Strength) * DefaultStrokeRemoval / DefaultToolStrength, Maximum));
	}
	static float LinearScale(int32 Size) { return FMath::Sqrt(float(FMath::Clamp(Size, 0, Maximum)) / Reference); }
	static int32 FromLegacyStage(uint8 Stage) { return Stage <= 3 ? int32(Stage) * DefaultRemoval : INDEX_NONE; }
	static int32 RandomInitial(int32 Min, int32 Max, int32 Seed)
	{
		Min = ClampPlacement(Min);
		Max = ClampPlacement(Max);
		if (Min > Max) Swap(Min, Max);
		return FRandomStream(Seed).RandRange(Min, Max);
	}
};
