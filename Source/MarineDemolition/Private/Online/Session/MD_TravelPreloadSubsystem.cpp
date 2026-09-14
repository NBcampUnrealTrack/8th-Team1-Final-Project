#include "Online/Session/MD_TravelPreloadSubsystem.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    TAutoConsoleVariable<int32> CVarTravelPreloadEnabled(TEXT("MD.Travel.Preload"), 1,
        TEXT("Prepare the configured lobby destination after the lobby loading screen closes."));
    TAutoConsoleVariable<float> CVarTravelSnapshotBudgetMs(TEXT("MD.Travel.PreloadSnapshotBudgetMs"), .5f,
        TEXT("Soft per-frame input-copy budget in the lobby; zero copies each profile synchronously."));
}

void UMD_TravelPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}

void UMD_TravelPreloadSubsystem::QueueDestination(const TSoftObjectPtr<UWorld>& Destination)
{
    if (!CVarTravelPreloadEnabled.GetValueOnGameThread() || Destination.IsNull()) return;
    const FSoftObjectPath Path = Destination.ToSoftObjectPath();
    if (Path != WarmupMap || WarmupAssets.IsEmpty() || Path == DestinationPath) return;
    Release();
    DestinationPath = Path;
    SourceWorld = GetWorld();
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(
        this, &ThisClass::TickPreparation));
}

bool UMD_TravelPreloadSubsystem::TickPreparation(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || World != SourceWorld.Get()) { Ticker.Reset(); return false; }
    if (Handle)
    {
        if (!Handle->HasLoadCompleted()) return true;
        // One owned snapshot/worker at a time bounds lobby preparation memory.
        while (VisualPreparationIndex < WarmupAssets.Num())
        {
            auto* Data = Cast<UMD_ScrapeMeshUVData>(WarmupAssets[VisualPreparationIndex].ResolveObject());
            if (!Data) { ++VisualPreparationIndex; continue; }
            Data->BeginVisualPreparation(FMath::Clamp(CVarTravelSnapshotBudgetMs.GetValueOnGameThread(), 0.f, 2.f));
            if (!Data->IsVisualPreparationReady()) return true;
            Data->GetVisualDependents();
            ++VisualPreparationIndex;
            if (VisualPreparationIndex < WarmupAssets.Num()) return true;
        }
        Ticker.Reset();
        return false;
    }
    const auto* Loading = GetGameInstance()->GetSubsystem<UMD_LoadingScreenSubsystem>();
    if (Loading && Loading->IsLoadingScreenActive()) { ReadySince = 0.0; return true; }
    const double Now = FPlatformTime::Seconds();
    if (ReadySince == 0.0) ReadySince = Now;
    // Let the lobby outro finish before scheduling any nonessential preparation.
    if (Now - ReadySince < 1.5) return true;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Travel_PreloadRequest);
    Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(WarmupAssets,
        FStreamableDelegate(), FStreamableManager::DefaultAsyncLoadPriority, false, false,
        TEXT("LobbyDestinationData"));
    UE_LOG(LogTemp, Log, TEXT("Travel preload requested %d data assets for %s"), WarmupAssets.Num(), *DestinationPath.ToString());
    return true;
}

void UMD_TravelPreloadSubsystem::HandlePostLoadMap(UWorld* World)
{
    if (!World || World->GetGameInstance() != GetGameInstance() || World == SourceWorld.Get()) return;
    // The transient seamless transition world must not release the destination's retained dependencies.
    if (!World->GetOutermost()->GetName().StartsWith(TEXT("/Temp/"))) Release();
}

void UMD_TravelPreloadSubsystem::Release()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    Ticker.Reset();
    if (WarmupAssets.IsValidIndex(VisualPreparationIndex))
        if (auto* Data = Cast<UMD_ScrapeMeshUVData>(WarmupAssets[VisualPreparationIndex].ResolveObject()))
            Data->CancelVisualPreparationSnapshot();
    if (Handle) { Handle->CancelHandle(); Handle.Reset(); }
    DestinationPath.Reset();
    SourceWorld.Reset();
    ReadySince = 0.0;
    VisualPreparationIndex = 0;
}

void UMD_TravelPreloadSubsystem::Deinitialize()
{
    Release();
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    Super::Deinitialize();
}
