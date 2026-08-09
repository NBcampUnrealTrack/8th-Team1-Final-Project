#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_ScrapeSurfaceComponent.generated.h"

class UDecalComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class AMD_Barnacle;
class AMD_ContaminationItem;
class UMD_ScrapeSurfaceComponent;

/** One solid object that can block a scrape stroke on this surface. */
struct FMD_ScrapeSurfaceObstacle
{
	TWeakObjectPtr<AMD_Barnacle> Barnacle;
	EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
	FVector2D CenterCm = FVector2D::ZeroVector;
	float RadiusCm = 0.0f;
};

/** A compact replicated cell delta: uint16 cell index plus uint8 remaining amount. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellState
	: public FFastArraySerializerItem
{
	GENERATED_BODY()

	FMD_ReplicatedScrapeCellState()
		: EncodedCellIndex(FMD_ScrapeReplicationCodec::InvalidCellIndex)
		, QuantizedRemainingContamination(0)
	{
	}

	FMD_ReplicatedScrapeCellState(
		const uint16 InEncodedCellIndex,
		const uint8 InQuantizedRemainingContamination)
		: EncodedCellIndex(InEncodedCellIndex)
		, QuantizedRemainingContamination(InQuantizedRemainingContamination)
	{
	}

	int32 GetCellIndex() const
	{
		return FMD_ScrapeReplicationCodec::DecodeCellIndex(EncodedCellIndex);
	}

	float GetRemainingContamination() const
	{
		return FMD_ScrapeReplicationCodec::DequantizeRemaining(
			QuantizedRemainingContamination);
	}

	UPROPERTY()
	uint16 EncodedCellIndex;

	UPROPERTY()
	uint8 QuantizedRemainingContamination;
};

/** Fast Array containing only cells that differ from their generated value. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellStateArray
	: public FFastArraySerializer
{
	GENERATED_BODY()

public:
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
	{
		return FastArrayDeltaSerialize<
			FMD_ReplicatedScrapeCellState,
			FMD_ReplicatedScrapeCellStateArray>(
				Items,
				DeltaParameters,
				*this);
	}

	void SetOwner(UMD_ScrapeSurfaceComponent* InOwner);
	void SetCellRemaining(int32 CellIndex, float RemainingContamination);
	void ResetState();

	const TArray<FMD_ReplicatedScrapeCellState>& GetItems() const
	{
		return Items;
	}

	void PreReplicatedRemove(
		const TArrayView<int32>& RemovedIndices,
		int32 FinalSize);
	void PostReplicatedAdd(
		const TArrayView<int32>& AddedIndices,
		int32 FinalSize);
	void PostReplicatedChange(
		const TArrayView<int32>& ChangedIndices,
		int32 FinalSize);

private:
	UPROPERTY()
	TArray<FMD_ReplicatedScrapeCellState> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<UMD_ScrapeSurfaceComponent> Owner;

	TMap<int32, int32> CellToItemIndex;
};

template<>
struct TStructOpsTypeTraits<FMD_ReplicatedScrapeCellStateArray>
	: public TStructOpsTypeTraitsBase2<FMD_ReplicatedScrapeCellStateArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMD_OnScrapeSurfaceFullyRemoved,
	UMD_ScrapeSurfaceComponent*,
	Surface);

/**
 * Reusable contamination surface behavior.
 *
 * The owner supplies a static mesh surface. The component owns scrape cells,
 * replication, mask/decal state, barnacle obstacles, debris drops, and Day
 * persistence data. It deliberately does not know whether its owner is a wall,
 * floor, or piece of equipment. In network play the owner actor must replicate
 * so this replicated component has a network channel.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_ScrapeSurfaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_ScrapeSurfaceComponent();

	/** 피격/부착된 Primitive을 실제 작업면으로 쓰는 표면 컴포넌트를 찾는다. */
	static UMD_ScrapeSurfaceComponent* FindForPrimitiveComponent(
		const UPrimitiveComponent* PrimitiveComponent);

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Reuses native visualization subobjects supplied by legacy owners. */
	void ConfigureExternalComponents(
		UStaticMeshComponent* InSurfaceMesh,
		UInstancedStaticMeshComponent* InContaminationOverlay,
		UDecalComponent* InFrontContaminationDecal,
		UDecalComponent* InBackContaminationDecal);

	/** Rebuilds geometry and all runtime contamination state. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape Surface")
	void InitializeSurface();

	/** Refreshes editor/runtime geometry without resetting scrape progress. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape Surface")
	bool RefreshSurfaceGeometry();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	bool IsSurfaceInitialized() const
	{
		return bContaminationSurfaceInitialized;
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	UStaticMeshComponent* GetSurfaceMesh() const
	{
		return SurfaceMesh.Get();
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Mask")
	UTexture2D* GetContaminationMaskTexture() const
	{
		return ContaminationMaskTexture.Get();
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Mask")
	FIntPoint GetContaminationMaskDimensions() const
	{
		return ContaminationMaskAtlasDimensions;
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Decal")
	UDecalComponent* GetFrontContaminationDecal() const
	{
		return FrontContaminationDecal.Get();
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Decal")
	UDecalComponent* GetBackContaminationDecal() const
	{
		return BackContaminationDecal.Get();
	}

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Scrape Surface")
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

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	float GetContaminationCleanRatio() const;
	float GetContaminationCleanRatio(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	bool IsContaminationFullyRemoved() const;
	bool IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace Face) const;

	/** Used by demolition owners without coupling this component to wall types. */
	float GetContaminationDemolitionReadyRatio() const;

	void ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells);
	void ExportContaminationDeltas(
		TArray<int32>& OutCellIndices,
		TArray<float>& OutInitialAmounts,
		TArray<float>& OutRemainingAmounts) const;
	void RestoreContaminationCells(
		const TArray<int32>& CellIndices,
		const TArray<float>& InitialAmounts,
		const TArray<float>& RemainingAmounts);

	UPROPERTY(BlueprintAssignable, Category = "MD|Scrape Surface")
	FMD_OnScrapeSurfaceFullyRemoved OnContaminationFullyRemoved;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Mesh that defines the working surface. Auto-resolved from the owner if empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|Geometry")
	TObjectPtr<UStaticMeshComponent> SurfaceMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Debris")
	TSubclassOf<AMD_ContaminationItem> ContaminationItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Debris", meta = (ClampMin = "0", UIMax = "10"))
	int32 ContaminationItemsPerFace = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Debris", meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationItemSpawnOffsetCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface",
		meta = (UIMin = "1", UIMax = "128"))
	FIntPoint ContaminationGridDimensions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContaminationPerCell;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Procedural")
	bool bUseProceduralContamination;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "1.0", Units = "cm"))
	float ContaminationNoiseScaleCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Procedural",
		meta = (EditCondition = "bUseProceduralContamination"))
	int32 ContaminationNoiseSeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationCoverage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Procedural",
		meta = (EditCondition = "bUseProceduralContamination", ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationMinimumAmount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D ContaminationSurfaceSizeOverrideCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Geometry")
	EMD_ScrapeSurfaceNormalAxis ContaminationSurfaceNormalAxis;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface")
	FMD_ScrapeSettings ScrapeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Overlay", meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationOverlayOffsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Overlay", meta = (ClampMin = "0.01", Units = "cm"))
	float ContaminationOverlayThicknessCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Overlay")
	bool bShowContaminationOverlay;

	/** Assets used when the component creates its own fallback overlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Overlay")
	TObjectPtr<UStaticMesh> ContaminationOverlayMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Overlay")
	TObjectPtr<UMaterialInterface> ContaminationOverlayMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	bool bShowContaminationDecals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "0.01", UIMin = "0.1", Units = "cm"))
	float ContaminationDecalProjectionDepthCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	bool bAutoFitContaminationDecalDepth;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (EditCondition = "bAutoFitContaminationDecalDepth", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalDepthFitMarginCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalSurfaceOffsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalEdgeInsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	int32 ContaminationDecalSortOrder;

private:
	friend struct FMD_ReplicatedScrapeCellStateArray;
	friend class AMD_CuttableWall;
	friend class FMD_CuttableWallScrapeIntegrationTest;
	friend class FMD_CuttableWallArbitraryMeshTest;
	friend class FMD_BarnacleSpawnerTest;
	friend class FMD_BarnacleStrokeBlockingTest;

	static constexpr int32 MaximumContaminationGridAxis = 256;
	static constexpr float MinimumContaminationFaceNormalDot = 0.5f;
	static constexpr float ScrapeObstacleContactMarginCm = 0.05f;

	bool HasAuthority() const;
	void ResolveSurfaceMeshFromOwner();
	void EnsureVisualizationComponents();
	void DestroyOwnedVisualizationComponents();
	void InitializeRuntimeState();
	void ShutdownRuntimeState();

	TArray<FMD_ScrapeSurfaceObstacle> ScrapeObstacles;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<FMD_ScrapeSurfaceCell> ContaminationCells;

	float PendingContaminationItemAmounts[2] = { 0.0f, 0.0f };
	TArray<float> ContaminationGeneratedInitialAmounts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInstancedStaticMeshComponent> ContaminationOverlay;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDecalComponent> FrontContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDecalComponent> BackContaminationDecal;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ContaminationMaskTexture;

	TArray<uint8> ContaminationMaskAtlasPixels;
	FIntPoint ContaminationMaskAtlasDimensions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrontContaminationDecalMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackContaminationDecalMID;

	bool bContaminationDecalsConfigured;

	UPROPERTY(Replicated, Transient)
	FMD_ReplicatedScrapeCellStateArray ReplicatedContaminationState;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	FVector2D ResolvedContaminationSurfaceSizeCm;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	EMD_ScrapeSurfaceNormalAxis ResolvedContaminationSurfaceNormalAxis;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	float ResolvedFrontDecalDepthCm;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	float ResolvedBackDecalDepthCm;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	float ResolvedContaminationDecalEdgeInsetCm;

	FVector ContaminationSurfaceCenterLocal;
	FVector ContaminationMeshLocalBoundsMinimum;
	FVector ContaminationMeshLocalBoundsMaximum;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	float CachedFrontContaminationCleanRatio;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	float CachedBackContaminationCleanRatio;

	bool bContaminationSurfaceInitialized;
	bool bContaminationCompletionBroadcast;
	bool bOwnsContaminationOverlay = false;
	bool bOwnsFrontContaminationDecal = false;
	bool bOwnsBackContaminationDecal = false;
	TArray<FTransform> ContaminationCellBaseTransforms;

	TArrayView<FMD_ScrapeSurfaceCell> GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face);
	TConstArrayView<FMD_ScrapeSurfaceCell> GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face) const;

	void RefreshContaminationCleanRatioCache();
	void RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace Face);
	bool ResolveContaminationSurfaceGeometry();
	void InitializeContaminationSurface();
	bool TryApplyProceduralInitialContamination(
		int32 CellsPerFace,
		float BaseInitialContamination);
	bool InitializeContaminationMask();
	void ReleaseContaminationMask();
	void UpdateContaminationMask(
		const TArray<FMD_ScrapeCellChange>& CellChanges);
	bool UploadContaminationMaskRegion(const FIntRect& AtlasRegion);
	void ResolveContaminationDecalDepths();
	bool ConfigureContaminationDecals();
	bool ConfigureContaminationDecalForFace(
		UDecalComponent* DecalComponent,
		EMD_ScrapeSurfaceFace Face,
		TObjectPtr<UMaterialInstanceDynamic>& InOutMID);
	void SetContaminationDecalsVisible(bool bVisible);
	void RefreshContaminationDecalMaterialParameters();
	void InitializeContaminationVisualization();
	void UpdateContaminationVisualization(
		const TArray<FMD_ScrapeCellChange>& CellChanges);
	void HandleReplicatedContaminationState(
		TConstArrayView<int32> StateItemIndices,
		bool bResetToInitial);
	void ApplyAllReplicatedContaminationState();
	void PublishContaminationChanges(
		const TArray<FMD_ScrapeCellChange>& CellChanges);
	int32 DropContaminationItems(
		EMD_ScrapeSurfaceFace SurfaceFace,
		float RemovedAmount,
		const FVector2D& SurfacePositionCm);
	bool TryCalculateContaminationItemSpawnTransform(
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector2D& SurfacePositionCm,
		FTransform& OutSpawnTransform) const;
	FTransform MakeContaminationCellVisualTransform(int32 CellIndex) const;
	bool TryProjectWorldPointToContaminationSurface(
		const FVector& WorldPoint,
		FVector2D& OutSurfacePositionCm) const;
	bool TryGetContaminationFaceFromHit(
		const FHitResult& Hit,
		EMD_ScrapeSurfaceFace& OutSurfaceFace) const;
};
