#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_CuttableWall.generated.h"

class UDecalComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture2D;
class UMD_ScrapeSurfaceComponent;
class AMD_CuttableWall;
class AMD_Barnacle;
class AMD_ContaminationItem;
class AMD_WallPiece;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMD_OnContaminationFullyRemoved,
	AMD_CuttableWall*,
	Wall);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnStrutCollapsed,
	AMD_CuttableWall*,
	Wall,
	AActor*,
	Cutter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnWallDemolished,
	AMD_CuttableWall*,
	Wall,
	AActor*,
	Cutter);

UENUM(BlueprintType)
enum class EMD_WallType : uint8
{
	Partition UMETA(DisplayName = "칸막이격벽"),
	Strut UMETA(DisplayName = "지주격벽"),
};

/**
 * A demolishable wall.
 *
 * Wall demolition remains here. Reusable contamination behavior lives in
 * UMD_ScrapeSurfaceComponent and this actor keeps compatibility wrappers for
 * existing Blueprints and callers.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_CuttableWall : public AActor
{
	GENERATED_BODY()

public:
	AMD_CuttableWall();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void SetSupported(bool bSupported) { bHasSupport = bSupported; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool IsSupported() const { return bHasSupport; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	EMD_WallType GetWallType() const { return WallType; }

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	UMD_ScrapeSurfaceComponent* GetScrapeSurfaceComponent() const
	{
		return ScrapeSurface.Get();
	}

	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnStrutCollapsed OnStrutCollapsed;

	UPROPERTY(BlueprintAssignable, Category = "MD|Wall")
	FMD_OnWallDemolished OnWallDemolished;

	UPROPERTY(BlueprintAssignable, Category = "MD|Wall|Contamination")
	FMD_OnContaminationFullyRemoved OnContaminationFullyRemoved;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Wall")
	void Cut(AActor* Cutter);

	UFUNCTION(BlueprintPure, Category = "MD|Wall")
	bool CanCut() const;

	UFUNCTION(BlueprintCallable, Category = "MD|Wall")
	void ScanHighlight(float Duration = 3.0f);

	// Compatibility façade. New surface-aware code should use the component.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHits(
		const FHitResult& StartHit,
		const FHitResult& EndHit);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyScrapeStrokeFromHitsWithToolSpec(
		const FHitResult& StartHit,
		const FHitResult& EndHit,
		const FMD_ScrapeToolSpec& ToolSpec);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStroke(const FMD_ScrapeStroke& Stroke);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Wall|Contamination")
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithToolSpec(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec);

	bool TryGetScrapeSurfacePositionFromHit(
		const FHitResult& Hit,
		FVector2D& OutSurfacePositionCm,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);
	bool TryGetScrapeSurfacePositionFromWorldPoint(
		const FVector& WorldPoint,
		const FVector& WorldNormal,
		FVector2D& OutSurfacePositionCm,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);
	bool TryGetScrapeSurfaceWorldPosition(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		FVector& OutWorldPosition) const;
	bool TryGetScrapeSurfaceWorldPositionAtDepth(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector& ReferenceWorldPosition,
		FVector& OutWorldPosition) const;

	bool RegisterScrapeObstacle(AMD_Barnacle* Barnacle);
	void UnregisterScrapeObstacle(AMD_Barnacle* Barnacle);
	bool TryClampScrapeStrokeAgainstObstacles(
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector2D& StartPositionCm,
		const FVector2D& EndPositionCm,
		float ToolHalfWidthCm,
		FVector2D& OutClampedEndPositionCm,
		AMD_Barnacle*& OutBlockingBarnacle) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	float GetContaminationCleanRatio() const;
	float GetContaminationCleanRatio(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination",
		meta = (DisplayName = "Get Contamination Clean Ratio For Face"))
	float GetContaminationCleanRatioForFace(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	float GetContaminationRemovedPercent() const;
	float GetContaminationRemovedPercent(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination",
		meta = (DisplayName = "Get Contamination Removed Percent For Face"))
	float GetContaminationRemovedPercentForFace(
		EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	float GetResidualContaminationRatio() const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination")
	bool IsContaminationFullyRemoved() const;
	bool IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination",
		meta = (DisplayName = "Is Contamination Fully Removed For Face"))
	bool IsContaminationFullyRemovedForFace(
		EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Mask")
	UTexture2D* GetContaminationMaskTexture() const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Mask")
	FIntPoint GetContaminationMaskDimensions() const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Decal")
	UDecalComponent* GetFrontContaminationDecal() const;

	UFUNCTION(BlueprintPure, Category = "MD|Wall|Contamination|Decal")
	UDecalComponent* GetBackContaminationDecal() const;

	void ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells);
	void ExportContaminationDeltas(
		TArray<int32>& OutCellIndices,
		TArray<float>& OutInitialAmounts,
		TArray<float>& OutRemainingAmounts) const;
	void RestoreContaminationCells(
		const TArray<int32>& CellIndices,
		const TArray<float>& InitialAmounts,
		const TArray<float>& RemainingAmounts);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "MD|Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Existing native visual subobjects are retained so old BP overrides survive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Overlay")
	TObjectPtr<UInstancedStaticMeshComponent> ContaminationOverlay;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UDecalComponent> FrontContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UDecalComponent> BackContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination")
	TObjectPtr<UMD_ScrapeSurfaceComponent> ScrapeSurface;

	UPROPERTY(EditAnywhere, Category = "MD|Wall")
	EMD_WallType WallType = EMD_WallType::Partition;

	UPROPERTY(EditAnywhere, Replicated, Category = "MD|Wall")
	bool bHasSupport = false;

	UPROPERTY(EditAnywhere, Category = "MD|Wall")
	int32 PieceCount = 10;

	UPROPERTY(EditDefaultsOnly, Category = "MD|Wall")
	TSubclassOf<AMD_WallPiece> PieceClass;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	/*
	 * Legacy BP configuration façade.
	 * These names and types stay on the actor so existing BP_MD_CuttableWall
	 * serialized values keep loading. OnConstruction copies them to ScrapeSurface.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris")
	TSubclassOf<AMD_ContaminationItem> ContaminationItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris",
		meta = (ClampMin = "0", UIMax = "10"))
	int32 ContaminationItemsPerFace = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Debris",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationItemSpawnOffsetCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination", meta = (UIMin = "1", UIMax = "128"))
	FIntPoint ContaminationGridDimensions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContaminationPerCell;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Procedural")
	bool bUseProceduralContamination;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "1.0", Units = "cm"))
	float ContaminationNoiseScaleCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination"))
	int32 ContaminationNoiseSeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationCoverage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationMinimumAmount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D ContaminationSurfaceSizeOverrideCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Geometry")
	EMD_ScrapeSurfaceNormalAxis ContaminationSurfaceNormalAxis;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination")
	FMD_ScrapeSettings ScrapeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Overlay",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationOverlayOffsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Overlay",
		meta = (ClampMin = "0.01", Units = "cm"))
	float ContaminationOverlayThicknessCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Overlay")
	bool bShowContaminationOverlay;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	bool bShowContaminationDecals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal",
		meta = (ClampMin = "0.01", UIMin = "0.1", Units = "cm"))
	float ContaminationDecalProjectionDepthCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	bool bAutoFitContaminationDecalDepth;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal",
		meta = (EditCondition = "bAutoFitContaminationDecalDepth", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalDepthFitMarginCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalSurfaceOffsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalEdgeInsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Wall|Contamination|Decal")
	int32 ContaminationDecalSortOrder;

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float PartitionCutGate = 0.0f;

	UPROPERTY(EditAnywhere, Category = "MD|Wall|Contamination|Cut",
		meta = (ClampMin = "0", ClampMax = "100"))
	float StrutCutGate = 0.0f;

private:
	friend class FMD_CuttableWallScrapeIntegrationTest;
	friend class FMD_CuttableWallArbitraryMeshTest;
	friend class FMD_BarnacleSpawnerTest;
	friend class FMD_BarnacleStrokeBlockingTest;

	void SyncLegacySurfaceConfiguration();
	void InitializeContaminationSurface();
	bool InitializeContaminationMask();
	bool ConfigureContaminationDecals();

	UFUNCTION()
	void HandleScrapeSurfaceFullyRemoved(
		UMD_ScrapeSurfaceComponent* RemovedSurface);

	void RestoreScanColor();

	bool bCut = false;
	FTimerHandle ScanTimerHandle;
};
