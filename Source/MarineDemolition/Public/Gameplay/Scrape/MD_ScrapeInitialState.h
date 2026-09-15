#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ContaminationGeneration.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"

class UMD_ScrapeMeshUVData;
class FMDScrapeInitialMaskLayout;
struct FMD_ScrapeMeshUVVisualSeamView;

/** Worker-owned output. UObject mutation and GPU resource creation remain on their owning threads. */
struct FMD_ScrapeInitialState
{
    TArray<float> Amounts;
    FMD_ScrapeMeshUVProgress Progress;
    double CanonicalCarrier = 0.0;
    // Owned recipe construction resumes between bounded blocks. No surface
    // or render resource is published until every block has been validated.
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> MaskLayout;
    TArray<uint32> MaskBits;
    TArray<uint8> MaskSeams;
    int32 NextMaskCell = 0, NextMaskSeam = 0;
    bool bMaskRecipeComplete = false;
};

namespace MD_ScrapeInitialState
{
    /** Pure compact recipe construction; GPU atlas expansion remains unchanged. */
    bool PrepareMaskRecipe(FMD_ScrapeInitialState& State, const FMD_ScrapeCellLayout& Cells,
        FMD_ScrapeMeshUVVisualSeamView Seams, float FullSignal, bool bParallel, double Deadline);
    TArray<float> Generate(const UMD_ScrapeMeshUVData& Data, const FMD_ScrapeCellLayout& Layout,
        const FMD_ContaminationGeneration& Generation);
    bool CalculateTotals(FMD_ScrapeInitialState& State, const FMD_ScrapeCellLayout& Layout,
        FMD_ScrapeCarrierWeightView Weights, float CleanTolerance);
}
