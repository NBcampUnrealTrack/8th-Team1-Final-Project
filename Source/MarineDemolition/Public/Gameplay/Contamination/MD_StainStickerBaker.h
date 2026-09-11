#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMesh;
class UMaterialInterface;
struct FMD_StainStickerPatch;

/** Editor-only topology/UV authoring. Runtime only consumes the immutable baked result. */
class MARINEDEMOLITION_API FMD_StainStickerBaker final
{
public:
#if WITH_EDITOR
	static bool Build(AActor& Owner, const FVector2D& HalfSize, int32 InitialSize,
		UMaterialInterface* Material, FMD_StainStickerPatch& OutPatch, UStaticMesh*& OutMesh, FString& OutError);
#endif
};
