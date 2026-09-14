#pragma once

#include "CoreMinimal.h"
#include "MD_StainStickerPatch.generated.h"

/** Immutable, surface-local triangles with unfolded sticker coordinates, baked in the editor. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_StainStickerPatch
{
	GENERATED_BODY()
	static constexpr int32 MaximumTriangles = 4096;
	UPROPERTY() TArray<FVector3f> Positions;
	UPROPERTY() TArray<FVector2f> UVs;
	UPROPERTY() int32 InitialSize = 0;
	UPROPERTY() FBox LocalBounds = FBox(ForceInit);

	bool IsValid() const;
	bool FindUV(const FVector& LocalPoint, const FVector& LocalNormal, FVector2D& OutUV) const;
	static float UVScale(int32 RemainingSize, int32 BakedSize);
};
