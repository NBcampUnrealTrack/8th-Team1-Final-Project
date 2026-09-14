#include "Gameplay/Scrape/MD_ScrapeInitialState.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
