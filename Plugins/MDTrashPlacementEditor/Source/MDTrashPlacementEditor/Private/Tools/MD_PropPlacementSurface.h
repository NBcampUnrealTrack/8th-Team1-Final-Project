#pragma once

#include "CoreMinimal.h"

class UWorld;
class UPrimitiveComponent;
struct FHitResult;
struct FCollisionQueryParams;
struct FMD_PropPlacementCandidate;

struct FMD_PropSurfacePlacement
{
	FTransform Transform = FTransform::Identity;
	TWeakObjectPtr<UPrimitiveComponent> Surface;
	TWeakObjectPtr<UPrimitiveComponent> Floor;
};

/** Bounded editor queries for a thin prop resting against an edge and on a lower floor. */
namespace MDPropPlacementSurface
{
	FMD_PropSurfacePlacement Solve(UWorld* World, const FMD_PropPlacementCandidate& Candidate,
		const FHitResult& Hit, uint32 CandidateIndex, const FCollisionQueryParams& Query);
	bool IsSafe(UWorld* World, const FMD_PropPlacementCandidate& Candidate, const FMD_PropSurfacePlacement& Placement);
}
