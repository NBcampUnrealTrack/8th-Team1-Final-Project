#pragma once

#include "CoreMinimal.h"

struct FHitResult;
class UWorld;
class UPrimitiveComponent;

/** Geometry shared by the Trash and Prop editor tools. */
namespace MDPlacementGeometry
{
	FVector GetSurfaceNormal(const FHitResult& Hit);
	double GetLowestProjection(const FBox& Bounds, const FTransform& Transform, const FVector& UnitNormal);
	/** Refine only traced static-mesh supports against their triangle collision; keep other blockers conservative. */
	bool HasBlockingOverlap(UWorld* World, const FBox& Bounds, const FTransform& Transform,
		double Tolerance, const UPrimitiveComponent* Surface, const UPrimitiveComponent* SecondSurface = nullptr);
}
