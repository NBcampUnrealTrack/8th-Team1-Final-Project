#pragma once

#include "Gameplay/Scrape/MD_ScrapeExposedSurface.h"

/** Splits authoring masks in source-triangle planes without removing geometry. */
class FMD_ScrapeSurfaceExposureClip final
{
public:
    static bool SplitAndClassify(
        TConstArrayView<FMD_ScrapeSurfaceFragment> SourceTriangles,
        TConstArrayView<FMD_ScrapeSurfaceFragment> InputFragments,
        const FMD_ScrapeExposedSurfaceSettings& Settings,
        TArray<FMD_ScrapeSurfaceFragment>& OutFragments, FString& OutError);
};
