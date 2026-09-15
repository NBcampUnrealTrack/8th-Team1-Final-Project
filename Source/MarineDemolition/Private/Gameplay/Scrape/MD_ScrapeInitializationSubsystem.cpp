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
    const int32 JobLimit = Covered ? FMath::Clamp(CVarScrapeLoadingJobs.GetValueOnGameThread(), 4, 12) : 4;
    // Refill released worker slots in this same frame. A completed surface must
    // not leave a vacant slot until the next tick while budget remains.
    for (int32 Pass = 0; Pass < 2 && FPlatformTime::Seconds() < Deadline; ++Pass)
    {
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
            Job->Epoch = Surface->DeferredInitializationEpoch;
            Job->Mesh = Surface->SurfaceMesh;
            Job->StaticMesh = Surface->SurfaceMesh->GetStaticMesh();
            Job->Scale = Surface->SurfaceMesh->GetComponentScale();
            Job->Tolerance = Surface->ScrapeSettings.CleanTolerance;
            Job->Revision = Job->Data->GetRuntimeCacheRevision();
            Job->Generation = Surface->ContaminationGeneration;
            Jobs.Add(MoveTemp(Job));
        }
        for (int32 I = Jobs.Num() - 1; I >= 0 && FPlatformTime::Seconds() < Deadline; --I)
        {
            auto& Job = *Jobs[I];
            auto* Surface = Job.Surface.Get();
            const bool Pending = Surface && Surface->bDeferredInitializationPending
                && Surface->DeferredInitializationEpoch == Job.Epoch;
            const bool Current = Pending && Surface->ContaminationMeshUVData == Job.Data.Get()
                && Job.Data->GetRuntimeCacheRevision() == Job.Revision
                && Surface->ContaminationGeneration == Job.Generation
                && Surface->SurfaceMesh == Job.Mesh.Get() && Job.Mesh.IsValid()
                && Job.Mesh->GetStaticMesh() == Job.StaticMesh.Get()
                && Job.Mesh->GetComponentScale() == Job.Scale
                && Surface->ScrapeSettings.CleanTolerance == Job.Tolerance;
            if (!Current)
            {
                // Retain immutable inputs until an already dispatched reader has left.
                if (Job.Result.IsValid() && !Job.Result.IsReady()) continue;
                if (Pending)
                {
                    Surface->bDeferredInitializationPending = false;
                    Surface->InitializeRuntimeState();
                }
                const bool SharedByAnotherJob = Jobs.ContainsByPredicate([&](const auto& Other)
                {
                    const auto* OtherSurface = Other->Surface.Get();
                    return Other.Get() != &Job && Other->Data.Get() == Job.Data.Get()
                        && OtherSurface && OtherSurface->bDeferredInitializationPending
                        && OtherSurface->DeferredInitializationEpoch == Other->Epoch;
                });
                if (!SharedByAnotherJob)
                {
                    Job.Data->CancelVisualPreparationSnapshot();
                    Job.Data->CancelInitializationPreparation();
                }
                Jobs.RemoveAtSwap(I);
                continue;
            }
            if (!Job.Layout)
            {
                bool PendingLayout = false;
                Job.Layout = Job.Data->GetSharedCellLayout(
                    float(FMath::Clamp((Deadline - FPlatformTime::Seconds()) * 1000.0, .0001, .5)), &PendingLayout);
                if (PendingLayout || FPlatformTime::Seconds() >= Deadline) continue;
            }
            if (!Job.Weights)
            {
                bool PendingWeights = false;
                Job.Weights = Job.Data->GetSharedCellCarrierWeights(Job.Mesh->GetComponentTransform(),
                    float(FMath::Clamp((Deadline - FPlatformTime::Seconds()) * 1000.0, .0001, .5)), &PendingWeights);
                if (PendingWeights || FPlatformTime::Seconds() >= Deadline) continue;
            }
            if (!Job.Layout || !Job.Weights || !Job.Generation.IsValid())
            {
                Surface->bDeferredInitializationPending = false;
                TGuardValue<bool> Completing(Surface->bCompletingDeferredInitialization, true);
                Surface->InitializeRuntimeState();
                Jobs.RemoveAtSwap(I);
                continue;
            }
            // Finish only the bounded owned snapshot on this thread. The existing
            // worker produces amounts, totals and the compact recipe in one job,
            // avoiding a second frame of latency or a game-thread ParallelFor join.
            const double RemainingMs = (Deadline - FPlatformTime::Seconds()) * 1000.0;
            if (RemainingMs <= 0.0) continue;
            Job.Data->BeginVisualPreparation(float(FMath::Min(RemainingMs, .5)));
            if (!Job.Data->IsVisualPreparationReady() || FPlatformTime::Seconds() >= Deadline) continue;
            if (!Job.Result.IsValid() && !Job.Prepared.IsSet())
            {
                FMD_ScrapeInitialState Initial;
                float FullSignal = 0.f;
                const bool MakeMask = Surface->GetDeferredInitialMaskInputs(Initial.MaskLayout, FullSignal);
                auto Seams = MakeMask ? Job.Data->GetSharedVisualSeams() : nullptr;
                const auto* Parallel = IConsoleManager::Get().FindConsoleVariable(TEXT("MD.Scrape.ParallelInitialMaskRecipe"));
                const bool bParallel = Parallel && Parallel->GetInt() != 0;
                const auto* Data = Job.Data.Get();
                const auto* Layout = Job.Layout.Get();
                const auto Weights = Job.Weights->GetView();
                const auto Generation = Job.Generation;
                const float Tolerance = Job.Tolerance;
                Job.Result = Async(EAsyncExecution::ThreadPool,
                    [Data, Layout, Weights, Generation, Tolerance, State = MoveTemp(Initial),
                     Seams = MoveTemp(Seams), FullSignal, bParallel]() mutable
                {
                    State.Amounts = MD_ScrapeInitialState::Generate(*Data, *Layout, Generation);
                    MD_ScrapeInitialState::CalculateTotals(State, *Layout, Weights, Tolerance);
                    if (State.Progress.bIsValid && State.MaskLayout && Seams)
                        MD_ScrapeInitialState::PrepareMaskRecipe(State, *Layout, Seams->GetView(),
                            FullSignal, bParallel, TNumericLimits<double>::Max());
                    else { State.MaskLayout.Reset(); State.bMaskRecipeComplete = true; }
                    return MoveTemp(State);
                });
            }
            if (!Job.Prepared.IsSet())
            {
                if (!Job.Result.IsReady()) continue;
                Job.Prepared = Job.Result.Consume();
            }
            if (FPlatformTime::Seconds() >= Deadline) continue;
            Surface->bDeferredInitializationPending = false;
            if (Job.Prepared->Progress.bIsValid) Surface->PreparedInitialState = MoveTemp(*Job.Prepared);
            TGuardValue<bool> Completing(Surface->bCompletingDeferredInitialization, true);
            Surface->InitializeRuntimeState();
            Surface->PreparedInitialState.Reset();
            Jobs.RemoveAtSwap(I);
        }
    }
    if (!IsPending()) { Queue.Empty(); Jobs.Empty(); }

}

void UMD_ScrapeInitializationSubsystem::Deinitialize()
{
    bStopping = true;
    Queue.Empty();
    // Keep pinned immutable profiles alive until readers have left; never touch a destroyed component.
    for (const auto& Job : Jobs)
    {
        Job->Data->CancelVisualPreparationSnapshot();
        Job->Data->CancelInitializationPreparation();
        if (Job->Result.IsValid()) Job->Result.Wait();
    }
    Jobs.Empty();
    Super::Deinitialize();
}
