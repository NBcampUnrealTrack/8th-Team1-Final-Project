#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ContaminationGeneration.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"

class UMD_ScrapeMeshUVData;

/** Worker-owned output. UObject mutation and GPU resource creation remain on their owning threads. */
struct FMD_ScrapeInitialState
{
    TArray<float> Amounts;
    FMD_ScrapeMeshUVProgress Progress;
    double CanonicalCarrier = 0.0;
};

namespace MD_ScrapeInitialState
{
    TArray<float> Generate(const UMD_ScrapeMeshUVData& Data, const FMD_ScrapeCellLayout& Layout,
        const FMD_ContaminationGeneration& Generation);
    bool CalculateTotals(FMD_ScrapeInitialState& State, const FMD_ScrapeCellLayout& Layout,
        FMD_ScrapeCarrierWeightView Weights, float CleanTolerance);
}
