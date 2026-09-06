#pragma once
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

// Derived display-only resampling; baked ownership and gameplay cells remain immutable.
namespace MD_ScrapeMeshUVSeam
{
    void Build(const UMD_ScrapeMeshUVData& Data, TArray<int32>& OutOffsets,
        TArray<FMD_ScrapeMeshUVVisualSample>& OutSamples, TArray<int32>& OutDependentOffsets,
        TArray<int32>& OutDependents);
}
