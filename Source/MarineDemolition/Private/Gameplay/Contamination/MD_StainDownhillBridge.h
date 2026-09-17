#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
class FProgressCancel;

/** Finds supported descents below the placement plane. Authoring geometry only. */
class FMD_StainDownhillBridge final
{
public:
	static bool Gather(const UE::Geometry::FDynamicMesh3& Surface, const UE::Geometry::FDynamicMesh3& LandingSurface, const FVector2D& HalfSize,
		TArray<UE::Geometry::FDynamicMesh3>& Wedges, FString& Error, FProgressCancel* Progress, bool bOverlapLandings = false);
};
#endif
