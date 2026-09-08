#include "Gameplay/Scrape/MD_ContaminationSolidPrewarmSubsystem.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    TAutoConsoleVariable<int32> CVarSolidPrewarmEnabled(
        TEXT("MD.Solid.Prewarm.Enabled"), 0,
        TEXT("Prepare existing world solid geometry before Actor BeginPlay; zero retains synchronous cache misses."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarSolidPrewarmWorkers(
        TEXT("MD.Solid.Prewarm.MaxWorkers"), 4,
        TEXT("Maximum simultaneous pure geometry builds during world startup, clamped to 1..4."),
        ECVF_Default);
}

bool UMD_ContaminationSolidPrewarmSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMD_ContaminationSolidPrewarmSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (bPreparedForWorld || InWorld.GetNetMode() == NM_DedicatedServer ||
        CVarSolidPrewarmEnabled.GetValueOnGameThread() == 0) return;
    bPreparedForWorld = true;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Prewarm);
    PendingPrewarm = MakeUnique<FMD_ContaminationSolidPrewarmBatch>();
    // This virtual callback precedes Actor BeginPlay. The similarly named world
    // multicast delegate runs after actors and would warm this cache too late.
    for (TActorIterator<AActor> Actor(&InWorld); Actor; ++Actor)
    {
        TInlineComponentArray<UMD_ScrapeSurfaceComponent*> Surfaces(*Actor);
        for (UMD_ScrapeSurfaceComponent* Surface : Surfaces)
        {
            const UMD_ScrapeMeshUVData* Data = nullptr;
            FVector Scale;
            FMD_ContaminationSolidSettings Settings;
            if (Surface && Surface->TryGetSolidPrewarmInputs(Data, Scale, Settings))
            {
                FString Error;
                PendingPrewarm->Add(*Data, Scale, Settings, Error);
            }
        }
    }
    PendingPrewarm->Execute(CVarSolidPrewarmWorkers.GetValueOnGameThread());
    LastStats = PendingPrewarm->GetStats();
    UE_LOG(LogTemp, Log, TEXT("Solid prewarm: %s"), *GetPrewarmSummary());
    // Keep cache hits and new results alive until all Actor BeginPlay calls can
    // consume them. A late actor/changed scale/settings keeps FindOrBuild fallback.
    ReleaseTimer = InWorld.GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateWeakLambda(this, [this]() { ReleasePrewarm(); }));
}

void UMD_ContaminationSolidPrewarmSubsystem::ReleasePrewarm()
{
    check(IsInGameThread());
    if (PendingPrewarm)
    {
        PendingPrewarm->Release();
        LastStats = PendingPrewarm->GetStats();
        PendingPrewarm.Reset();
    }
    ReleaseTimer.Invalidate();
}

void UMD_ContaminationSolidPrewarmSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
    InWorld.GetTimerManager().ClearTimer(ReleaseTimer);
    ReleasePrewarm();
    bPreparedForWorld = false;
    Super::OnWorldEndPlay(InWorld);
}

void UMD_ContaminationSolidPrewarmSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ReleaseTimer);
    ReleasePrewarm();
    Super::Deinitialize();
}

FString UMD_ContaminationSolidPrewarmSubsystem::GetPrewarmSummary() const
{
    return FString::Printf(TEXT("candidates=%d queued=%d cache_hits=%d duplicates=%d rejected=%d built=%d failed=%d invalidated=%d peak_workers=%d retained_geometry_bytes=%lld execute_seconds=%.6f"),
        LastStats.Candidates, LastStats.Queued, LastStats.CacheHits, LastStats.Duplicates,
        LastStats.Rejected, LastStats.Built, LastStats.Failed, LastStats.Invalidated,
        LastStats.PeakWorkers, LastStats.RetainedGeometryBytes, LastStats.ExecuteSeconds);
}
