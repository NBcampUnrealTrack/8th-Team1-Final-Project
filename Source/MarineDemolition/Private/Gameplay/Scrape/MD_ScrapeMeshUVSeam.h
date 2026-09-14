#pragma once
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

struct FMD_ScrapeVisualSourceView
{
    TConstArrayView<FMD_ScrapeMeshUVCell> Cells;
    TConstArrayView<FMD_ScrapeMeshUVTriangle> Triangles;
    FIntPoint AtlasDimensions;
    TConstArrayView<TConstArrayView<FMD_ScrapeMeshUVNeighbor>> Neighbors;
    FString Name;
    bool IsActiveCell(int32 I) const { return Cells.IsValidIndex(I) && Cells[I].IsActive(); }
    TConstArrayView<FMD_ScrapeMeshUVNeighbor> GetTriangleNeighbors(int32 I) const { return Neighbors[I]; }
    const FString& GetName() const { return Name; }
};

// Derived display-only resampling; baked ownership and gameplay cells remain immutable.
namespace MD_ScrapeMeshUVSeam
{
    void BuildAdjacency(TConstArrayView<FMD_ScrapeMeshUVTriangle> Triangles, double AdjacencyWeldToleranceCm,
        TMap<int32, TArray<FMD_ScrapeMeshUVNeighbor>>& Out);
    void Build(const FMD_ScrapeVisualSourceView& Data, TArray<int32>& OutOffsets,
        TArray<FMD_ScrapeMeshUVVisualSample>& OutSamples, TArray<int32>& OutDependentOffsets,
        TArray<int32>& OutDependents);
    void Build(const UMD_ScrapeMeshUVData& Data, TArray<int32>& OutOffsets,
        TArray<FMD_ScrapeMeshUVVisualSample>& OutSamples, TArray<int32>& OutDependentOffsets,
        TArray<int32>& OutDependents);
}
