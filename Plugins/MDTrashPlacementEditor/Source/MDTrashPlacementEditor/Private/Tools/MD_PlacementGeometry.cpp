#include "Tools/MD_PlacementGeometry.h"

#include "Engine/HitResult.h"

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
