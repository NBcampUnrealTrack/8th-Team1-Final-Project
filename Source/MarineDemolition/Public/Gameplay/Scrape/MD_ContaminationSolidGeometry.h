#pragma once

#include "CoreMinimal.h"

class UMD_ScrapeMeshUVData;
struct FMD_ContaminationSolidSettings;

struct FMD_ContaminationSolidVertex
{
    FVector3d Position = FVector3d::ZeroVector;
    FVector3f OffsetPerCm = FVector3f::ZeroVector;
    FVector3f Normal = FVector3f::ZeroVector;
    float ThicknessCm = 0.0f;
};

struct FMD_ContaminationSolidTriangle
{
    FIntVector Vertices = FIntVector::ZeroValue;
    FVector2f UV[3];
    int32 SourceTriangle = INDEX_NONE;
    int32 ChunkIndex = INDEX_NONE;
    int32 LocalTriangleIndex = INDEX_NONE;
};

struct FMD_ContaminationSolidSample
{
    int32 CellIndex = INDEX_NONE;
    float Weight = 0.0f;
};

struct FMD_ContaminationSolidChunk
{
    TArray<int32> Triangles;
    TArray<int32> TopVertices;
    /** Two entries per boundary edge; positions coincide but skirt topology stays separate. */
    TArray<int32> BottomVertices;
    /** Oriented top boundary; each edge has two skirt triangles down into the wall. */
    TArray<FIntPoint> BoundaryEdges;
    /** Contains all allowed heights, including clean inset and two accumulated layers. */
    FBox LocalBounds = FBox(ForceInit);
};

/** Immutable topology. Cache and shared ownership stay on the game thread; workers build unique results. */
struct MARINEDEMOLITION_API FMD_ContaminationSolidGeometry
{
    static constexpr float WallInsetCm = 0.03f;

    static TSharedPtr<const FMD_ContaminationSolidGeometry> FindOrBuild(
        const UMD_ScrapeMeshUVData& Data, const FVector& WallScale,
        const FMD_ContaminationSolidSettings& Settings, FString& OutError);

    TArray<FMD_ContaminationSolidVertex> Vertices;
    TArray<FMD_ContaminationSolidTriangle> Triangles;
    TArray<FMD_ContaminationSolidChunk> Chunks;
    TArray<int32> SampleOffsets;
    TArray<FMD_ContaminationSolidSample> Samples;
    TArray<int32> CellVertexOffsets;
    TArray<int32> CellVertices;
    TArray<int32> VertexChunkOffsets;
    TArray<int32> VertexChunks;
    /** Parallel to VertexChunks: direct CPU-mesh position index inside that chunk. */
    TArray<int32> VertexChunkLocalVertices;
    TArray<int32> VertexBoundaryOffsets;
    /** (chunk index, local boundary-edge index), two entries per boundary edge. */
    TArray<FIntPoint> VertexBoundaries;
    TArray<int32> VertexTriangleOffsets;
    TArray<int32> VertexTriangles;
    /** Numerically zero-area source fragments retain carrier mappings with zero influence. */
    TBitArray<> CarrierOnlySources;
    float SampleSpacingCm = 0.0f;
    float MaximumThicknessCm = 0.0f;
    int32 BoundaryEdgeCount = 0;
    int32 DegenerateSourceCount = 0;
    int32 IsolatedSourceCount = 0;
    int32 RecoveredSourceCount = 0;
    int32 AliasedCarrierCount = 0;
    int32 UnalignedCarrierCount = 0;
    int32 GlobalCarrierQueryCount = 0;
    int64 GetAllocatedBytes() const;
};

/** Counts cover the whole prewarm operation; retained bytes are shared topology, not process memory. */
struct FMD_ContaminationSolidPrewarmStats
{
    int32 Candidates = 0;
    int32 Queued = 0;
    int32 CacheHits = 0;
    int32 Duplicates = 0;
    int32 Rejected = 0;
    int32 Built = 0;
    int32 Failed = 0;
    int32 Invalidated = 0;
    int32 PeakWorkers = 0;
    int64 RetainedGeometryBytes = 0;
    double ExecuteSeconds = 0.0;
};

/**
 * Game-thread-owned preparation and cache publication. Execute joins at most four
 * worker bodies before returning; workers receive only immutable array views and
 * settings copies and produce uniquely owned geometry. No shared-pointer refcount
 * or UObject access crosses onto those workers.
 *
 * Baked input is immutable while this batch exists. Direct payload edits must
 * invalidate runtime caches and change LayoutId for a new layout, as required by
 * UMD_ScrapeMeshUVData; raw nested-array edits cannot be detected by pointer keys.
 */
class MARINEDEMOLITION_API FMD_ContaminationSolidPrewarmBatch final
{
public:
    FMD_ContaminationSolidPrewarmBatch();
    ~FMD_ContaminationSolidPrewarmBatch();
    FMD_ContaminationSolidPrewarmBatch(const FMD_ContaminationSolidPrewarmBatch&) = delete;
    FMD_ContaminationSolidPrewarmBatch& operator=(const FMD_ContaminationSolidPrewarmBatch&) = delete;

    bool Add(const UMD_ScrapeMeshUVData& Data, const FVector& WallScale,
        const FMD_ContaminationSolidSettings& Settings, FString& OutError);
    void Execute(int32 MaximumWorkers = 4);
    void Release();
    const FMD_ContaminationSolidPrewarmStats& GetStats() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
