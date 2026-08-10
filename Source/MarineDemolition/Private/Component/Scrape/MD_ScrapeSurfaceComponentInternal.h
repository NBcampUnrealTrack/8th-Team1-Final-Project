#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("MD Scrape"), STATGROUP_MD_Scrape, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Stroke Total"), STAT_MD_ScrapeStrokeTotal,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Evaluate"), STAT_MD_ScrapeEvaluate,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Clean Ratio"), STAT_MD_ScrapeCleanRatio,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("G8 Mask Update"), STAT_MD_ScrapeMaskUpdate,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ISM Overlay Update"), STAT_MD_ScrapeOverlayUpdate,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Replication Publish"), STAT_MD_ScrapePublish,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Fully Removed Check"),
	STAT_MD_ScrapeFullyRemovedCheck, STATGROUP_MD_Scrape,
	MARINEDEMOLITION_API);

/** Names shared by the component implementation files. */
namespace MD_ScrapeSurfaceComponentInternal
{
	extern const FName ContaminationMaskTextureParameterName;
	extern const FName ContaminationMaskVScaleParameterName;
	extern const FName ContaminationMaskVOffsetParameterName;
	extern const FName ContaminationMaskUFlipParameterName;
	extern const FName ContaminationMaskHalfTexelUParameterName;
	extern const FName ContaminationMaskHalfTexelVParameterName;
}
