#pragma once
#include "CoreMinimal.h"
#include "Curve/GeneralPolygon2.h"

/** Geometry captured during the evaluator's already validated contact traversal.
 * Atlas coordinates are measured in logical cells; no G8 signal is used as geometry. */
struct FMD_ScrapeBoundaryFootprint
{
    int32 TriangleIndex = INDEX_NONE;
    TArray<FVector2D> Points;
    // Only tiles containing validated physical contacts (plus a one-cell halo).
    // Empty permits geometric fixtures to derive a bounded candidate rectangle.
    TArray<int32> Tiles;
};

struct FMD_ScrapeBoundaryPatch
{
    TArray<UE::Geometry::FGeneralPolygon2d> Polygons;
    bool bFallback = false;
    int32 VertexCount() const;
    uint64 GetAllocatedBytes() const;
};

/** Current union per private chart and 8x8 logical tile, with a one-cell halo.
 * COW patches allow prediction rollback without retaining the stroke history.
 * Saturation explicitly disables the affected patch instead of retaining stale geometry. */
class MARINEDEMOLITION_API FMD_ScrapeBoundaryState
{
public:
    static constexpr int32 Version = 1;
    static constexpr int32 TileCells = 8;
    static constexpr int32 MaximumPatchVertices = 512;
    static constexpr int32 MaximumPatches = 1024;
    static constexpr int32 MaximumPayloadBytes = 2 * 1024 * 1024;
    static constexpr uint64 MaximumWorldBytes = 32ull * 1024 * 1024;

    FGuid LayoutId;
    FIntPoint Dimensions = FIntPoint::ZeroValue;
    TMap<uint64, TSharedPtr<const FMD_ScrapeBoundaryPatch>> Patches;
    TSet<int32> DirtyTiles;
    uint64 Generation = 0; // Transient change counter; saves contain only current geometry.
    bool bDisabled = false;

    void Reset(FGuid Layout, FIntPoint Size);
    void Apply(TConstArrayView<FMD_ScrapeBoundaryFootprint> Footprints);
    const FMD_ScrapeBoundaryPatch* Find(int32 Tile, int32 Triangle) const;
    uint64 GetAllocatedBytes() const;
    bool Serialize(TArray<uint8>& Out) const;
    bool Deserialize(TConstArrayView<uint8> Bytes, FGuid ExpectedLayout, FIntPoint ExpectedSize);
    static uint64 Key(int32 Tile, int32 Triangle)
    { return (uint64(uint32(Tile)) << 32) | uint32(Triangle); }
    int32 TileColumns() const { return FMath::DivideAndRoundUp(Dimensions.X, TileCells); }
};
