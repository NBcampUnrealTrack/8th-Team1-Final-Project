#pragma once

#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

/** Builds authoring-only connectivity without welding or changing triangle geometry. */
class FMD_ScrapeMeshUVAdjacency final
{
public:
#if WITH_EDITOR
    static bool Build(TArray<FMD_ScrapeMeshUVTriangle>& Triangles, double ToleranceCm,
        int32& OutConnectedPairs, FString& OutError);
#endif
};
