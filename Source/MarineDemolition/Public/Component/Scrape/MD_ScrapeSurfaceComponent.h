#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ContaminationGeneration.h"
#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"
#include "Gameplay/Scrape/MD_ScrapeMaskPixels.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "Gameplay/Scrape/MD_ScrapePagedState.h"
#include "Components/ActorComponent.h"
#include "Component/Scrape/MD_ContaminationSolidVisualComponent.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeInitialState.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_ScrapeSurfaceComponent.generated.h"

struct FMD_ScrapeBoundaryVisual;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UTexture2D;
class UMD_ScrapeToolComponent;
class UMD_ScrapeMeshUVData;
struct FMD_ScrapeMeshUVStroke;
struct FMD_ScrapeDecalFaceLayout;
class AMD_ContaminationItem;
class UMD_ScrapeSurfaceComponent;

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
		, QuantizedCoveredFraction(0)
	{
	}

	FMD_ReplicatedScrapeCellState(
		const uint32 InEncodedCellIndex,
		const uint8 InQuantizedInitialContamination,
		const uint8 InQuantizedRemainingContamination,
		const uint8 InQuantizedTransferredContamination = 0,
		const uint8 InQuantizedTransferredPatternDensity = 0,
		const uint8 InQuantizedCoveredFraction = 0)
		: EncodedCellIndex(InEncodedCellIndex)
		, QuantizedInitialContamination(InQuantizedInitialContamination)
		, QuantizedRemainingContamination(InQuantizedRemainingContamination)
		, QuantizedTransferredContamination(
			InQuantizedTransferredContamination)
		, QuantizedTransferredPatternDensity(
			InQuantizedTransferredPatternDensity)
		, QuantizedCoveredFraction(InQuantizedCoveredFraction)
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

	float GetCoveredFraction() const
	{
		return FMD_ScrapeReplicationCodec::DequantizeRemaining(
			QuantizedCoveredFraction);
	}

	UPROPERTY()
	uint32 EncodedCellIndex;

	UPROPERTY()
	uint8 QuantizedInitialContamination;

	UPROPERTY()
	uint8 QuantizedRemainingContamination;

	UPROPERTY()
	uint8 QuantizedTransferredContamination;

	UPROPERTY()
	uint8 QuantizedTransferredPatternDensity;

	/**
	 * 헤라 날에 덮인 면적 비율. 시각 전용이지만 복제해야 한다.
	 *
	 * 클라이언트는 셀 델타만 받고 스트로크 기하는 모른다. 이 값 없이 잔여량에서
	 * 역산하면 얇은 막과 부분 피복이 다시 한 값으로 뭉개져, 서버에서는 곧은
	 * 자국 경계가 클라이언트에서만 격자 계단으로 보인다.
	 */
	UPROPERTY()
	uint8 QuantizedCoveredFraction;
};

/** Fast Array containing only cells that differ from their generated value. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ReplicatedScrapeCellStateArray
	: public FFastArraySerializer
{
	GENERATED_BODY()

public:
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters);

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

/**
 * 서버가 성공한 한 번의 긁기 뒤 현재 렌더 peer에만 보내는 작은 절대 상태 묶음.
 *
 * FastArray와 ScrapeLoad property가 저장/JIP/손실 복구의 정본이다. 이 값은
 * 같은 서버 확정 상태를 그 복제보다 먼저 화면에 반영할 뿐 gameplay 권위를
 * 가지지 않는다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ScrapeAuthoritativeVisualDelta
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMD_ScrapeSurfaceComponent> Surface = nullptr;

	/** Surface 전체에서 증가하는 서버 확정 시각 revision. 0은 무효다. */
	UPROPERTY()
	uint32 SurfaceVisualRevision = 0;

	/** 바뀐 셀의 최종 1/255 상태. 상대 제거량이 아니라 절대값이다. */
	UPROPERTY()
	TArray<FMD_ReplicatedScrapeCellState> CellStates;

	/** 같은 transaction이 끝난 뒤 서버의 절대 Tool Load. */
	UPROPERTY()
	float ScrapeLoadAfter = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMD_OnScrapeSurfaceFullyRemoved,
	UMD_ScrapeSurfaceComponent*,
	Surface);

/**
 * Reusable contamination surface behavior.
 *
 * The owner supplies a static mesh surface. The component owns scrape cells,
 * replication, mask/decal state, debris drops, and Day
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
    bool ExportContaminationBoundary(TArray<uint8>& Out) const;
    bool ValidateContaminationBoundary(TConstArrayView<uint8> Bytes) const;
    bool RestoreContaminationBoundary(TConstArrayView<uint8> Bytes, uint32 VisualRevision=0, FGuid Epoch=FGuid());

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

    virtual void Serialize(FArchive& Ar) override;

    UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface|Procedural")
    int32 GetResolvedContaminationSeed() const { return ContaminationGeneration.Seed; }

    const FMD_ContaminationGeneration& GetContaminationGeneration() const { return ContaminationGeneration; }
    FMD_ContaminationGeneration MakeContaminationGeneration(bool bLegacy = false) const;
    bool RestoreContaminationGeneration(const FMD_ContaminationGeneration& Generation, bool bResetState = false);

	/** Refreshes editor/runtime geometry without resetting scrape progress. */
	UFUNCTION(BlueprintCallable, Category = "MD|Scrape Surface")
	bool RefreshSurfaceGeometry();

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	bool IsSurfaceInitialized() const
	{
		return bContaminationSurfaceInitialized;
	}

    /** Rebuilds only the derived solid display; gameplay state is preserved. */
    UFUNCTION(BlueprintCallable, Category="MD|Scrape Surface|Solid")
    void RefreshSolidContaminationVisual();
    /** Explicit preview avoids rebuilding large meshes on selection or every construction run. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="MD|Scrape Surface|Solid")
    void PreviewSolidContamination();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="MD|Scrape Surface|Solid")
    void ClearSolidContaminationPreview();
    UFUNCTION(BlueprintPure, Category="MD|Scrape Surface|Solid")
    FString GetSolidContaminationStats() const;
    /** GT-only candidate query before Actor BeginPlay; late changes retain the synchronous fallback. */
    bool TryGetSolidPrewarmInputs(const UMD_ScrapeMeshUVData*& OutData,
        FVector& OutWallScale, FMD_ContaminationSolidSettings& OutSettings);
    bool TryGetSolidVisualWorldContact(const FVector2D& SurfacePositionCm,
        const FVector& BaseWorldPosition, const FVector& WorldNormal, FVector& OutPosition) const;

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	UStaticMeshComponent* GetSurfaceMesh() const
	{
		return SurfaceMesh.Get();
	}

	/** Copies the existing live footprint/paint parameters; no scan-owned CPU mask generation. */
	bool ConfigureContaminationScanMaterial(UMaterialInstanceDynamic* Material) const;
	UPrimitiveComponent* GetSolidContaminationScanMesh() const;

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

	/** Tool이 제거 질량을 Load와 물리 원장에 커밋할 때까지 완료 이벤트를 유예한다. */
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithDeferredCompletion(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec);

	/** Tool의 질량 커밋이 끝난 후 유예한 완료 전환을 확정한다. */
	void FinalizeLocalContaminationPath();

	/** Uniform residue over every actual blade contact. A positive ratio deposits without removing existing contamination. */
	FMD_ScrapeResult ApplyLocalScrapeStrokeWithContaminationStamp(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		float StampRatio,
		float& OutRemovedCleanRatioDelta,
		bool bDeferCompletionBroadcast = false);

	/** Exact movement spacing used by the evaluator for this surface and tool. */
	double GetScrapeSampleSpacingCm(
		const FMD_ScrapeToolSpec& ToolSpec) const;

	/** 스크래핑과 동일한 충돌 기준으로 앞면/뒷면만 판정한다. */
	bool TryGetScrapeSurfaceFaceFromHit(
		const FHitResult& Hit,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);

	bool TryGetScrapeSurfacePositionFromHit(
		const FHitResult& Hit,
		FVector2D& OutSurfacePositionCm,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);

	/** 조준 충돌점이 속한 단일 셀의 현재 오염 여부를 반환한다. */
	bool TryGetContaminationStateFromHit(
		const FHitResult& Hit,
		bool& OutContaminated);

	/** 이미 변환된 표면 좌표가 속한 단일 셀의 현재 오염 여부를 반환한다. */
	bool TryGetContaminationStateAtSurfacePosition(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		bool& OutContaminated) const;

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

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	float GetContaminationCleanRatio() const;

	/** MeshUV addresses are atlas coordinates; physical work always resolves the baked surface. */
	bool UsesMeshUVSurface() const;
	const UMD_ScrapeMeshUVData* GetContaminationMeshUVData() const;
	bool TryBuildMeshUVStroke(const FMD_ScrapeStroke& Stroke, FMD_ScrapeMeshUVStroke& OutStroke) const;
	bool TryGetScrapeSurfaceWorldNormalAtPosition(const FVector2D& Position, EMD_ScrapeSurfaceFace Face, FVector& OutNormal) const;
	bool IsContaminationCellActive(int32 CellIndex) const;
    UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
    bool GetContaminationCellState(int32 AtlasCellIndex, FMD_ScrapeSurfaceCell& OutCell) const;
	double GetContaminationCellCarrierWeight(int32 CellIndex) const;
	bool AreContaminationCarrierWeightsCurrent() const;
	FVector GetContaminationCarrierWeightScale() const;
	int32 GetContaminationActiveCellCount() const;
	int32 GetContaminationLayoutVersion() const;
	FGuid GetContaminationLayoutId() const;
	FIntPoint GetContaminationGridDimensions() const;
	bool CanRestoreContaminationLayout(int32 Version, const FGuid& LayoutId, const FIntPoint& Grid) const;
    /** Selects only an explicitly registered historical layout; cell indices are never remapped. */
    bool SelectCompatibleContaminationLayout(const FGuid& LayoutId, bool bInitialize = true);
	float GetContaminationCleanRatio(EMD_ScrapeSurfaceFace Face) const;

	UFUNCTION(BlueprintPure, Category = "MD|Scrape Surface")
	bool IsContaminationFullyRemoved() const;
	bool IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace Face) const;

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
	/**
	 * 시각용 피복률까지 함께 내보낸다.
	 *
	 * 이 값이 없으면 다음 Day에 어제 긁어 둔 자국의 경계가 격자 계단으로
	 * 되돌아간다. 잔여량만으로는 얇은 막과 부분 피복을 구분할 수 없다.
	 */
	void ExportContaminationDeltas(
		TArray<int32>& OutCellIndices,
		TArray<float>& OutInitialAmounts,
		TArray<float>& OutRemainingAmounts,
		TArray<float>& OutTransferredAmounts,
		TArray<float>& OutTransferredPatternDensities,
		TArray<float>& OutCoveredFractions) const;
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
	/** CoveredFractions가 비어 있으면 잔여량에서 역산해 구버전 겉모습을 쓴다. */
	void RestoreContaminationCells(
		const TArray<int32>& CellIndices,
		const TArray<float>& InitialAmounts,
		const TArray<float>& RemainingAmounts,
		const TArray<float>& TransferredAmounts,
		const TArray<float>& TransferredPatternDensities,
		const TArray<float>& CoveredFractions);

	UPROPERTY(BlueprintAssignable, Category = "MD|Scrape Surface")
	FMD_OnScrapeSurfaceFullyRemoved OnContaminationFullyRemoved;

protected:
    virtual void BeginDestroy() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(
		FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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

	/**
	 * 헤라 자국 경계를 자를 때 쓰는 마스크 값 구간의 폭.
	 *
	 * 마스크 텍셀은 0~1 불투명도 신호이고, 머티리얼은 0.5를 중심으로 이 폭만큼
	 * 부드럽게 자른다. 경계의 위치 정확도는 마스크 필드의 기울기가 정하므로 이
	 * 값과 무관하다. 즉 이 값은 경계선의 선명도만 바꾼다. 넓으면 부분 피복
	 * 대역 전체가 그라데이션으로 퍼져 선이 아니라 흐릿한 띠로 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Mask",
		meta = (ClampMin = "0.02", ClampMax = "1.0", UIMax = "0.5"))
	float ContaminationVisualEdgeSoftness;

	/**
	 * 셀에 얇게 남은 오염막의 두께를 불투명도로 바꿀 때 쓰는 감마.
	 *
	 * 1은 두께에 정비례한다. 그래야 한 번 긁을 때 줄어든 잔량이 그만큼 옅어져,
	 * 긁은 횟수만큼 일정하게 지워지는 것으로 보인다. 낮추면 얇은 막이 더 진하게
	 * 보이지만, 막이 남은 경계 셀에서 곡선이 포화되어 그 쪽 테두리가 셀 단위
	 * 톱니로 돌아간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Mask",
		meta = (ClampMin = "0.75", ClampMax = "1.0"))
	float ContaminationVisualFilmOpacityGamma;

	/**
	 * 경계에 얹는 머티리얼 노이즈의 세기. 자르는 구간 폭에 대한 비율이다.
	 *
	 * 0이면 완전한 직선이 된다. 절대값이 아니라 비율로 두는 이유는, 구간을 좁혀
	 * 경계를 날카롭게 만들 때 같은 절대 세기의 노이즈가 경계를 통째로 들었다
	 * 놨다 하며 톱니로 만들어 버리기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Mask",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationVisualEdgeNoiseAmount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContaminationPerCell;

	/** Legacy serialized generation mode, retained only to restore saves made before procedural became the default. */
    UPROPERTY()
    bool bUseProceduralContamination;

    /** Fill the entire surface; unchecked uses procedural contamination. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|Procedural",
        meta = (DisplayName = "Full Contamination"))
    bool bForceFullContamination = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Procedural",
		meta = (EditCondition = "!bForceFullContamination", ClampMin = "1.0", Units = "cm"))
	float ContaminationNoiseScaleCm;

    /** Automatic uses the server's common seed for this contract. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "MD|Scrape Surface|Procedural",
        meta = (DisplayName = "Use Fixed Seed", EditCondition = "!bForceFullContamination"))
    bool bUseFixedContaminationSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "MD|Scrape Surface|Procedural",
        meta = (DisplayName = "Fixed Seed", EditCondition = "!bForceFullContamination && bUseFixedContaminationSeed"))
    int32 ContaminationNoiseSeed;

    /** Serialized fixed coverage retained for saves without generation records. */
    UPROPERTY()
    float ContaminationCoverage;

    /** Coverage is sampled once per wall and noise seed; saved games retain the selected value. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|Procedural",
        meta = (DisplayName = "Coverage Min", EditCondition = "!bForceFullContamination", ClampMin = "0.0", ClampMax = "1.0"))
    float ContaminationCoverageMin = 0.55f;

    /** Equal endpoints give fixed coverage. Reversed endpoints are sorted when generating. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|Procedural",
        meta = (DisplayName = "Coverage Max", EditCondition = "!bForceFullContamination", ClampMin = "0.0", ClampMax = "1.0"))
    float ContaminationCoverageMax = 1.0f;

    /** Retain the exact serialized lower bound for old saves without generation records. */
    UPROPERTY()
    float ContaminationMinimumAmount;

    /** Lower bound inside new procedural patches; zero gives a continuous fade from clean. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|Procedural",
        meta = (DisplayName = "Minimum Contamination Amount", EditCondition = "!bForceFullContamination", ClampMin = "0.0", ClampMax = "1.0"))
    float ContaminationDensityMinimumAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D ContaminationSurfaceSizeOverrideCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Geometry")
	EMD_ScrapeSurfaceNormalAxis ContaminationSurfaceNormalAxis;

	/**
	 * 셀 좌표와 데칼 canonical UV가 함께 사용하는 표면 주소체계.
	 * 기존 벽은 Planar를 유지한다. 양쪽이 원호이면 CylindricalArc, D자형처럼
	 * 앞은 곡면이고 뒤는 평면이면 CurvedFrontPlanarBack을 사용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Geometry")
	FMD_ScrapeSurfaceMapping ContaminationSurfaceMapping;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface")
	FMD_ScrapeSettings ScrapeSettings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Decal")
	TObjectPtr<UMaterialInterface> ContaminationDecalMaterial;

	/**
	 * MeshUV 모드에서 메시 위에 직접 얹는 오염 머티리얼.
	 *
	 * 프로젝터가 없으므로 Deferred Decal Domain이 아니라 Surface Domain이며,
	 * 마스크 주소는 TexCoord[MeshUVChannel]에서 나온다. 투영 상자, 각도 페이드,
	 * 두께 게이트, 면 소유권 게이트를 모두 쓰지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category = "MD|Scrape Surface|Mesh UV")
	TObjectPtr<UMaterialInterface> ContaminationMeshUVMaterial;

	/** Bake in the data asset, then use its generated mesh on this component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Scrape Surface|MeshUV")
	TObjectPtr<UMD_ScrapeMeshUVData> ContaminationMeshUVData;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Scrape Surface|Solid")
    bool bSolidContaminationEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MD|Scrape Surface|Solid")
    FMD_ContaminationSolidSettings SolidContaminationSettings;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MD|Scrape Surface|Solid")
    TObjectPtr<UMaterialInterface> ContaminationSolidMaterial;

    UPROPERTY(EditDefaultsOnly, Category = "MD|Contamination|Solid")
    TSoftObjectPtr<UMaterialInterface> DefaultContaminationSolidMaterial;
    bool bLoadedDefaultSolidMaterial = false;
    void ResolveDefaultSolidMaterial();
    UPROPERTY(Transient)
    TObjectPtr<UMD_ContaminationSolidVisualComponent> ContaminationSolidVisual;
    bool bEditorSolidPreview = false;
    bool ConfigureSolidContaminationVisual();
    void ReleaseSolidContaminationVisual();
    void UpdateSolidContaminationVisual(const TArray<FMD_ScrapeCellChange>& CellChanges);

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
    FMD_ScrapeBoundaryState ContaminationBoundary;
    FMD_ScrapeBoundaryState PredictedContaminationBoundary;
    bool bHasPredictedContaminationBoundary = false;
    uint32 LastBoundaryVisualRevision=0;
    FGuid LastBoundaryEpoch;
    TSharedPtr<FMD_ScrapeBoundaryVisual> ContaminationBoundaryVisual;
    bool CommitContaminationBoundary(const FMD_ScrapeEvaluationScratch& Scratch, bool bPrediction);
    void UpdateContaminationBoundaryVisual(const TArray<FMD_ScrapeCellChange>& Changes);
    void ApplyContaminationBoundaryParameters(UMaterialInstanceDynamic& MID) const;
    void ReleaseContaminationBoundaryVisual();
    void ResetContaminationBoundary();
    void ResetPredictedContaminationBoundary();
    bool CheckContaminationBoundaryBudget();
	friend class UMD_ScrapeMeshUVSyncComponent;
	friend class UMD_ScrapeMeshUVSyncSubsystem;
	friend class AMD_CuttableWall;
	friend class UMD_ScrapeToolComponent;

	static constexpr int32 MaximumContaminationGridAxis = 256;
	static constexpr float MaximumContaminationCellSizeForPaintCm = 2.5f;
	static constexpr int32 MaximumTransferredPaintMaskAxis = 1024;
	static constexpr float MinimumContaminationFaceNormalDot = 0.5f;
	static constexpr float MinimumFullCoverageFaceNormalDot = 0.05f;
	static constexpr int32 MaximumContaminationDecalPatchesPerFace = 32;
	static constexpr int32 MaximumLocalScrapePredictionBatches = 24;
	static constexpr int32 MaximumLocalScrapePredictionStrokesPerBatch = 128;

	bool HasAuthority() const;
	void ResolveSurfaceMeshFromOwner();
	void EnsureVisualizationComponents();
	void DestroyOwnedVisualizationComponents();
	friend class UMD_ScrapeInitializationSubsystem;
	bool bDeferredInitializationPending = false;
    uint64 DeferredInitializationEpoch = 0;
	bool bCompletingDeferredInitialization = false;
	TOptional<FMD_ScrapeInitialState> PreparedInitialState;
    bool GetDeferredInitialMaskInputs(
        TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe>& OutLayout, float& OutFullSignal) const;
	bool TryDeferContaminationInitialization();
	void InitializeRuntimeState();
	void ShutdownRuntimeState();
	void ApplyContaminationEnabledState();

	UFUNCTION()
	void OnRep_ContaminationEnabled();

	// Dense planar state only; MeshUV reads shared initial state plus changed pages.
    // Use GetContaminationCellState for logical atlas access, including unmodified cells.
	// Never generate per-cell Details rows when a PIE/SIE wall instance is selected.
	UPROPERTY(BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TArray<FMD_ScrapeSurfaceCell> ContaminationCells;
    FMD_ScrapePagedState ContaminationPagedCells;
    TSharedPtr<const FMD_ScrapeCellLayout> ContaminationCellLayout;
    int32 GetContaminationStorageCellCount() const;
    FMD_ScrapeSurfaceCell ReadContaminationStorageCell(int32 StorageIndex) const;
    FMD_ScrapeSurfaceCell& EditContaminationStorageCell(int32 StorageIndex);
    void SetContaminationStorageCell(int32 StorageIndex, const FMD_ScrapeSurfaceCell& Cell);
    bool HasGeneratedInitialContamination(int32 StorageIndex) const;
    float GetGeneratedInitialContamination(int32 StorageIndex) const;
    bool HasLocalPredictedContaminationState() const;
    int32 GetContaminationAtlasCellCount() const;
    int32 GetContaminationStorageIndex(int32 AtlasCellIndex) const;
    FMD_ScrapeCellView GetContaminationCellView();
    FMD_ScrapeConstCellView GetContaminationCellView() const;

	struct FLocalScrapePredictionStroke
	{
		FMD_ScrapeStroke Stroke;
		FMD_ScrapeToolSpec ToolSpec;
	};

	struct FLocalScrapePrediction
	{
		TWeakObjectPtr<UMD_ScrapeToolComponent> Owner;
		uint64 FocusSessionId = 0;
		uint32 AimSequence = 0;
		double CreatedSeconds = 0.0;
		TArray<FLocalScrapePredictionStroke> Strokes;
		TMap<int32, FMD_ScrapeSurfaceCell> PredictedFinalCellStates;
	};

	TArray<FLocalScrapePrediction> LocalScrapePredictions;
	TArray<FMD_ScrapeSurfaceCell> LocalPredictedContaminationCells;
    FMD_ScrapePagedState LocalPredictedPagedCells;
    FMD_ScrapeMeshUVProgress LocalPredictedMeshUVProgress;
    bool bLocalScrapePredictionRebuildPending = false;
	TArray<FMD_ScrapeSurfaceCell> LocalScrapePredictionFaceScratch;
	FMD_ScrapeEvaluationScratch ScrapeEvaluationScratch;
	TArray<int32> PendingLocalScrapePredictionVisualCellIndices;
	TBitArray<> PendingLocalScrapePredictionReconcileCells;
	TArray<int32> PendingBatchedContaminationVisualCellIndices;
	int32 ContaminationVisualUpdateBatchDepth = 0;

	double PendingContaminationItemAmounts[2] = { 0.0, 0.0 };
    FVector2D PendingContaminationDropPositions[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
    int32 TransferPendingContaminationDrop(int32 FaceSlot, const FVector2D& SurfacePositionCm);
    void PreservePendingContaminationDrops();
	/**
	 * 한 face의 base/transferred layer는 내부에서 완전히 섞인 것으로 보고
	 * 모든 source lot을 제거 질량 비율대로 함께 소비한다.
	 */
	TArray<FMD_ScrapeRecoveryLot> BaseContaminationRecoveryLots[2];
	TArray<FMD_ScrapeRecoveryLot> TransferredContaminationRecoveryLots[2];
	/** 아직 물리 아이템 한 개의 임계량에 못 미친 raw와 정확히 대응하는 lot. */
	TArray<FMD_ScrapeRecoveryLot> PendingPhysicalRecoveryLots[2];
	/**
	 * legacy 물리 Actor에는 lot 필드가 아직 없으므로, 이미 스폰된 몫을
	 * MASS-013 carrier 연결 전까지 Surface가 잃지 않고 보관한다.
	 */
	TArray<FMD_ScrapeRecoveryLot> SpawnedPhysicalRecoveryLotsAwaitingCarrier[2];
	/**
	 * authoring/procedural 생성 직후 실제로 제거 가능한 base carrier 합계.
	 * 화면용 cache와 달리 밤샘 악화·legacy 복원·Refresh가 바꾸지 않으며,
	 * 제품 net 제거율과 face당 canonical Reward1.0의 공통 분모다.
	 */
	double CanonicalInitialContaminationCarrierAmounts[2] = { 0.0, 0.0 };
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

	bool InitializeContaminationCellCarrierWeights();
	// One immutable triangle-scale metric per baked profile and exact scale, shared across live walls.
	TSharedPtr<const FMD_ScrapeCarrierWeights> SharedContaminationCellCarrierWeights;
	FMD_ScrapeCarrierWeightView GetContaminationCellCarrierWeights() const;
	FVector ContaminationCarrierWeightScale = FVector::OneVector;
	FMD_ScrapeMaskPixels ContaminationMaskAtlasPixels;
	FIntPoint ContaminationMaskAtlasDimensions;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient,
		Category = "MD|Scrape Surface|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> TransferredPaintMaskTexture;

	FMD_ScrapeMaskPixels TransferredPaintMaskAtlasPixels;
	FIntPoint TransferredPaintMaskAtlasDimensions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrontContaminationDecalMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackContaminationDecalMID;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FrontContaminationDecalPatchMIDs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BackContaminationDecalPatchMIDs;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ContaminationMeshUVOverlayMID;

	bool bContaminationDecalsConfigured;

	/** FastArray보다 먼저 수신해 오래된 canonical cell bunch도 되감지 못하게 한다. */
	UPROPERTY(ReplicatedUsing = OnRep_ContaminationVisualRevision, Transient)
	uint32 ReplicatedContaminationVisualRevision = 0;

	UPROPERTY(Replicated, Transient)
	FMD_ReplicatedScrapeCellStateArray ReplicatedContaminationState;

	/** client mirror에 이미 반영된 FastArray 또는 visual delta revision. */
	uint32 LastAppliedContaminationVisualRevision = 0;

	/** one-shot delegate/feedback까지 소비한 visual delta revision. JIP에는 복제하지 않는다. */
	uint32 LastConsumedContaminationVisualRevision = 0;

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

	// 전체 벽 제거율은 면별 퍼센트 평균이 아니라 실제 초기/현재 오염 총량으로 계산한다.
	double CachedFrontInitialContaminationAmount;
	double CachedBackInitialContaminationAmount;
	double CachedFrontCurrentContaminationAmount;
	double CachedBackCurrentContaminationAmount;
	/** Canonical MeshUV totals and completion counts; refreshed by every bulk state replacement. */
	FMD_ScrapeMeshUVProgress CachedMeshUVProgress;

	bool bContaminationSurfaceInitialized;
	bool bContaminationCompletionBroadcast;
	bool bDeferredContaminationCompletionTransition = false;
	bool bOwnsFrontContaminationDecal = false;
	bool bOwnsBackContaminationDecal = false;
	/** MeshUV owns one CPU atlas; analytic surfaces retain their two face slots. */
	bool TryCalculateContaminationCellCounts(
		int32& OutCellsPerFace, int32& OutStateCellCount) const;
	FMD_ScrapeCellView GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face);
	FMD_ScrapeConstCellView GetContaminationCellsForFace(
		EMD_ScrapeSurfaceFace Face) const;
	bool ApplyLocalScrapePrediction(
		UMD_ScrapeToolComponent* PredictionOwner,
		uint64 FocusSessionId,
		uint32 AimSequence,
		double CreatedSeconds,
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		bool bDeferVisualUpdate = false);
	void ResolveLocalScrapePredictions(
		UMD_ScrapeToolComponent* PredictionOwner,
		uint64 FocusSessionId,
		uint32 AimSequence,
		const FMD_ScrapeAuthoritativeVisualDelta* Resolution = nullptr,
		bool bRebuildIfPredictionMissing = false);
	void ExpireLocalScrapePredictions(
		UMD_ScrapeToolComponent* PredictionOwner,
		double ExpireBeforeSeconds);
	void ClearLocalScrapePredictions(UMD_ScrapeToolComponent* PredictionOwner);
	bool HasLocalScrapePredictions(
		const UMD_ScrapeToolComponent* PredictionOwner) const;
	bool ApplyLocalScrapePredictionToShadow(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		TArray<int32>& OutChangedCellIndices);
	void MarkLocalScrapePredictionReconcileCell(int32 CellIndex);
	void FlushLocalScrapePredictionVisualUpdates();
	void RebuildLocalScrapePredictionShadow();
    void RequestLocalScrapePredictionRebuild();
    void FlushLocalScrapePredictionRebuild();
	SIZE_T GetLocalScrapePredictionAllocatedBytes() const;
	bool UpdateLocalScrapePredictionMemoryBudget();
	void ReleaseLocalScrapePredictionMemoryBudget();
	static constexpr uint64 MaximumLocalScrapePredictionBytes = 8 * 1024 * 1024;
	static constexpr uint64 MaximumWorldScrapePredictionBytes = 32 * 1024 * 1024;
	bool DoesLocalScrapePredictionMatchResolution(
		const FLocalScrapePrediction& Prediction,
		const FMD_ScrapeAuthoritativeVisualDelta& Resolution) const;
	static bool IsAimSequenceAtOrBefore(uint32 Candidate, uint32 Baseline);
	FMD_ScrapeResult ApplyLocalScrapeStrokeInternal(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		float StampRatio,
		float& OutRemovedCleanRatioDelta,
		bool bDeferCompletionBroadcast = false);

	void CommitContaminationMeshUVProgressCache(const FMD_ScrapeMeshUVProgress& Progress);
	void RefreshContaminationCleanRatioCache();
	void RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace Face);
	bool ResolveContaminationSurfaceGeometry();
	void InitializeContaminationSurface();
    UPROPERTY(Transient, DuplicateTransient, ReplicatedUsing = OnRep_ContaminationGeneration)
    FMD_ContaminationGeneration ContaminationGeneration;
    FMD_ContaminationGeneration AppliedContaminationGeneration;
    UFUNCTION() void OnRep_ContaminationGeneration();
    bool bRestoringContaminationGeneration = false;
    void PrepareContaminationGeneration();
    float ResolveContaminationCoverage(int32 ResolvedSeed) const;
    bool TryApplyContinuousInitialContamination();
    float CalculateContaminationVisualSignal(const FMD_ScrapeSurfaceCell& Cell) const;
    void ApplyContaminationGenerationParameters(UMaterialInstanceDynamic& MID) const;
    void UpdateContaminationGenerationOverride(const TArray<FMD_ScrapeCellChange>& Changes);
    bool TryBuildInitialContaminationMaskRecipe();
    void ReleaseContaminationGenerationVisual();
    UPROPERTY(Transient) TObjectPtr<UTexture2D> ProceduralOverrideTexture;
    FMD_ScrapeMaskPixels ProceduralOverridePixels;
    TWeakObjectPtr<UStaticMeshComponent> GenerationTransformSource;
    FDelegateHandle GenerationTransformHandle;

	bool TryApplyProceduralInitialContamination(
		int32 CellsPerFace,
		float BaseInitialContamination);
	bool TryBuildContaminationMaskPixels(
		FIntPoint& OutAtlasDimensions, TArray<uint8>& OutPixels) const;
	bool InitializeContaminationMask();
	void ReleaseContaminationMask();
	FMD_ScrapeConstCellView GetVisualContaminationCells() const;
	FMD_ScrapeSurfaceCell MakeContaminationMaskCell(int32 CellIndex) const;
	void UpdateContaminationMask(
		const TArray<FMD_ScrapeCellChange>& CellChanges);
	void BeginContaminationVisualUpdateBatch();
	void EndContaminationVisualUpdateBatch();
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
		UMaterialInstanceDynamic& DecalMID,
		EMD_ScrapeSurfaceFace Face) const;
	void ApplyContaminationAppearanceParameters(
		UMaterialInstanceDynamic& DecalMID,
		UMaterialInterface* ParentMaterial) const;
	void ApplyContaminationVisualMaskCurveParameters(
		UMaterialInstanceDynamic& DecalMID) const;
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

	/** MeshUV 모드에서 메시 자체에 오염 오버레이 머티리얼을 부착한다. */
	bool ConfigureContaminationMeshUVOverlay();

	/** 오버레이를 떼고 MID를 버린다. 모드 전환과 마스크 재생성에서 쓴다. */
	void ReleaseContaminationMeshUVOverlay();

	/** 오버레이 MID에 마스크 텍스처와 아틀라스 주소 규약을 넣는다. */
	void ApplyContaminationMeshUVOverlayParameters(
		UMaterialInstanceDynamic& OverlayMID) const;
	void HandleReplicatedContaminationState(
		TConstArrayView<int32> StateItemIndices,
		bool bResetToInitial);
	void ApplyReplicatedContaminationStates(
		TConstArrayView<FMD_ReplicatedScrapeCellState> StateItems,
		bool bResetToInitial);
	void ApplyAllReplicatedContaminationState();
	void PublishContaminationChanges(const TArray<FMD_ScrapeCellChange>& CellChanges, bool bBoundaryChanged=false);
	bool TryBuildReplicatedCellState(
		int32 CellIndex,
		FMD_ReplicatedScrapeCellState& OutState,
		bool& bOutMatchesGeneratedState) const;
	bool BuildAuthoritativeVisualDelta(
		const TArray<FMD_ScrapeCellChange>& CellChanges,
		float ScrapeLoadAfter,
		FMD_ScrapeAuthoritativeVisualDelta& OutDelta);
	bool ApplyAuthoritativeVisualDelta(
		const FMD_ScrapeAuthoritativeVisualDelta& Delta);
	bool ApplyAuthoritativePredictionResolution(
		const FMD_ScrapeAuthoritativeVisualDelta& Delta);
	bool ValidateAuthoritativeVisualDeltaCells(
		const FMD_ScrapeAuthoritativeVisualDelta& Delta) const;
	void AdvanceContaminationVisualRevision();
	static bool IsVisualRevisionNewer(uint32 Candidate, uint32 Baseline);

	UFUNCTION()
	void OnRep_ContaminationVisualRevision();
	/**
	 * Tool이 셀을 바꾸기 전에 후속 recovery commit이 실패할 수 없는지 검사한다.
	 * legacy pending raw의 missing lot은 Commit에서 reward 0으로 보충 가능하다.
	 */
	bool CanAcceptRecoveryTransaction(
		EMD_ScrapeSurfaceFace SurfaceFace) const;
	int32 DropContaminationItems(
		EMD_ScrapeSurfaceFace SurfaceFace,
		double RemovedAmount,
		const FVector2D& SurfacePositionCm,
		TConstArrayView<FMD_ScrapeRecoveryLot> RecoveryLots,
		bool* bOutAcceptedToLedger = nullptr);
	enum class EPhysicalCommitStatus : uint8
	{
		Rejected,
		AcceptedNoPhysicalShare,
		AcceptedPending
	};
	struct FPhysicalCommitResult
	{
		EPhysicalCommitStatus Status = EPhysicalCommitStatus::Rejected;
		int32 SpawnedCount = 0;

		bool IsAccepted() const
		{
			return Status != EPhysicalCommitStatus::Rejected;
		}
	};
	/**
	 * Tool이 Load 수용량을 확정한 뒤 받지 못한 순제거 질량만 한 번 커밋한다.
	 * 제품 호출자는 UMD_ScrapeToolComponent 하나로 제한해 같은 결과의 중복 커밋을 막는다.
	 */
	FPhysicalCommitResult CommitContaminationPhysicalDrop(
		const FMD_ScrapeResult& ScrapeResult,
		const FMD_ScrapeMassAllocationResult& Allocation,
		const FVector2D& SurfacePositionCm);
	bool TryCalculateContaminationItemSpawnTransform(
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector2D& SurfacePositionCm,
		FTransform& OutSpawnTransform) const;
	bool TryProjectWorldPointToContaminationSurface(
		const FVector& WorldPoint,
		EMD_ScrapeSurfaceFace SurfaceFace,
		FVector2D& OutSurfacePositionCm) const;

	/**
	 * MeshUV 모드에서 히트 삼각형의 UV를 셀 주소로 해석한다.
	 *
	 * 월드 위치를 받는 형태가 아니라 FHitResult를 받는 이유는, 이 모드의 주소가
	 * 위치가 아니라 맞힌 삼각형의 정점 속성에 들어 있기 때문이다.
	 */
	bool TryProjectHitToContaminationMeshUVSurface(
		const FHitResult& Hit,
		FVector2D& OutSurfacePositionCm) const;
	bool TryGetContaminationFaceFromHit(
		const FHitResult& Hit,
		EMD_ScrapeSurfaceFace& OutSurfaceFace) const;
};
