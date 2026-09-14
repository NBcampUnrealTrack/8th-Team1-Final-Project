#include "Gameplay/Contamination/MD_SurfaceStainPersistence.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "World/Contamination/MD_SurfaceStain.h"

void FMD_SurfaceStainPersistence::Capture(UWorld* World, TArray<FMD_SurfaceStainRecord>& OutRecords)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Capture);
	OutRecords.Reset();
	if (!World || World->GetNetMode() == NM_Client) return;
	for (TActorIterator<AMD_SurfaceStain> It(World); It; ++It)
	{
		FMD_SurfaceStainRecord& Record = OutRecords.AddDefaulted_GetRef();
		Record.ActorPath = FSoftObjectPath(UWorld::RemovePIEPrefix(FSoftObjectPath(*It).ToString()));
		Record.RemainingSize = It->GetRemainingSize();
	}
	OutRecords.Sort([](const FMD_SurfaceStainRecord& A, const FMD_SurfaceStainRecord& B)
	{
		return A.ActorPath.ToString() < B.ActorPath.ToString();
	});
}

bool FMD_SurfaceStainPersistence::Validate(TConstArrayView<FMD_SurfaceStainRecord> Records)
{
	TSet<FSoftObjectPath> Paths;
	for (const FMD_SurfaceStainRecord& Record : Records)
	{
		if (!Record.ActorPath.IsValid() || !FMD_SurfaceStainSize::IsValidRemaining(Record.ResolveSize())
			|| Paths.Contains(Record.ActorPath)) return false;
		Paths.Add(Record.ActorPath);
	}
	return true;
}

bool FMD_SurfaceStainPersistence::Restore(UWorld* World, TConstArrayView<FMD_SurfaceStainRecord> Records)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceStain_Restore);
	if (!World || World->GetNetMode() == NM_Client || !Validate(Records)) return false;
	TMap<FSoftObjectPath, int32> Sizes;
	for (const FMD_SurfaceStainRecord& Record : Records) Sizes.Add(Record.ActorPath, Record.ResolveSize());
	for (TActorIterator<AMD_SurfaceStain> It(World); It; ++It)
	{
		const FSoftObjectPath Path(UWorld::RemovePIEPrefix(FSoftObjectPath(*It).ToString()));
		const int32* Size = Sizes.Find(Path);
		if (!It->RestoreSize(Size ? *Size : It->GetInitialSize())) return false;
	}
	return true;
}
