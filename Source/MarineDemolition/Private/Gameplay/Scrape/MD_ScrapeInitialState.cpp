#include "Gameplay/Scrape/MD_ScrapeInitialState.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"
#include "MDScrapeInitialMaskRenderer.h"

// Generated network baselines must use the same arithmetic in Game and Editor.
// UE 5.8 defaults to fast math for Game and precise math for Editor on MSVC.
#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#endif

TArray<float> MD_ScrapeInitialState::Generate(const UMD_ScrapeMeshUVData& Data,
    const FMD_ScrapeCellLayout& Layout, const FMD_ContaminationGeneration& G)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_GenerateInitialAmounts);
    TArray<float> Amounts;
    Amounts.SetNumZeroed(Layout.ActiveToAtlas.Num());
    const double MaximumAxisScale = G.SampleTransform.GetMaximumAxisScale();
    constexpr int32 CellsPerBatch = 256;
    ParallelFor(FMath::DivideAndRoundUp(Amounts.Num(), CellsPerBatch), [&](int32 Batch)
    {
        TArray<FVector> Polygon;
        const int32 End = FMath::Min((Batch + 1) * CellsPerBatch, Amounts.Num());
        for (int32 I = Batch * CellsPerBatch; I < End; ++I)
        {
            const int32 Address = Layout.ActiveToAtlas[I];
            const auto& Cell = Data.Cells[Address];
            const FVector WorldCenter = G.SampleTransform.TransformPosition(Cell.PositionLocal);
            const float N = FMD_ContaminationNoise::FractalNoise3D(WorldCenter, G.NoiseScaleCm, G.Seed);
            const double Bound = 4.5 * Cell.SupportRadiusCm * MaximumAxisScale / G.NoiseScaleCm;
            if (G.Coverage <= UE_SMALL_NUMBER || N + Bound <= 1.f - G.Coverage) continue;
            if (N - Bound > 1.f - G.Coverage) { Amounts[I] = G.AmountFromNoise(N); continue; }
            if (!FMD_ScrapeMeshUVGeometry::GetCellPolygonLocal(Data, Address, Polygon)) continue;
            double AreaSum = 0, Sum = 0;
            for (int32 J = 1; J + 1 < Polygon.Num(); ++J)
            {
                const double Area = FVector::CrossProduct(Polygon[J] - Polygon[0], Polygon[J+1] - Polygon[0]).Length();
                AreaSum += Area;
                Sum += Area * G.IntegrateTriangle(Polygon[0], Polygon[J], Polygon[J+1]);
            }
            Amounts[I] = AreaSum > 0 ? float(Sum / AreaSum) : 0;
        }
    });
    return Amounts;
}

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif

bool MD_ScrapeInitialState::CalculateTotals(FMD_ScrapeInitialState& State,
    const FMD_ScrapeCellLayout& Layout, FMD_ScrapeCarrierWeightView Weights, float Tolerance)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitialTotals);
    State.Progress = {};
    State.CanonicalCarrier = 0.0;
    if (State.Amounts.Num() != Layout.ActiveToAtlas.Num() || Weights.Num() != Layout.AtlasToActive.Num()
        || !FMath::IsFinite(Tolerance) || Tolerance < 0.f) return false;
    auto& P = State.Progress;
    P.CleanTolerance = Tolerance;
    P.UncleanCellCount = P.UncleanCellCountAtZero = 0;
    // Ascending active addresses preserve the old atlas reduction order; inactive terms are exact +0.
    for (int32 I = 0; I < State.Amounts.Num(); ++I)
    {
        const float Amount = State.Amounts[I];
        const double Weight = Weights[Layout.ActiveToAtlas[I]];
        if (!FMath::IsFinite(Amount) || Amount < 0.f || Amount > 1.f
            || !FMath::IsFinite(Weight) || Weight < 0.0) return false;
        P.UncleanCellCount += Amount > UE_SMALL_NUMBER && Amount > Tolerance;
        P.UncleanCellCountAtZero += Amount > UE_SMALL_NUMBER;
        FMD_ScrapeSurfaceCell Cell;
        Cell.InitialContamination = Cell.RemainingContamination = Amount;
        Cell.Normalize();
        P.InitialCarrierAmount += Weight * static_cast<double>(Cell.InitialContamination);
        P.BaseCarrierAmount += Weight * static_cast<double>(Cell.RemainingContamination);
        if (Amount > Tolerance) State.CanonicalCarrier += static_cast<double>(Amount) * Weight;
    }
    P.CurrentCarrierAmount = P.BaseCarrierAmount;
    P.bIsValid = FMath::IsFinite(P.InitialCarrierAmount) && FMath::IsFinite(P.CurrentCarrierAmount)
        && FMath::IsFinite(State.CanonicalCarrier);
    return P.bIsValid;
}

bool MD_ScrapeInitialState::PrepareMaskRecipe(FMD_ScrapeInitialState& State,
    const FMD_ScrapeCellLayout& Cells, FMD_ScrapeMeshUVVisualSeamView Seams,
    float FullSignal, bool bParallel, double Deadline)
{
    if (State.bMaskRecipeComplete) return true;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitialMaskRecipeSlice);
    if (!State.MaskLayout || State.Amounts.Num() != Cells.ActiveToAtlas.Num())
    { State.MaskLayout.Reset(); State.bMaskRecipeComplete = true; return true; }
    if (State.MaskBits.IsEmpty() && State.NextMaskCell == 0)
    {
        State.MaskBits.SetNumZeroed(FMath::DivideAndRoundUp(State.Amounts.Num(), 32));
        State.MaskSeams.SetNumUninitialized(State.MaskLayout->SeamPixels.Num());
    }
    while (State.NextMaskCell < State.Amounts.Num())
    {
        const int32 End = FMath::Min(State.NextMaskCell + 4096, State.Amounts.Num());
        for (; State.NextMaskCell < End; ++State.NextMaskCell)
            if (State.Amounts[State.NextMaskCell] > UE_SMALL_NUMBER)
                State.MaskBits[State.NextMaskCell >> 5] |= 1u << (State.NextMaskCell & 31);
        if (FPlatformTime::Seconds() >= Deadline) return false;
    }
    while (State.NextMaskSeam < State.MaskSeams.Num())
    {
        const int32 Begin = State.NextMaskSeam;
        constexpr int32 SeamsPerJob = 2048;
        const int32 End = FMath::Min(Begin + 16 * SeamsPerJob, State.MaskSeams.Num());
        TAtomic<bool> Valid(true);
        // Join only a bounded group; retain parallel seam evaluation instead of
        // serializing the full recipe or waiting on one job for the whole atlas.
        ParallelFor(FMath::DivideAndRoundUp(End - Begin, SeamsPerJob), [&](int32 Job)
        {
            const int32 JobEnd = FMath::Min(Begin + (Job + 1) * SeamsPerJob, End);
            for (int32 Seam = Begin + Job * SeamsPerJob; Seam < JobEnd; ++Seam)
            {
                double Sum = 0.0;
                for (const auto& Sample : Seams.GetSamples(State.MaskLayout->SeamPixels[Seam]))
                {
                    if (!Cells.AtlasToActive.IsValidIndex(Sample.CellIndex)) continue;
                    const int32 Active = Cells.AtlasToActive[Sample.CellIndex];
                    const float Signal = Active != INDEX_NONE && ((State.MaskBits[Active >> 5] >> (Active & 31)) & 1u) ? FullSignal : 0.f;
                    Sum += Sample.Weight * Signal;
                }
                const float Signal = static_cast<float>(FMath::Clamp(Sum, 0.0, 1.0));
                if (!FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(Signal, 1.f, State.MaskSeams[Seam])) Valid.Store(false);
            }
        }, bParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
        if (!Valid.Load())
        { State.MaskLayout.Reset(); State.MaskBits.Empty(); State.MaskSeams.Empty(); State.bMaskRecipeComplete = true; return true; }
        State.NextMaskSeam = End;
        if (FPlatformTime::Seconds() >= Deadline) return false;
    }
    State.bMaskRecipeComplete = true;
    return true;
}
