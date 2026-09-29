#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMesh;
class UMaterialInterface;
class UMD_SurfaceStainDefinition;
struct FMD_StainStickerPatch;

#if WITH_EDITOR
struct FMD_StainStickerAsyncResult;
struct FMD_StainStickerSourceCache;
/** One cancellable authoring job; destruction never waits for a worker. */
class MARINEDEMOLITION_API FMD_StainStickerAsyncBake final
{
public:
	~FMD_StainStickerAsyncBake();
	bool IsReady() const;
	bool IsPreviewReady() const;
	UStaticMesh* FinishPreview(AActor& Owner, UMaterialInterface* Material);
	bool IsCancelled() const;
	void Cancel();
	bool SourcesAreCurrent() const;
	UMD_SurfaceStainDefinition* FinishVolume(AActor& Owner, const UMD_SurfaceStainDefinition& Definition, FString& Error);
	bool Finish(AActor& Owner, UMaterialInterface* Material, FMD_StainStickerPatch& OutPatch, UStaticMesh*& OutMesh, FString& Error);
private:
	friend class FMD_StainStickerBaker;
	TSharedPtr<FMD_StainStickerAsyncResult, ESPMode::ThreadSafe> Result;
};
#endif

/** Editor-only topology/UV authoring. Runtime only consumes the immutable baked result. */
class MARINEDEMOLITION_API FMD_StainStickerBaker final
{
public:
#if WITH_EDITOR
	static UStaticMesh* CreateMesh(AActor& Owner, const FMD_StainStickerPatch& Patch, UMaterialInterface* Material);
	static TSharedPtr<FMD_StainStickerSourceCache> CreateSourceCache();
	static TSharedPtr<FMD_StainStickerAsyncBake> StartAsync(AActor& Owner, const FVector2D& HalfSize, int32 InitialSize,
		const TSharedPtr<FMD_StainStickerSourceCache>& Cache = nullptr, const UMD_SurfaceStainDefinition* FitDefinition = nullptr, bool bPreparePreview = true);
	static bool Build(AActor& Owner, const FVector2D& HalfSize, int32 InitialSize,
		UMaterialInterface* Material, FMD_StainStickerPatch& OutPatch, UStaticMesh*& OutMesh, FString& OutError, bool bSurfaceWalk = false);
#endif
};
