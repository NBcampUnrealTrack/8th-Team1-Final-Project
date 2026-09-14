#include "Gameplay/Scrape/MD_ScrapeVisualPreparation.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSeam.h"
#include "Async/Async.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> BuildMaskLayout(
        TConstArrayView<FMD_ScrapeMeshUVCell> Cells, const FMD_ScrapeMeshUVVisualSeamView& Seams)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_BuildInitialMaskLayout);
        TArray<int32> AtlasToActive;
        AtlasToActive.Init(INDEX_NONE, Cells.Num());
        int32 ActiveCount = 0;
        for (int32 I = 0; I < Cells.Num(); ++I)
            if (Cells[I].IsActive()) AtlasToActive[I] = ActiveCount++;
        TArray<uint32> Sources;
        TArray<int32> SeamPixels;
        Sources.SetNumUninitialized(Cells.Num());
        for (int32 Pixel = 0; Pixel < Cells.Num(); ++Pixel)
        {
            if (!Seams.GetSamples(Pixel).IsEmpty())
            {
                Sources[Pixel] = 0x80000000u | uint32(SeamPixels.Num());
                SeamPixels.Add(Pixel);
            }
            else
            {
                const int32 Source = Cells[Pixel].VisualSourceCellIndex;
                const int32 Active = AtlasToActive.IsValidIndex(Source) ? AtlasToActive[Source] : INDEX_NONE;
                Sources[Pixel] = Active == INDEX_NONE ? MAX_uint32 : uint32(Active);
            }
        }
        return MakeShared<FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe>(MoveTemp(Sources), MoveTemp(SeamPixels));
    }
}

void UMD_ScrapeMeshUVData::BeginVisualPreparation(float SnapshotBudgetMs) const
{
    check(IsInGameThread());
    if (VisualPreparation && VisualPreparation->Result.IsValid()) return;
    if (!VisualPreparation && VisualDependentsLayoutId == LayoutId
        && VisualDependentsAtlasDimensions == AtlasDimensions && VisualDependentsCellCount == Cells.Num()) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_SnapshotVisualPreparation);
    const bool bTimeSliced = FMath::IsFinite(SnapshotBudgetMs) && SnapshotBudgetMs > 0.f;
    const double Deadline = FPlatformTime::Seconds() + FMath::Min(SnapshotBudgetMs, 2.f) / 1000.0;
    // Invalidation is required for same-size edits. Also reject replaced/resized input
    // between slices before combining it with any previously copied prefix.
    if (VisualPreparation && (VisualPreparation->Revision != RuntimeCacheRevision
        || VisualPreparation->LayoutId != LayoutId || VisualPreparation->Dimensions != AtlasDimensions
        || VisualPreparation->SourceCells != Cells.GetData() || VisualPreparation->CellCount != Cells.Num()
        || VisualPreparation->SourceTriangles != Triangles.GetData() || VisualPreparation->TriangleCount != Triangles.Num()
        || VisualPreparation->Tolerance != AdjacencyWeldToleranceCm)) VisualPreparation.Reset();
    if (!VisualPreparation)
    {
        auto Pending = MakeShared<FMD_ScrapeVisualPreparation>();
        Pending->Revision = RuntimeCacheRevision;
        Pending->LayoutId = LayoutId;
        Pending->SourceCells = Cells.GetData(); Pending->CellCount = Cells.Num();
        Pending->SourceTriangles = Triangles.GetData(); Pending->TriangleCount = Triangles.Num();
        Pending->Dimensions = AtlasDimensions; Pending->Tolerance = AdjacencyWeldToleranceCm;
        Pending->Name = GetName();
        // Reserve once; Append touches/constructs only the next bounded block.
        Pending->Cells.Reserve(Cells.Num());
        Pending->Triangles.Reserve(Triangles.Num());
        VisualPreparation = MoveTemp(Pending);
    }
    auto& Pending = *VisualPreparation;
    while (Pending.Cells.Num() < Cells.Num())
    {
        const int32 Begin = Pending.Cells.Num();
        Pending.Cells.Append(Cells.GetData() + Begin, FMath::Min(4096, Cells.Num() - Begin));
        if (bTimeSliced && FPlatformTime::Seconds() >= Deadline) return;
    }
    while (Pending.Triangles.Num() < Triangles.Num())
    {
        const int32 Begin = Pending.Triangles.Num();
        Pending.Triangles.Append(Triangles.GetData() + Begin, FMath::Min(64, Triangles.Num() - Begin));
        if (bTimeSliced && FPlatformTime::Seconds() >= Deadline) return;
    }
    // The worker owns every input. Partial snapshots never enter the thread pool.
    Pending.Result = Async(EAsyncExecution::ThreadPool,
        [CellsCopy = MoveTemp(Pending.Cells), TrianglesCopy = MoveTemp(Pending.Triangles),
         Dimensions = Pending.Dimensions, Tolerance = Pending.Tolerance, Name = MoveTemp(Pending.Name)]()
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_PrepareSharedVisualData);
        FMD_ScrapePreparedVisualData Out;
        MD_ScrapeMeshUVSeam::BuildAdjacency(TrianglesCopy, Tolerance, Out.Adjacency);
        TArray<TConstArrayView<FMD_ScrapeMeshUVNeighbor>> Neighbors;
        Neighbors.Reserve(TrianglesCopy.Num());
        for (int32 I = 0; I < TrianglesCopy.Num(); ++I)
            Neighbors.Add(Out.Adjacency.Contains(I) ? MakeArrayView(Out.Adjacency[I])
                : MakeArrayView(TrianglesCopy[I].Neighbors));
        for (int32 I = 0; I < CellsCopy.Num(); ++I)
            if (CellsCopy[I].VisualSourceCellIndex != INDEX_NONE && CellsCopy[I].VisualSourceCellIndex != I)
                Out.Dependents.Add(CellsCopy[I].VisualSourceCellIndex, I);
        MD_ScrapeMeshUVSeam::Build(FMD_ScrapeVisualSourceView{CellsCopy, TrianglesCopy, Dimensions, Neighbors, Name},
            Out.Offsets, Out.Samples, Out.ReverseOffsets, Out.Reverse);
        Out.MaskLayout = BuildMaskLayout(CellsCopy, {Out.Offsets, Out.Samples});
        return Out;
    });
}

void UMD_ScrapeMeshUVData::CancelVisualPreparationSnapshot() const
{
    check(IsInGameThread());
    if (VisualPreparation && !VisualPreparation->Result.IsValid()) VisualPreparation.Reset();
}

bool UMD_ScrapeMeshUVData::IsVisualPreparationReady() const
{
    check(IsInGameThread());
    return !VisualPreparation || (VisualPreparation->Result.IsValid() && VisualPreparation->Result.IsReady());
}

TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> UMD_ScrapeMeshUVData::GetSharedInitialMaskLayout() const
{
    check(IsInGameThread());
    const auto Seams = GetVisualSeamView();
    if (PreparedMaskLayout)
    {
        auto Layout = MoveTemp(PreparedMaskLayout);
        SharedMaskLayout = Layout;
        return Layout;
    }
    if (auto Layout = SharedMaskLayout.Pin()) return Layout;
    auto Layout = BuildMaskLayout(Cells, Seams);
    SharedMaskLayout = Layout;
    return Layout;
}
