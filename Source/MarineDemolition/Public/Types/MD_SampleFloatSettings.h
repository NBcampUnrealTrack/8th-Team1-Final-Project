#pragma once

#include "CoreMinimal.h"
#include "MD_SampleFloatSettings.generated.h"

/** Opt-in buoyancy around an authored horizontal water plane (world centimetres). */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_SampleFloatSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Float", meta = (DisplayName = "Float On Water"))
	bool bEnabled = false;

	/** World Z of the visible water surface. This does not query Water plugin waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Float", meta = (EditCondition = "bEnabled", Units = "cm"))
	float SurfaceZ = 0.f;

	/** Centre of mass height relative to the water surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Float", meta = (EditCondition = "bEnabled", Units = "cm"))
	float HeightOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Float", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "20", Units = "cm"))
	float BobAmplitude = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Float", meta = (EditCondition = "bEnabled", ClampMin = "1", Units = "s"))
	float BobPeriod = 3.f;

	/** Buoyancy only operates near this plane; inland/deep placements retain normal gravity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Float", AdvancedDisplay, meta = (EditCondition = "bEnabled", ClampMin = "30", Units = "cm"))
	float CaptureDepth = 100.f;

	bool IsValid() const
	{
		return FMath::IsFinite(SurfaceZ) && FMath::IsFinite(HeightOffset)
			&& FMath::IsFinite(BobAmplitude) && BobAmplitude >= 0.f && BobAmplitude <= 20.f
			&& FMath::IsFinite(BobPeriod) && BobPeriod >= 1.f
			&& FMath::IsFinite(CaptureDepth) && CaptureDepth >= 30.f;
	}

	FVector GetAcceleration(float CentreZ, const FVector& Velocity, float Time, float GravityZ) const
	{
		if (!bEnabled || !IsValid() || !FMath::IsFinite(CentreZ) || Velocity.ContainsNaN()
			|| !FMath::IsFinite(Time) || !FMath::IsFinite(GravityZ)) return FVector::ZeroVector;
		const float RestZ = SurfaceZ + HeightOffset;
		const float Depth = RestZ - CentreZ;
		if (Depth < -30.f || Depth > CaptureDepth) return FVector::ZeroVector;
		const float WaveZ = BobAmplitude * FMath::Sin(Time * 2.f * PI / BobPeriod);
		const float Weight = FMath::Clamp((Depth + 30.f) / 15.f, 0.f, 1.f);
		const float Lift = FMath::Clamp(-GravityZ + 12.f * (Depth + WaveZ) - 7.f * Velocity.Z, 0.f, 3000.f);
		return FVector(-Velocity.X * 1.5f, -Velocity.Y * 1.5f, Lift) * Weight;
	}
};
