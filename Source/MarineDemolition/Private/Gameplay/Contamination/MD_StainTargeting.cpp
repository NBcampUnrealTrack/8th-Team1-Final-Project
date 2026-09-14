#include "Gameplay/Contamination/MD_StainTargeting.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Contamination/MD_SurfaceStain.h"

bool FMD_StainTargeting::Trace(UWorld& World, const FVector& Origin, const FVector& Direction, double Reach,
	FHitResult& OutHit, const AActor* IgnoredActor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Pick);
	if (Origin.ContainsNaN() || Direction.ContainsNaN() || !Direction.IsNormalized() || !FMath::IsFinite(Reach) || Reach <= 0) return false;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDStainPick), true, IgnoredActor);
	World.LineTraceMultiByChannel(Hits, Origin, Origin + Direction * Reach, ECC_Visibility, Params);
	const FHitResult* Surface = Hits.FindByPredicate([](const FHitResult& Hit) { return Hit.bBlockingHit; });
	// The world remains the occluder. A bridge may be hit before it, or without a backing hit.
	double Nearest = Surface ? FMath::Min(Reach, double(Surface->Distance) + 0.02) : Reach;
	bool bFound = false;
	for (const FHitResult& Candidate : Hits)
	{
		if (Candidate.bBlockingHit) break;
		AMD_SurfaceStain* Stain = Cast<AMD_SurfaceStain>(Candidate.GetActor());
		if (!Stain) continue;
		double Distance = Nearest;
		const bool bHit = Stain->HasSticker() ? Stain->TraceSticker(Origin, Direction, Nearest, Distance)
			: Surface && Stain->ContainsSurfacePoint(Surface->ImpactPoint, Surface->ImpactNormal);
		if (!bHit) continue;
		Nearest = Distance; bFound = true;
		OutHit = Candidate;
		OutHit.Distance = float(Distance);
		OutHit.ImpactPoint = OutHit.Location = Origin + Direction * Distance;
	}
	return bFound;
}
