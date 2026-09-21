#pragma once

#include "CoreMinimal.h"

struct FHitResult;

/** Geometry shared by the Trash and Prop editor tools. */
namespace MDPlacementGeometry
{
	FVector GetSurfaceNormal(const FHitResult& Hit);
	double GetLowestProjection(const FBox& Bounds, const FTransform& Transform, const FVector& UnitNormal);
}
