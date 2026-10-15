#pragma once
#include "CoreMinimal.h"

class UMD_SurfaceStainDefinition;
class UStaticMesh;
class UMaterialInterface;
class AMD_SurfaceStain;
struct FMD_StainStickerPatch;
struct FMD_StainSurfaceFitJob;
class FProgressCancel;

/** Editor authoring only. The game consumes ordinary immutable volume definitions. */
class MARINEDEMOLITION_API FMD_StainSurfaceFit
{
public:
#if WITH_EDITOR
	/** Full-stage intrinsic spans at size 300, cached by original asset and authoring revision. */
	static bool GetSourceDimensions(const UMD_SurfaceStainDefinition& Source, FVector& OutDimensions);
	/** Read existing sizes through 10000 without changing their stored cleaning budget. */
	static bool GetSizeDimensions(const UMD_SurfaceStainDefinition& Source, int32 Size,
		FVector& OutDimensions, bool bFollowSurface = true);
	/** New-placement width limits, including the original profile's limits when surface fitting is off. */
	static bool GetAllowedWidthRange(const UMD_SurfaceStainDefinition& Source, bool bFollowSurface,
		FVector2D& OutAllowedWidth, FString& Error);
	/** Convert an in-range intrinsic width to the existing integer budget; never silently clamp input. */
	static bool ResolveWidth(const UMD_SurfaceStainDefinition& Source, double RequestedWidthCm,
		bool bFollowSurface, int32& OutSize, FVector& OutAppliedDimensions,
		FVector2D& OutAllowedWidth, FString& Error);
	static TSharedPtr<FMD_StainSurfaceFitJob, ESPMode::ThreadSafe> Capture(const UMD_SurfaceStainDefinition& Source);
	static bool Prepare(FMD_StainSurfaceFitJob& Job, const FMD_StainStickerPatch& Surface, int32 Size,
		FString& Error, FProgressCancel* Progress = nullptr);
	/** Lower-density full-shape estimate only; never saved or used for removal stages. */
	static bool PreparePreview(FMD_StainSurfaceFitJob& Job, const FMD_StainStickerPatch& Surface, int32 Size,
		FProgressCancel* Progress = nullptr);
	static UStaticMesh* CreatePreview(UObject& Owner, UMaterialInterface* Material, const FMD_StainSurfaceFitJob& Job);
	static UMD_SurfaceStainDefinition* Create(UObject& Owner, const UMD_SurfaceStainDefinition& Source,
		const FMD_StainSurfaceFitJob& Job, FString& Error);
	static FVector2D HalfSize(const UMD_SurfaceStainDefinition& Source, int32 Size);
	static UMD_SurfaceStainDefinition* Build(UObject& Owner, const UMD_SurfaceStainDefinition& Source,
		const FMD_StainStickerPatch& Surface, int32 Size, FString& Error);
	/** Normal-build actor-owned meshes from prepared geometry, without refitting or saving a separate asset. */
	static UMD_SurfaceStainDefinition* BuildFinal(AMD_SurfaceStain& Owner,
		const UMD_SurfaceStainDefinition& Prepared, FString& Error);
#endif
};
