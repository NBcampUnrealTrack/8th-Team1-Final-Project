#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_ScrapeMeshUVData.generated.h"

class UStaticMesh;
struct FMD_ScrapeCellLayout;
class FMD_ScrapeCarrierWeights;

/** Immutable display weight; gameplay ownership remains in the baked Cells. */
struct FMD_ScrapeMeshUVVisualSample
{
    int32 CellIndex = INDEX_NONE;
    float Weight = 0.0f;
};

/** Borrowed immutable seam addressing for one synchronous game-thread rebuild.
 *  Never retain across profile mutation/cache invalidation; contains no UObject access. */
struct FMD_ScrapeMeshUVVisualSeamView
{
    TConstArrayView<int32> Offsets;
    TConstArrayView<FMD_ScrapeMeshUVVisualSample> Samples;
    TConstArrayView<FMD_ScrapeMeshUVVisualSample> GetSamples(const int32 PixelIndex) const
    {
        if (PixelIndex < 0 || PixelIndex + 1 >= Offsets.Num()) return {};
        const int32 Begin = Offsets[PixelIndex];
        const int32 Count = Offsets[PixelIndex + 1] - Begin;
        return Count > 0 ? Samples.Slice(Begin, Count) : TConstArrayView<FMD_ScrapeMeshUVVisualSample>();
    }
};

/** Immutable shared arrays can outlive profile invalidation while a worker reads them. */
struct FMD_ScrapeMeshUVVisualSeams
{
    TArray<int32> Offsets;
    TArray<FMD_ScrapeMeshUVVisualSample> Samples;
    FMD_ScrapeMeshUVVisualSeamView GetView() const { return {Offsets, Samples}; }
};

/** A real shared subsegment; a T-junction can have several neighbors along one triangle edge. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVNeighbor
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere) int32 TriangleIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere) FVector SharedEdgeA = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere) FVector SharedEdgeB = FVector::ZeroVector;
};

/** One original triangle or source fragment. Canonical UV never replaces source material UVs. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVTriangle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere) FVector A = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector B = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector C = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector NormalLocal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector2D UVA = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector2D UVB = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector2D UVC = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere) int32 SourceTriangleId = INDEX_NONE;
	/** Fragment corners expressed on the immutable original source triangle. */
	UPROPERTY(VisibleAnywhere) FVector SourceBarycentricA = FVector(1, 0, 0);
	UPROPERTY(VisibleAnywhere) FVector SourceBarycentricB = FVector(0, 1, 0);
	UPROPERTY(VisibleAnywhere) FVector SourceBarycentricC = FVector(0, 0, 1);
	/** EMD_ScrapeExposureReason; cap exclusion is recorded separately by bCleanable. */
	UPROPERTY(VisibleAnywhere) uint8 ExposureExclusionReason = 0;
	UPROPERTY(VisibleAnywhere) int32 MaterialIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere) bool bCleanable = false;
	/** Across AB, BC and CA. Ambiguous/non-manifold edges deliberately have no neighbor. */
	UPROPERTY(VisibleAnywhere) FIntVector AdjacentTriangles = FIntVector(INDEX_NONE);
    /** Baked partial-edge portals; AdjacentTriangles is only a legacy full-edge fallback. */
    UPROPERTY(VisibleAnywhere) TArray<FMD_ScrapeMeshUVNeighbor> Neighbors;
	UPROPERTY(VisibleAnywhere) TArray<int32> CellIndices;
};

/** A texel clipped to its single owning source triangle. Inactive/gutter cells have no owner. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere) int32 TriangleIndex = INDEX_NONE;
	// Adjacent indices avoid eight padding bytes per cell; persistent field order stays explicit.
	/** Active texel to copy for this pixel's visual gutter, or INDEX_NONE for unused pixels. */
	UPROPERTY(VisibleAnywhere) int32 VisualSourceCellIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere) FVector PositionLocal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector Barycentric = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) double AreaCm2 = 0.0;
	/** Conservative radius of the clipped polygon around PositionLocal, in local cm. */
	UPROPERTY(VisibleAnywhere) double SupportRadiusCm = 0.0;

	bool IsActive() const { return TriangleIndex != INDEX_NONE && AreaCm2 > 0.0; }
	/** Compact persistent payload; legacy assets, transactions and duplication keep tagged fields. */
	bool Serialize(FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FMD_ScrapeMeshUVCell> : TStructOpsTypeTraitsBase2<FMD_ScrapeMeshUVCell>
{
    enum { WithSerializer = true };
};

/** Exact original-mesh contact; TriangleIndex is a profile index, not FHitResult::FaceIndex. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVSample
{
	int32 TriangleIndex = INDEX_NONE;
	FVector PositionLocal = FVector::ZeroVector;
	FVector NormalLocal = FVector::ZeroVector;
	FVector Barycentric = FVector::ZeroVector;
	FVector2D CanonicalUV = FVector2D::ZeroVector;
	double DistanceCm = 0.0;
};

/**
 * Editor-baked, cooked surface geometry and atlas ownership. Baking only modifies this data
 * asset and its generated mesh subobject; the source mesh and its UV/materials are untouched.
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_ScrapeMeshUVData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Scrape|Bake") TObjectPtr<UStaticMesh> SourceMesh;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.1", Units="cm"))
	double TargetCellSizeCm = 2.5;
    /** Zero keeps legacy normalization. Set when rebaking density to preserve physical carrier units. */
    UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.0", Units="cm"))
    double CarrierReferenceCellSizeCm = 0.0;
    double GetCarrierReferenceCellSizeCm() const
    { return CarrierReferenceCellSizeCm == 0.0 ? TargetCellSizeCm : CarrierReferenceCellSizeCm; }
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="32", ClampMax="1024"))
	int32 MaximumAtlasAxis = 1024;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="1", ClampMax="4"))
	int32 GutterCells = 1;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.0", ClampMax="0.01", Units="cm"))
	double AdjacencyWeldToleranceCm = 0.001;
	/** Only the whole mesh's extreme top/bottom caps are excluded; intermediate shelves stay. */
	UPROPERTY(EditAnywhere, Category="Scrape|Bake") bool bExcludeTopAndBottomCaps = true;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.99", ClampMax="1.0"))
	double CapNormalThreshold = 0.999;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.0", Units="cm"))
	double CapHeightToleranceCm = 0.2;
	/** Split intersecting parts and limit cleaning to exposed source fragments. */
	UPROPERTY(EditAnywhere, Category="Scrape|Bake") bool bOnlyExposedSurfaces = true;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.000001", Units="cm"))
	double IntersectionToleranceCm = 0.0001;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.000001", Units="cm"))
	double ExposureClassificationOffsetCm = 0.001;
	/** Opposing surfaces inside this joining clearance stay rendered but acquire no dirt. */
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.0", Units="cm"))
	double ContactGapToleranceCm = 0.1;
	UPROPERTY(EditAnywhere, Category="Scrape|Bake", meta=(ClampMin="0.000001", Units="cm"))
	double CoplanarToleranceCm = 0.0001;


	/** Source appearance and split geometry, a spare canonical UV channel, Nanite disabled. */
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") TObjectPtr<UStaticMesh> BakedRenderMesh;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") FGuid LayoutId;
    /** Exact historical layout, loaded only when a save/server requests its cell indices. */
    UPROPERTY(EditAnywhere, Category="Scrape|Compatibility") FGuid PreviousLayoutId;
    UPROPERTY(EditAnywhere, Category="Scrape|Compatibility") TSoftObjectPtr<UMD_ScrapeMeshUVData> PreviousLayoutData;
    UMD_ScrapeMeshUVData* ResolveCompatibleLayout(const FGuid& RequestedLayout);
	/** Cheap rebuild invalidation; the immutable baked mesh remains the geometry authority. */
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") FGuid SourceMeshLightingGuid;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") int32 ConnectedEdgeCount = 0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") int32 ValidCellCount = 0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") double CleanableAreaCm2 = 0.0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") int32 CanonicalUVChannel = 1;
	// Keep the payload serialized, but out of Details: expanding hundreds of thousands of
	// nested property rows after Bake exhausts editor memory. Inspect the summaries instead.
	UPROPERTY() TArray<FMD_ScrapeMeshUVTriangle> Triangles;
	UPROPERTY() TArray<FMD_ScrapeMeshUVCell> Cells;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") FString LastBakeReport;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") int32 OriginalTriangleCount = 0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") double InteriorExcludedAreaCm2 = 0.0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") double ContactExcludedAreaCm2 = 0.0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") double DuplicateExcludedAreaCm2 = 0.0;
	UPROPERTY(VisibleAnywhere, Category="Scrape|Baked") FString ExposureDiagnostics;


	UFUNCTION(CallInEditor, Category="Scrape|Bake") void BakeSourceMesh();
    /** Always performs the complete payload validation, including for loaded/cached assets. */
    bool Validate(FString* OutError = nullptr) const;
    /** Reuses successful validation only for clean, immutable loaded assets on the game thread. */
    bool ValidateForRuntime(FString* OutError = nullptr) const;
    /**
     * Call before directly editing Cells/Triangles or derived metadata, even when sizes/LayoutId
     * stay unchanged. Reinitialize live consumers after edits; change LayoutId for a new layout.
     * Bake/editor/load hooks do this automatically. Raw writes are not detected by a cheap cache key.
     */
    void InvalidateRuntimeCaches() const;
    uint64 GetRuntimeCacheRevision() const { return RuntimeCacheRevision; }
    static const FGuid CellSerializationVersionGuid;
    static constexpr int32 CellSerializationVersion = 1;
    virtual void Serialize(FArchive& Ar) override;
    virtual void PostLoad() override;
    virtual bool IsPostLoadThreadSafe() const override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditUndo() override;
#endif
	bool IsValidForMesh(const UStaticMesh* Mesh) const;
	bool IsActiveCell(int32 CellIndex) const;
    /** Baked portals plus unambiguous exact edges and shared-source fragment portals. Never serialized. */
    TConstArrayView<FMD_ScrapeMeshUVNeighbor> GetTriangleNeighbors(int32 TriangleIndex) const;
	/** Game-thread-only lookup shared by every wall using this immutable baked profile. */
	const TMultiMap<int32, int32>& GetVisualDependents() const;
    /** Empty means the original single-source texel. Never serialized. */
    TConstArrayView<FMD_ScrapeMeshUVVisualSample> GetVisualSeamSamples(int32 PixelIndex) const;
    /** Validate/build shared caches once, then borrow their arrays for this call only. */
    FMD_ScrapeMeshUVVisualSeamView GetVisualSeamView() const;
    TSharedPtr<const FMD_ScrapeMeshUVVisualSeams, ESPMode::ThreadSafe> GetSharedVisualSeams() const;
    TConstArrayView<int32> GetVisualSeamDependents(int32 SourceCellIndex) const;
    TSharedPtr<const FMD_ScrapeCellLayout> GetSharedCellLayout(float BudgetMs = 0.f, bool* OutPending = nullptr) const;
    /** Positive budget advances an owned input snapshot; zero finishes it synchronously. */
    void BeginVisualPreparation(float SnapshotBudgetMs = 0.f) const;
    /** Release abandoned partial inputs; an already-dispatched worker keeps owning its snapshot. */
    void CancelVisualPreparationSnapshot() const;
    bool IsVisualPreparationReady() const;
    TSharedPtr<const class FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> GetSharedInitialMaskLayout() const;
    /** Game thread only. Exact scale determines area; rotation/translation do not. */
    TSharedPtr<const FMD_ScrapeCarrierWeights> GetSharedCellCarrierWeights(
        const FTransform& Transform, float BudgetMs = 0.f, bool* OutPending = nullptr) const;
    /** Drop unpublished metrics after cancellation; published shared results remain valid. */
    void CancelInitializationPreparation() const;

private:
    mutable uint64 RuntimeCacheRevision = 0;
    mutable TSharedPtr<struct FMD_ScrapeVisualPreparation> VisualPreparation;
    mutable TSharedPtr<const class FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> PreparedMaskLayout;
    mutable TWeakPtr<const class FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> SharedMaskLayout;
    void BuildExactAdjacencyCache() const;
    mutable TMap<int32, TArray<FMD_ScrapeMeshUVNeighbor>> ExactAdjacencyOverrides;
    mutable const FMD_ScrapeMeshUVTriangle* ExactAdjacencyTriangleData = nullptr;
    mutable int32 ExactAdjacencyTriangleCount = INDEX_NONE;
    mutable FGuid ExactAdjacencyLayoutId;
    struct FRuntimeValidationKey
    {
        const UStaticMesh* Source = nullptr;
        const UStaticMesh* RenderMesh = nullptr;
        const FMD_ScrapeMeshUVCell* CellData = nullptr;
        const FMD_ScrapeMeshUVTriangle* TriangleData = nullptr;
        int32 CellCount = 0, TriangleCount = 0, ActiveCount = 0, UVChannel = 0;
        FIntPoint Dimensions = FIntPoint::ZeroValue;
        FGuid Layout, SourceGuid;
        double Area = 0.0, WeldTolerance = 0.0, CellSize = 0.0, CarrierSize = 0.0;
        bool Matches(const UMD_ScrapeMeshUVData& Data) const;
        void Capture(const UMD_ScrapeMeshUVData& Data);
    };
    mutable FRuntimeValidationKey RuntimeValidationKey;
    mutable bool bRuntimeValidationCached = false;
    mutable TSharedPtr<const FMD_ScrapeCellLayout> CachedCellLayout;
    mutable TSharedPtr<FMD_ScrapeCellLayout> PreparingCellLayout;
    mutable int32 PreparingLayoutCellCount = INDEX_NONE;
    mutable FGuid CellLayoutId;
    mutable FIntPoint CellLayoutAtlasDimensions = FIntPoint::ZeroValue;
    struct FCarrierWeightCacheEntry
    {
        FVector Scale = FVector::ZeroVector;
        TWeakPtr<const FMD_ScrapeCarrierWeights> Weights;
        TSharedPtr<FMD_ScrapeCarrierWeights> Preparing;
    };
    mutable TArray<FCarrierWeightCacheEntry> CarrierWeightCache;
    mutable FGuid CarrierWeightsLayoutId;
    mutable FIntPoint CarrierWeightsAtlasDimensions = FIntPoint::ZeroValue;
    mutable int32 CarrierWeightsCellCount = INDEX_NONE;
    mutable int32 CarrierWeightsTriangleCount = INDEX_NONE;
    mutable double CarrierWeightsTargetCellSize = 0.0;
	void InvalidateVisualDependentsCache() const;
	// Derived runtime data: never serialized, cooked, or expanded by the Details panel.
	mutable TMultiMap<int32, int32> CachedVisualDependents;
    mutable TSharedPtr<const FMD_ScrapeMeshUVVisualSeams, ESPMode::ThreadSafe> CachedVisualSeams;
    mutable TArray<int32> CachedVisualSeamDependentOffsets;
    mutable TArray<int32> CachedVisualSeamDependents;
	mutable FGuid VisualDependentsLayoutId;
	mutable FIntPoint VisualDependentsAtlasDimensions = FIntPoint::ZeroValue;
	mutable int32 VisualDependentsCellCount = INDEX_NONE;
};

/** Stateless geometry queries shared by hit detection, cell evaluation and visual debugging. */
class MARINEDEMOLITION_API FMD_ScrapeMeshUVGeometry final
{
public:
	/** Finds the first original surface, then rejects if it is uncleanable; never rays through caps. */
	static bool RaycastSource(const UMD_ScrapeMeshUVData& Data,
		const FVector& StartLocal, const FVector& EndLocal, FMD_ScrapeMeshUVSample& OutSample,
		bool bCullBackFaces = true);
	/** Nearest source point within a bounded tolerance. Normal gate uses outward source normals. */
	static bool FindNearestSource(const UMD_ScrapeMeshUVData& Data,
		const FVector& PointLocal, const FVector& NormalLocal, double MaximumDistanceCm,
		FMD_ScrapeMeshUVSample& OutSample, double MinimumNormalDot = 0.5);
	/** Canonical UV -> original triangle point; unused atlas and gutters fail instead of approximating. */
	static bool SampleCanonicalUV(const UMD_ScrapeMeshUVData& Data,
		const FVector2D& UV, FMD_ScrapeMeshUVSample& OutSample);
    /** UV-clipped polygon expressed directly in its triangle barycentric coordinates. */
    static bool GetCellPolygonBarycentric(const UMD_ScrapeMeshUVData& Data,
        int32 CellIndex, TArray<FVector>& OutBarycentricPolygon);
	static bool GetCellPolygonLocal(const UMD_ScrapeMeshUVData& Data,
		int32 CellIndex, TArray<FVector>& OutPolygon);
    /** Positive-area closest point without the engine's absolute normal-size cutoff. */
    static bool ClosestPointOnSourceTriangle(const FMD_ScrapeMeshUVTriangle& Triangle,
        const FVector& PointLocal, FVector& OutClosestLocal);
    /** Applies the metric after transformation, including nonuniform world scale. */
    static bool ClosestPointOnSourceTriangle(const FMD_ScrapeMeshUVTriangle& Triangle,
        const FTransform& MeshToWorld, const FVector& PointWorld, FVector& OutClosestWorld);
	static bool TryCalculateBarycentric(const FMD_ScrapeMeshUVTriangle& Triangle,
		const FVector& PointLocal, FVector& OutBarycentric);
};
