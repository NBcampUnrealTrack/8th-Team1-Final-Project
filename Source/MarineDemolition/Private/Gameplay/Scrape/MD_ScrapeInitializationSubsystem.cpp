#include "Gameplay/Scrape/MD_ScrapeInitializationSubsystem.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/MD_GameStateBase.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    TAutoConsoleVariable<int32> CVarScrapeInitializationEnabled(TEXT("MD.Scrape.DeferredInitialization"), 1,
        TEXT("Prepare new procedural MeshUV surfaces with bounded background jobs."));
    TAutoConsoleVariable<int32> CVarScrapeLoadingJobs(TEXT("MD.Scrape.LoadingInitializationJobs"), 8,
        TEXT("Bounded worker slots during covered loading; four during gameplay."));
    TAutoConsoleVariable<float> CVarScrapeLoadingBudgetMs(TEXT("MD.Scrape.LoadingInitializationBudgetMs"), 8.f,
        TEXT("Soft commit budget while the loading screen is covering the world."));
    TAutoConsoleVariable<float> CVarScrapeInitializationBudgetMs(TEXT("MD.Scrape.InitializationBudgetMs"), 2.f,
        TEXT("Soft game-thread budget for committing prepared surfaces; one commit is atomic."));
}

bool UMD_ScrapeInitializationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld();
}

bool UMD_ScrapeInitializationSubsystem::Enqueue(UMD_ScrapeSurfaceComponent& Surface)
{
    if (bStopping || !CVarScrapeInitializationEnabled.GetValueOnGameThread()) return false;
    ++Surface.DeferredInitializationEpoch;
    Queue.AddUnique(&Surface);
    return true;
}

TStatId UMD_ScrapeInitializationSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMD_ScrapeInitializationSubsystem, STATGROUP_Tickables);
}

void UMD_ScrapeInitializationSubsystem::Tick(float DeltaTime)
{
    if (bStopping || !IsPending()) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitializationQueue);
    const auto* GI = GetWorld()->GetGameInstance();
    const auto* Loading = GI ? GI->GetSubsystem<UMD_LoadingScreenSubsystem>() : nullptr;
    const auto* GS = GetWorld()->GetGameState<AMD_GameStateBase>();
    const bool Covered = (Loading && Loading->IsLoadingScreenActive())
        || (GetWorld()->GetNetMode() == NM_DedicatedServer && GS && !GS->IsWorldInitializationReady());
    const float Budget = Covered ? CVarScrapeLoadingBudgetMs.GetValueOnGameThread() : CVarScrapeInitializationBudgetMs.GetValueOnGameThread();
    const double Deadline = FPlatformTime::Seconds() + FMath::Clamp(Budget, .1f, 12.f) / 1000.0;
    for (int32 I = Jobs.Num() - 1; I >= 0; --I)
    {
        if (!Jobs[I]->Result.IsReady()) continue;
        // A background result must never make the game thread wait for shared preparation.
        if (!Jobs[I]->Data->IsVisualPreparationReady()) continue;
        auto Job = MoveTemp(Jobs[I]);
        Jobs.RemoveAtSwap(I);
        FMD_ScrapeInitialState Result = Job->Result.Consume();
        if (auto* Surface = Job->Surface.Get(); Surface && Surface->bDeferredInitializationPending
            && Surface->DeferredInitializationEpoch == Job->Epoch)
        {
            const bool Current = Surface->ContaminationMeshUVData == Job->Data.Get()
                && Job->Data->GetRuntimeCacheRevision() == Job->Revision
                && Surface->ContaminationGeneration == Job->Generation
                && Surface->SurfaceMesh == Job->Mesh.Get() && Job->Mesh.IsValid()
                && Job->Mesh->GetStaticMesh() == Job->StaticMesh.Get()
                && Job->Mesh->GetComponentScale() == Job->Scale
                && Surface->ScrapeSettings.CleanTolerance == Job->Tolerance;
            Surface->bDeferredInitializationPending = false;
            if (Current && Result.Progress.bIsValid) Surface->PreparedInitialState = MoveTemp(Result);
            TGuardValue<bool> Completing(Surface->bCompletingDeferredInitialization, true);
            Surface->InitializeRuntimeState();
            Surface->PreparedInitialState.Reset();
        }
        if (FPlatformTime::Seconds() >= Deadline) break;
    }
    const int32 JobLimit = Covered ? FMath::Clamp(CVarScrapeLoadingJobs.GetValueOnGameThread(), 4, 12) : 4;
    // Preparing a job also does game-thread work; share the commit deadline.
    while (Jobs.Num() < JobLimit && !Queue.IsEmpty() && FPlatformTime::Seconds() < Deadline)
    {
        FVector Origin = FVector::ZeroVector;
        if (auto* PC = GetWorld()->GetFirstPlayerController())
            if (APawn* Pawn = PC->GetPawn()) Origin = Pawn->GetActorLocation();
        int32 Nearest = 0;
        double Distance = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Queue.Num(); ++I)
            if (auto* Surface = Queue[I].Get())
                if (auto* Owner = Surface->GetOwner())
                {
                    const double D = FVector::DistSquared(Origin, Owner->GetActorLocation());
                    if (D < Distance) { Distance = D; Nearest = I; }
                }
        auto* Surface = Queue[Nearest].Get();
        Queue.RemoveAtSwap(Nearest);
        if (!Surface || !Surface->bDeferredInitializationPending || !Surface->SurfaceMesh) continue;
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InitializationDispatch);
        Surface->PrepareContaminationGeneration();
        auto Job = MakeUnique<FJob>();
        Job->Surface = Surface;
        Job->Data.Reset(Surface->ContaminationMeshUVData.Get());
        if (!Job->Data.IsValid()) { Surface->bDeferredInitializationPending = false; continue; }
        Job->Data->BeginVisualPreparation();
        Job->Layout = Job->Data->GetSharedCellLayout();
        Job->Weights = Job->Data->GetSharedCellCarrierWeights(Surface->SurfaceMesh->GetComponentTransform());
        Job->Epoch = Surface->DeferredInitializationEpoch;
        Job->Mesh = Surface->SurfaceMesh;
        Job->StaticMesh = Surface->SurfaceMesh->GetStaticMesh();
        Job->Scale = Surface->SurfaceMesh->GetComponentScale();
        Job->Tolerance = Surface->ScrapeSettings.CleanTolerance;
        Job->Revision = Job->Data->GetRuntimeCacheRevision();
        Job->Generation = Surface->ContaminationGeneration;
        if (!Job->Layout || !Job->Weights || !Job->Generation.IsValid())
        {
            Surface->bDeferredInitializationPending = false;
            TGuardValue<bool> Completing(Surface->bCompletingDeferredInitialization, true);
            Surface->InitializeRuntimeState();
            continue;
        }
        const auto* Data = Job->Data.Get();
        const auto* Layout = Job->Layout.Get();
        const auto Weights = Job->Weights->GetView();
        const auto Generation = Job->Generation;
        const float Tolerance = Job->Tolerance;
        Job->Result = Async(EAsyncExecution::ThreadPool, [Data, Layout, Weights, Generation, Tolerance]
        {
            FMD_ScrapeInitialState Result;
            Result.Amounts = MD_ScrapeInitialState::Generate(*Data, *Layout, Generation);
            MD_ScrapeInitialState::CalculateTotals(Result, *Layout, Weights, Tolerance);
            return Result;
        });
        Jobs.Add(MoveTemp(Job));
    }
}

void UMD_ScrapeInitializationSubsystem::Deinitialize()
{
    bStopping = true;
    Queue.Empty();
    // Keep pinned immutable profiles alive until readers have left; never touch a destroyed component.
    for (const auto& Job : Jobs) Job->Result.Wait();
    Jobs.Empty();
    Super::Deinitialize();
}
