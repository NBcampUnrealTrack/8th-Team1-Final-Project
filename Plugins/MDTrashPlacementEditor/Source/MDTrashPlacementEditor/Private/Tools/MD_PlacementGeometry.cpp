#include "Tools/MD_PlacementGeometry.h"

#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FVector MDPlacementGeometry::GetSurfaceNormal(const FHitResult& Hit)
{
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	return Normal.IsNearlyZero() ? FVector::UpVector : Normal;
}

double MDPlacementGeometry::GetLowestProjection(
	const FBox& Bounds, const FTransform& Transform, const FVector& UnitNormal)
{
	const FVector ScaledNormal = Transform.GetRotation().UnrotateVector(UnitNormal) * Transform.GetScale3D();
	return FVector::DotProduct(UnitNormal, Transform.GetLocation())
		+ FVector::DotProduct(ScaledNormal, Bounds.GetCenter())
		- FVector::DotProduct(ScaledNormal.GetAbs(), Bounds.GetExtent());
}

bool MDPlacementGeometry::HasBlockingOverlap(UWorld* World, const FBox& Bounds, const FTransform& Transform,
	double Tolerance, const UPrimitiveComponent* Surface, const UPrimitiveComponent* SecondSurface)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_PlacementSupportOverlap);
	if (!IsValid(World) || !Bounds.IsValid || !Transform.IsValid())
	{
		return true;
	}
	const FVector Extent = (Bounds.GetExtent() * Transform.GetScale3D().GetAbs()
		- FVector(FMath::Max(Tolerance, 0.0))).ComponentMax(FVector(0.01));
	const FVector Center = Transform.TransformPosition(Bounds.GetCenter());
	const FCollisionShape Box = FCollisionShape::MakeBox(Extent);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MDPlacementOverlap), false);
	Query.bIgnoreTouches = true;
	TArray<FOverlapResult> Overlaps;
	if (!World->OverlapMultiByChannel(Overlaps, Center, Transform.GetRotation(), ECC_Pawn, Box, Query))
	{
		return false;
	}
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (!Overlap.bBlockingHit)
		{
			continue;
		}
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!Component || (Component != Surface && Component != SecondSurface))
		{
			return true;
		}
		// Missing triangle collision must never turn a simple-only support into a non-blocker.
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
		const UBodySetup* Body = MeshComponent ? MeshComponent->GetBodySetup() : nullptr;
		if (!Body || Body->TriMeshGeometries.IsEmpty())
		{
			return true;
		}
	}
	// Surface picking is complex too. Recheck at the same accuracy instead of rejecting the
	// contact merely because a convex hull protrudes beyond the mesh's visible surface.
	Query.bTraceComplex = true;
	return World->OverlapBlockingTestByChannel(Center, Transform.GetRotation(), ECC_Pawn, Box, Query);
}
