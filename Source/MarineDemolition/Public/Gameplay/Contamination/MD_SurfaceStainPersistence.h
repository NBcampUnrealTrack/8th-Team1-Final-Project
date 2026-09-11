#pragma once

#include "CoreMinimal.h"
#include "Types/Persistence/MD_SurfaceStainRecord.h"

class UWorld;

/** Single save adapter shared by file saves and Day snapshots. */
struct MARINEDEMOLITION_API FMD_SurfaceStainPersistence
{
	static void Capture(UWorld* World, TArray<FMD_SurfaceStainRecord>& OutRecords);
	static bool Validate(TConstArrayView<FMD_SurfaceStainRecord> Records);
	static bool Restore(UWorld* World, TConstArrayView<FMD_SurfaceStainRecord> Records);
};
