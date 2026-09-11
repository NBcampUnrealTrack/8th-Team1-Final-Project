#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "World/Contamination/MD_ContaminationItem.h"

FVector2D UMD_SurfaceStainDefinition::GetHalfSize(int32 RemainingSize) const
{
	return SizeCm * (0.5f * FMD_SurfaceStainSize::LinearScale(RemainingSize));
}

bool UMD_SurfaceStainDefinition::ContainsUV(const FVector2D& UV) const
{
	if (UV.ContainsNaN() || UV.X < 0 || UV.X > 1 || UV.Y < 0 || UV.Y > 1) return false;
	if (FootprintMask.IsEmpty()) return true; // Legacy/custom definitions use their authored rectangle.
	constexpr int32 Resolution = 64;
	if (FootprintMask.Num() != Resolution * Resolution) return false;
	const int32 X = FMath::Min(FMath::FloorToInt(UV.X * Resolution), Resolution - 1);
	const int32 Y = FMath::Min(FMath::FloorToInt(UV.Y * Resolution), Resolution - 1);
	return FootprintMask[Y * Resolution + X] != 0;
}

bool UMD_SurfaceStainDefinition::IsUsable() const
{
	return static_cast<uint8>(Kind) <= static_cast<uint8>(EMD_SurfaceStainKind::Speckles)
		&& IsValid(DecalMaterial) && DecalMaterial->GetMaterial()
		&& DecalMaterial->GetMaterial()->MaterialDomain == MD_DeferredDecal
		&& !SizeCm.ContainsNaN() && SizeCm.X >= 1.0 && SizeCm.Y >= 1.0
		&& FMath::IsFinite(ProjectionDepthCm) && ProjectionDepthCm >= 1.0f && ProjectionDepthCm <= 1500.0f
		&& FMath::IsFinite(MinimumSurfaceCosine) && MinimumSurfaceCosine >= 0.1f && MinimumSurfaceCosine <= 1.0f
		&& (FootprintMask.IsEmpty() || FootprintMask.Num() == 64 * 64)
		&& RemovedItemClass && !RemovedItemClass->HasAnyClassFlags(CLASS_Abstract);
}
