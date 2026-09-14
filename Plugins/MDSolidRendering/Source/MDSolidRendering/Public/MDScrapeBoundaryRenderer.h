#pragma once
#include "CoreMinimal.h"
class UTextureRenderTarget2D;

/** One 8x8 logical tile plus one-cell halo, sampled 8 times per logical cell.
 * Every cell references only its own private chart's current contour. */
struct FMDScrapeBoundaryTileUpload
{
    FIntPoint LogicalOrigin = FIntPoint::ZeroValue;
    FIntPoint PhysicalOrigin = FIntPoint::ZeroValue;
    TArray<FVector4f> Edges;
    // 100 cells: edge offset/count, target opacity, original opacity.
    TArray<FVector4f> Cells;
    // Three affine half-plane equations per cell; excludes atlas triangle slivers
    // from the coverage mean without sampling unrelated islands.
    TArray<FVector4f> Domains;
};

class MDSOLIDRENDERING_API FMDScrapeBoundaryRenderer
{
public:
    static constexpr int32 AtlasSize = 2048;
    static constexpr int32 TileSize = 80;
    static constexpr int32 TileColumns = AtlasSize / TileSize;
    static constexpr int32 TileCapacity = TileColumns * TileColumns;
    static constexpr uint64 MaximumPendingBytes = 8ull*1024*1024;
    /** Enqueued data is bounded through GPU completion, including RHI staging.
     * False means no submission occurred; callers keep their dirty tile queue. */
    static bool Submit(UTextureRenderTarget2D* Atlas, TArray<FMDScrapeBoundaryTileUpload>&& Tiles);
};
