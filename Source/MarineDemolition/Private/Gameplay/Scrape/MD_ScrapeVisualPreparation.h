#pragma once
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Async/Future.h"
#include "MDScrapeInitialMaskRenderer.h"

struct FMD_ScrapePreparedVisualData
{
    TMap<int32, TArray<FMD_ScrapeMeshUVNeighbor>> Adjacency;
    TMultiMap<int32, int32> Dependents;
    TArray<int32> Offsets, ReverseOffsets, Reverse;
    TArray<FMD_ScrapeMeshUVVisualSample> Samples;
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> MaskLayout;
};

/** Worker owns copied immutable inputs, so editor edits or GC cannot invalidate its reads. */
struct FMD_ScrapeVisualPreparation
{
    uint64 Revision = 0;
    FGuid LayoutId;
    // Filled only on the game thread, then moved into the worker in one operation.
    TArray<FMD_ScrapeMeshUVCell> Cells;
    TArray<FMD_ScrapeMeshUVTriangle> Triangles;
    const FMD_ScrapeMeshUVCell* SourceCells = nullptr;
    const FMD_ScrapeMeshUVTriangle* SourceTriangles = nullptr;
    int32 CellCount = 0, TriangleCount = 0;
    FIntPoint Dimensions = FIntPoint::ZeroValue;
    double Tolerance = 0.0;
    FString Name;
    TFuture<FMD_ScrapePreparedVisualData> Result;
};
