#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_ScrapeSurfaceComponent.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UTexture2D;
struct FMD_ScrapeDecalFaceLayout;
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

/** A compact replicated cell delta with base and transferred-paint state. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellState
	: public FFastArraySerializerItem
{
	GENERATED_BODY()

	FMD_ReplicatedScrapeCellState()
		: EncodedCellIndex(FMD_ScrapeReplicationCodec::InvalidCellIndex)
		, QuantizedInitialContamination(0)
		, QuantizedRemainingContamination(0)
		, QuantizedTransferredContamination(0)
		, QuantizedTransferredPatternDensity(0)
	{
	}

	FMD_ReplicatedScrapeCellState(
		const uint16 InEncodedCellIndex,
		const uint8 InQuantizedInitialContamination,
		const uint8 InQuantizedRemainingContamination,
		const uint8 InQuantizedTransferredContamination = 0,
		const uint8 InQuantizedTransferredPatternDensity = 0)
		: EncodedCellIndex(InEncodedCellIndex)
		, QuantizedInitialContamination(InQuantizedInitialContamination)
		, QuantizedRemainingContamination(InQuantizedRemainingContamination)
		, QuantizedTransferredContamination(
			InQuantizedTransferredContamination)
		, QuantizedTransferredPatternDensity(
			InQuantizedTransferredPatternDensity)
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

	float GetInitialContamination() const
	{
		return FMD_ScrapeReplicationCodec::DequantizeRemaining(
			QuantizedInitialContamination);
	}

	float GetTransferredContamination() const
	{
		return FMD_ScrapeReplicationCodec::DequantizeRemaining(
			QuantizedTransferredContamination);
	}

	float GetTransferredPatternDensity() const
	{
		return FMD_ScrapeReplicationCodec::DequantizeRemaining(
			QuantizedTransferredPatternDensity);
	}

	UPROPERTY()
	uint16 EncodedCellIndex;

	UPROPERTY()
	uint8 QuantizedInitialContamination;

	UPROPERTY()
	uint8 QuantizedRemainingContamination;

	UPROPERTY()
	uint8 QuantizedTransferredContamination;

	UPROPERTY()
	uint8 QuantizedTransferredPatternDensity;
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
	void SetCellState(int32 CellIndex);
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

	/** Whether this surface participates in contamination at all. */
	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Contamination")
	bool IsContaminationEnabled() const
	{
		return bContaminationEnabled;
	}

	/**
	 * Enables or disables the complete contamination feature at runtime.
	 * Disabling discards runtime cells/masks; enabling starts from the
	 * configured initial contamination again.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Scrape Surface|Contamination")
	void SetContaminationEnabled(bool bEnabled);

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Reuses native decal subobjects supplied by legacy owners. */
	void ConfigureExternalComponents(
		UStaticMeshComponent* InSurfaceMesh,
		UDecalComponent* InFrontContaminationDecal,
		UDecalComponent* InBackContaminationDecal);

	/** Reapplies the artist-authored appearance overrides to every active MID. */
	UFUNCTION(BlueprintCallable, CallInEditor,
		Category = "MD|Scrape Surface|Appearance")
	void RefreshContaminationAppearance();

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
	UTexture2D* GetTransferredPaintMaskTexture() const
	{
		return TransferredPaintMaskTexture.Get();
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

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Decal")
	int32 GetFrontContaminationDecalPatchCount() const
	{
		return !bContaminationEnabled
			? 0
			: (FrontContaminationDecalPatches.IsEmpty()
			? (FrontContaminationDecal ? 1 : 0)
			: FrontContaminationDecalPatches.Num());
	}

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Decal")
	int32 GetBackContaminationDecalPatchCount() const
	{
		return !bContaminationEnabled
			? 0
			: (BackContaminationDecalPatches.IsEmpty()
			? (BackContaminationDecal ? 1 : 0)
			: BackContaminationDecalPatches.Num());
	}

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Scrape Surface")
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithToolSpec(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec);

	/** 서버에서 일반 제거 후 경로의 무작위 지점에 헤라 접촉면 모양으로 새 오염을 덧칠한다. */
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithContaminationStamp(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		float StampRatio,
		float& OutRemovedCleanRatioDelta);

	FMD_ScrapeResult ApplyLocalScrapeStrokeWithContaminationStamp(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FVector2D& StampPathStartPositionCm,
		const FVector2D& StampPathAxis,
		float StampRatio,
		float StampCoverage,
		float StampDepthCm,
		float StampSpacingCm,
		int32 StampPatternSeed,
		float& OutRemovedCleanRatioDelta);

	/** 입력 종료 시 보호 거리 뒤에 남겨 둔 마지막 오염 도장 구간을 적용한다. */
	void FlushLocalContaminationPaintTail(
		const FMD_ScrapeStroke& FullPathStroke,
		const FVector2D& StampPathAxis,
		const FMD_ScrapeToolSpec& ToolSpec,
		float StampRatio,
		float StampCoverage,
		float StampDepthCm,
		float StampSpacingCm,
		int32 StampPatternSeed);

	bool TryGetScrapeSurfacePositionFromHit(
		const FHitResult& Hit,
		FVector2D& OutSurfacePositionCm,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);

	/** Resolves a world-space receiver point to the same face/2D coordinates used by scraping. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape Surface")
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
	void ExportContaminationDeltas(
		TArray<int32>& OutCellIndices,
		TArray<float>& OutInitialAmounts,
		TArray<float>& OutRemainingAmounts,
		TArray<float>& OutTransferredAmounts,
		TArray<float>& OutTransferredPatternDensities) const;
	void RestoreContaminationCells(
		const TArray<int32>& CellIndices,
		const TArray<float>& InitialAmounts,
		const TArray<float>& RemainingAmounts);
	void RestoreContaminationCells(
		const TArray<int32>& CellIndices,
		const TArray<float>& InitialAmounts,
		const TArray<float>& RemainingAmounts,
		const TArray<float>& TransferredAmounts,
		const TArray<float>& TransferredPatternDensities);

	UPROPERTY(BlueprintAssignable, Category = "MD|Scrape Surface")
	FMD_OnScrapeSurfaceFullyRemoved OnContaminationFullyRemoved;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Mesh that defines the working surface. Auto-resolved from the owner if empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|Geometry",
		meta = (NoEditInline))
	TObjectPtr<UStaticMeshComponent> SurfaceMesh;

	/**
	 * Master switch for contamination state, rendering, scraping, persistence,
	 * and replication on this surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		ReplicatedUsing = OnRep_ContaminationEnabled,
		Category = "MD|Scrape Surface|Contamination")
	bool bContaminationEnabled;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Debris")
	TSubclassOf<AMD_ContaminationItem> ContaminationItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Debris", meta = (ClampMin = "0", UIMax = "10"))
	int32 ContaminationItemsPerFace = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Debris", meta = (ClampMin = "0.0", Units = "cm"))
	float ContaminationItemSpawnOffsetCm = 12.0f;

	/** 면당 게임플레이 해상도. 큰 표면은 2.5cm/셀 이하가 되도록 런타임에 자동 보강된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface",
		meta = (UIMin = "1", UIMax = "256"))
	FIntPoint ContaminationGridDimensions;

	/** 새 페인트 전용 고해상도 마스크의 목표 texel 크기. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Mask",
		meta = (ClampMin = "0.25", UIMin = "0.25", UIMax = "2.0", Units = "cm"))
	float TransferredPaintVisualTexelSizeCm;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	/**
	 * Optional artist override for the tiled grime color. Null keeps the
	 * ContamBaseColor texture inherited from the assigned material or MI.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Appearance",
		meta = (DisplayName = "Base Color Texture"))
	TObjectPtr<UTexture2D> ContaminationBaseColorTexture;

	/**
	 * Optional artist override for large deposits and accent shapes. Black
	 * pixels keep the base grime layer; they no longer remove contamination.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Appearance",
		meta = (DisplayName = "Detail Mask Texture"))
	TObjectPtr<UTexture2D> ContaminationShapeMaskTexture;

	/** Minimum visual coverage left where the detail mask is black. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Appearance",
		meta = (DisplayName = "Base Coverage", ClampMin = "0.0",
			ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ContaminationBaseCoverage;

	/** Overall opacity of the contamination layer before the scrape mask. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Appearance",
		meta = (DisplayName = "Overall Opacity", ClampMin = "0.0",
			ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ContaminationOpacity;

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

	/**
	 * Coverage-first planar decal mode. Front and back keep a logical ownership
	 * partition, while both physical projection volumes span the complete mesh
	 * thickness. Scrape hits use a reliable receiver normal first, with the
	 * spatial split reserved for nearly tangent or otherwise ambiguous normals.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (EditCondition = "bAutoFitContaminationDecalDepth"))
	bool bUseFullCoverageContaminationDecals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (EditCondition = "bAutoFitContaminationDecalDepth", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalDepthFitMarginCm;

	/**
	 * Extra outward reach used only by full-coverage front/back decals. This
	 * captures curved return faces without changing the inward depth or logical
	 * front/back ownership split.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (DisplayName = "Full Coverage Decal Outer Margin",
			EditCondition = "bAutoFitContaminationDecalDepth && bUseFullCoverageContaminationDecals",
			ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float FullCoverageContaminationDecalOuterMarginCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalSurfaceOffsetCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ContaminationDecalEdgeInsetCm;

	/** Fit each patch to collision samples. Disable for a planar decal control. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	bool bFitContaminationDecalPatchesToSurface;

	/** Target horizontal width of one physical decal projection patch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "1.0", UIMin = "25.0", Units = "cm"))
	float ContaminationDecalPatchWidthCm;

	/** Physical size represented by one repeat of the grime detail textures. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal",
		meta = (ClampMin = "1.0", UIMin = "25.0", Units = "cm"))
	float ContaminationDetailTileWorldSizeCm;

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
	static constexpr float MaximumContaminationCellSizeForPaintCm = 2.5f;
	static constexpr int32 MaximumTransferredPaintMaskAxis = 1024;
	static constexpr float MinimumContaminationFaceNormalDot = 0.5f;
	static constexpr float MinimumFullCoverageFaceNormalDot = 0.05f;
	static constexpr float ScrapeObstacleContactMarginCm = 0.05f;
	static constexpr int32 MaximumContaminationDecalPatchesPerFace = 32;

	bool HasAuthority() const;
	void ResolveSurfaceMeshFromOwner();
	void EnsureVisualizationComponents();
	void DestroyOwnedVisualizationComponents();
	void InitializeRuntimeState();
	void ShutdownRuntimeState();
	void ApplyContaminationEnabledState();

	UFUNCTION()
	void OnRep_ContaminationEnabled();

	TArray<FMD_ScrapeSurfaceObstacle> ScrapeObstacles;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<FMD_ScrapeSurfaceCell> ContaminationCells;

	float PendingContaminationItemAmounts[2] = { 0.0f, 0.0f };
	TArray<float> ContaminationGeneratedInitialAmounts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime",
		meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UDecalComponent> FrontContaminationDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime",
		meta = (AllowPrivateAccess = "true", NoEditInline))
	TObjectPtr<UDecalComponent> BackContaminationDecal;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UDecalComponent>> FrontContaminationDecalPatches;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<UDecalComponent>> BackContaminationDecalPatches;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ContaminationMaskTexture;

	TArray<uint8> ContaminationMaskAtlasPixels;
	FIntPoint ContaminationMaskAtlasDimensions;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> TransferredPaintMaskTexture;

	TArray<uint8> TransferredPaintMaskAtlasPixels;
	FIntPoint TransferredPaintMaskAtlasDimensions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrontContaminationDecalMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackContaminationDecalMID;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FrontContaminationDecalPatchMIDs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BackContaminationDecalPatchMIDs;

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

	/** True only after the requested full-coverage depth partition was validated. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bResolvedFullCoverageContaminationDecals;

	double ResolvedContaminationFaceSplitCoordinateLocal;

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
	bool bOwnsFrontContaminationDecal = false;
	bool bOwnsBackContaminationDecal = false;
	TArrayView<FMD_ScrapeSurfaceCell> GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face);
	TConstArrayView<FMD_ScrapeSurfaceCell> GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face) const;
	FMD_ScrapeResult ApplyLocalScrapeStrokeInternal(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FVector2D& StampPathStartPositionCm,
		const FVector2D& StampPathAxis,
		float StampRatio,
		float StampCoverage,
		float StampDepthCm,
		float StampSpacingCm,
		int32 StampPatternSeed,
		float& OutRemovedCleanRatioDelta);

	void RefreshContaminationCleanRatioCache();
	void RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace Face);
	bool ResolveContaminationSurfaceGeometry();
	void InitializeContaminationSurface();
	bool TryApplyProceduralInitialContamination(
		int32 CellsPerFace,
		float BaseInitialContamination);
	bool InitializeContaminationMask();
	void ReleaseContaminationMask();
	FMD_ScrapeSurfaceCell MakeContaminationMaskCell(int32 CellIndex) const;
	void UpdateContaminationMask(
		const TArray<FMD_ScrapeCellChange>& CellChanges);
	bool UploadContaminationMaskRegion(const FIntRect& AtlasRegion);
	bool InitializeTransferredPaintMask();
	void ReleaseTransferredPaintMask();
	void UpdateTransferredPaintMask(
		const TArray<FMD_ScrapeCellChange>& CellChanges);
	bool UploadTransferredPaintMaskRegion(const FIntRect& AtlasRegion);
	bool TryCalculateTransferredPaintMaskDimensions(
		FIntPoint& OutFaceDimensions,
		FIntPoint& OutAtlasDimensions) const;
	uint8 CalculateTransferredPaintMaskPixel(
		EMD_ScrapeSurfaceFace Face,
		const FIntPoint& FacePixelCoordinate,
		const FIntPoint& FaceMaskDimensions) const;
	void ResolveContaminationDecalDepths();
	bool ConfigureContaminationDecals();
	bool ConfigureContaminationDecalPatchesForFace(
		UDecalComponent* BaseDecalComponent,
		EMD_ScrapeSurfaceFace Face,
		TArray<TObjectPtr<UDecalComponent>>& OutPatchComponents,
		TArray<TObjectPtr<UMaterialInstanceDynamic>>& OutPatchMIDs);
	bool ConfigureContaminationDecalPatch(
		UDecalComponent* DecalComponent,
		EMD_ScrapeSurfaceFace Face,
		const FMD_ScrapeDecalFaceLayout& Layout,
		TObjectPtr<UMaterialInstanceDynamic>& InOutMID);
	void ApplyContaminationFaceOwnershipParameters(
		UMaterialInstanceDynamic& DecalMID,
		EMD_ScrapeSurfaceFace Face) const;
	void ApplyContaminationDetailWorldFrame(
		UMaterialInstanceDynamic& DecalMID) const;
	void ApplyContaminationAppearanceParameters(
		UMaterialInstanceDynamic& DecalMID,
		UMaterialInterface* ParentMaterial) const;
	bool TryResolveContaminationFace(
		const FVector& WorldPosition,
		const FVector& WorldNormal,
		EMD_ScrapeSurfaceFace& OutSurfaceFace) const;
	bool TryResolveContaminationFaceFromWorldPosition(
		const FVector& WorldPosition,
		EMD_ScrapeSurfaceFace& OutSurfaceFace) const;
	UDecalComponent* CreateAdditionalContaminationDecalPatch(
		EMD_ScrapeSurfaceFace Face,
		int32 PatchIndex);
	void DestroyAdditionalContaminationDecalPatches();
	bool TryFitContaminationDecalPatchToSurface(
		EMD_ScrapeSurfaceFace Face,
		FMD_ScrapeDecalFaceLayout& InOutLayout) const;
	void SetContaminationDecalsVisible(bool bVisible);
	void RefreshContaminationDecalMaterialParameters();
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
	bool TryProjectWorldPointToContaminationSurface(
		const FVector& WorldPoint,
		FVector2D& OutSurfacePositionCm) const;
	bool TryGetContaminationFaceFromHit(
		const FHitResult& Hit,
		EMD_ScrapeSurfaceFace& OutSurfaceFace) const;
};
