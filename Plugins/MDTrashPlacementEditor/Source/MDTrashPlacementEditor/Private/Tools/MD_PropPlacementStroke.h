#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"

/** Distance-based sampling only. The tool must trace each proposed sample back onto a real surface. */
struct FMD_PropPlacementStrokePath
{
	void Reset();
	void Begin(const FHitResult& Hit);
	void Sample(const FHitResult& Hit, double Spacing, TArray<FHitResult>& OutSamples);

private:
	FHitResult PreviousHit;
	double DistanceSinceSample = 0.0;
	bool bHasPreviousHit = false;
};
