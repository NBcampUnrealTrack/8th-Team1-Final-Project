#pragma once

#include "CoreMinimal.h"

class UMD_SurfaceStainDefinition;
class UPrimitiveComponent;
class UWorld;

/** Bounded editor-only fitting of pre-authored shapes; no mesh generation or runtime tracing. */
struct FMD_StainEdgePlacement
{
	static bool Resolve(UWorld& World, UMD_SurfaceStainDefinition& Definition, UPrimitiveComponent& Surface,
		const FTransform& Hint, int32 InitialSize, FTransform& OutContact, FString& Error);
	static bool Validate(UWorld& World, UMD_SurfaceStainDefinition& Definition, UPrimitiveComponent& Surface,
		const FTransform& Contact, int32 InitialSize, FString& Error);
};
