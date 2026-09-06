#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_ScrapeMeshUVData.generated.h"

class UStaticMesh;

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
	UPROPERTY(VisibleAnywhere) FVector PositionLocal = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FVector Barycentric = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) double AreaCm2 = 0.0;
	/** Conservative radius of the clipped polygon around PositionLocal, in local cm. */
	UPROPERTY(VisibleAnywhere) double SupportRadiusCm = 0.0;
	/** Active texel to copy for this pixel's visual gutter, or INDEX_NONE for unused pixels. */
	UPROPERTY(VisibleAnywhere) int32 VisualSourceCellIndex = INDEX_NONE;

	bool IsActive() const { return TriangleIndex != INDEX_NONE && AreaCm2 > 0.0; }
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
	bool Validate(FString* OutError = nullptr) const;
	bool IsValidForMesh(const UStaticMesh* Mesh) const;
	bool IsActiveCell(int32 CellIndex) const;
	/** Game-thread-only lookup shared by every wall using this immutable baked profile. */
	const TMultiMap<int32, int32>& GetVisualDependents() const;

private:
	void InvalidateVisualDependentsCache() const;
	// Derived runtime data: never serialized, cooked, or expanded by the Details panel.
	mutable TMultiMap<int32, int32> CachedVisualDependents;
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
