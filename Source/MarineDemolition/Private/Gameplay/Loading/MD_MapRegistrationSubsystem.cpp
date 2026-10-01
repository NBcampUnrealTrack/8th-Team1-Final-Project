#include "Gameplay/Loading/MD_MapRegistrationSubsystem.h"
#include "Game/MD_GameModeBase.h"
#include "World/Wall/MD_WallShadowBatchSubsystem.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"
#include "ContentStreaming.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    TAutoConsoleVariable<int32> CVarMapRegistrationEnabled(TEXT("MD.MapRegistration.Enabled"), 1,
        TEXT("Spread initial native static-mesh actor registration before server BeginPlay."));
    TAutoConsoleVariable<float> CVarMapRegistrationBudgetMs(TEXT("MD.MapRegistration.BudgetMs"), 8.f,
        TEXT("Soft initial map-registration budget; one actor is atomic."));
    TAutoConsoleVariable<int32> CVarMapRegistrationMaxActors(TEXT("MD.MapRegistration.MaxActorsPerFrame"), 1024,
        TEXT("Maximum actors in each initial registration batch."));
}

bool UMD_MapRegistrationSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    // PIE editor ticks lengthen the total load when registration spans frames.
    // Keep its ordinary startup; use batching in standalone/packaged game worlds.
    return Type == EWorldType::Game;
}

void UMD_MapRegistrationSubsystem::PostInitialize()
{
    Super::PostInitialize();
    CollectDeferredActors();
}

void UMD_MapRegistrationSubsystem::CollectDeferredActors()
{
    UWorld* World = GetWorld();
    if (!CVarMapRegistrationEnabled.GetValueOnGameThread() || !World || !World->PersistentLevel
        || World->HasBegunPlay() || World->GetNetMode() == NM_Client) return;
    const auto* Settings = World->GetWorldSettings();
    if (!Settings || !Settings->DefaultGameMode
        || !Settings->DefaultGameMode->IsChildOf(AMD_GameModeBase::StaticClass())) return;

    TRACE_CPUPROFILER_EVENT_SCOPE(MD_MapRegistration_Collect);
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        // Native actors have no Blueprint construction/initialization scripts to defer.
        // Moving and replicated actors keep the normal path. Registration resolves
        // attachment parents; already registered parents are safely reused.
        if (!IsValid(Actor) || Actor->GetClass() != AStaticMeshActor::StaticClass()
            || Actor->IsActorInitialized()) continue;
        if (Actor->GetIsReplicated()) { continue; }
        auto* Mesh = Cast<AStaticMeshActor>(Actor)->GetStaticMeshComponent();
        if (!Mesh || !Mesh->GetStaticMesh() || Mesh->IsRegistered()) continue;
        if (Mesh->Mobility != EComponentMobility::Static) { continue; }
        if (Mesh->BodyInstance.bSimulatePhysics) { continue; }
        FDeferredActor Entry;
        Entry.Actor = Actor;
        Entry.Mesh = Mesh;
        Entry.bIgnoredStreaming = Mesh->bIgnoreInstanceForTextureStreaming;
        TInlineComponentArray<UActorComponent*> Components(Actor);
        bool Eligible = true;
        for (UActorComponent* Component : Components)
        {
            if (!IsValid(Component) || Component->IsRegistered() || Component->HasBegunPlay())
            { Eligible = false; break; }
            if (Component->bAutoRegister) Entry.Components.Add(Component);
        }
        if (Eligible && !Entry.Components.IsEmpty()) Actors.Add(MoveTemp(Entry));
    }
    // Avoid extra scheduling for small maps such as the title/transition/lobby.
    if (Actors.Num() < 512) { Actors.Empty(); return; }
    bPreviousSimulatePhysics = World->bShouldSimulatePhysics;
    World->bShouldSimulatePhysics = false;
#if WITH_EDITOR
    bPreviousTraceCollision = World->bEnableTraceCollision;
    World->bEnableTraceCollision = false;
#endif
    bHoldingPhysics = true;
    for (FDeferredActor& Entry : Actors)
    {
        for (const auto& Weak : Entry.Components)
            if (UActorComponent* Component = Weak.Get()) Component->bAutoRegister = false;
        // Unregistered meshes are discarded by the initial static streaming build.
        // Avoid collecting their material data twice; registration publishes it later.
        if (auto* Mesh = Entry.Mesh.Get()) Mesh->bIgnoreInstanceForTextureStreaming = true;
    }
    FrameStartHandle = FCoreDelegates::OnBeginFrame.AddUObject(this, &ThisClass::BeginRegistrationFrame);
    bWaitForFirstBatch = true;
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(
        this, &ThisClass::TickRegistration));
}

void UMD_MapRegistrationSubsystem::RestoreAutoRegistration(FDeferredActor& Entry)
{
    if (Entry.bPolicyRestored) return;
    Entry.bPolicyRestored = true;
    for (const auto& Weak : Entry.Components)
        if (UActorComponent* Component = Weak.Get()) Component->bAutoRegister = true;
    if (auto* Mesh = Entry.Mesh.Get())
    {
        Mesh->bIgnoreInstanceForTextureStreaming = Entry.bIgnoredStreaming;
        // Attachment resolution can register a parent before its scheduled batch.
        // Such a parent needs an explicit refresh after restoring its original policy.
        if (Mesh->IsRegistered()) IStreamingManager::Get().NotifyPrimitiveUpdated(Mesh);
    }
}

void UMD_MapRegistrationSubsystem::RefreshLoadingScreen()
{
    if (UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
        if (auto* Loading = Instance->GetSubsystem<UMD_LoadingScreenSubsystem>())
            Loading->UpdateCoveredWorldRendering();
}

void UMD_MapRegistrationSubsystem::BeginRegistrationFrame()
{
    FrameStartSeconds = FPlatformTime::Seconds();
}

void UMD_MapRegistrationSubsystem::StopScheduling()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker); Ticker.Reset();
    FCoreDelegates::OnBeginFrame.Remove(FrameStartHandle); FrameStartHandle.Reset();
    FrameStartSeconds = 0.0;
    DeferredFrameCount = 0;
}

bool UMD_MapRegistrationSubsystem::TickRegistration(float)
{
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown)
    {
        for (FDeferredActor& Entry : Actors) RestoreAutoRegistration(Entry);
        Actors.Empty(); NextActor = 0; RestorePhysics(); StopScheduling();
        return false;
    }
    if (!World->AreActorsInitialized()) return true;
    auto* GameMode = Cast<AMD_GameModeBase>(World->GetAuthGameMode());
    // BeginPlay and player restart must have reached the explicit hold before work runs.
    if (!GameMode || !GameMode->IsStartPlayWaitingForMapRegistration()) return true;
    // The handoff frame already initializes the world and streaming managers.
    if (bWaitForFirstBatch)
    {
        bWaitForFirstBatch = false;
        RefreshLoadingScreen();
        return true;
    }
    // Core ticks run after the render fence. Do not add registration/Chaos work
    // to a frame that already spent its budget waiting for initial render resources.
    // Exclude the intentional FPS-cap sleep; it is not work competing with registration.
    const bool bLateFrame = FrameStartSeconds > 0.0
        && FPlatformTime::Seconds() - FrameStartSeconds - FApp::GetIdleTime() >= .020;
    if (bLateFrame && DeferredFrameCount < 2) { ++DeferredFrameCount; return true; }
    // If every frame is slow, make a small batch after two deferred frames so loading
    // still progresses. A fixed FPS cap or background throttling cannot starve it.
    DeferredFrameCount = 0;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_MapRegistration_Batch);
    const double WorkBudgetMs = bLateFrame ? .5 : FMath::Clamp(CVarMapRegistrationBudgetMs.GetValueOnGameThread(), .1f, 8.f);
    const double Deadline = FPlatformTime::Seconds() + WorkBudgetMs / 1000.0;
    const int32 Limit = FMath::Clamp(CVarMapRegistrationMaxActors.GetValueOnGameThread(), 1, bLateFrame ? 32 : 1024);
    FRegisterComponentContext Context(World);
    int32 Count = 0;
    while (IsPending() && Count++ < Limit && FPlatformTime::Seconds() < Deadline)
    {
        FDeferredActor& Entry = Actors[NextActor++];
        RestoreAutoRegistration(Entry);
        if (AActor* Actor = Entry.Actor.Get(); Actor && !Actor->IsActorBeingDestroyed())
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(MD_MapRegistration_Actor);
            Actor->IncrementalRegisterComponents(0, &Context);
        }
    }
    Context.Process();
    // Publish each batch to Chaos while gameplay physics is held. The solver's
    // explicit zero-delta flush creates proxies/broadphase entries without moving
    // bodies, including the fixed-step path; the first playable frame stays small.
    if (FPhysScene* Scene = World->GetPhysicsScene(); Scene && Scene->GetSolver())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_MapRegistration_PhysicsFlush);
        const FGraphEventRef Flush = Scene->GetSolver()->AdvanceAndDispatch_External(0.f);
        if (Flush) FTaskGraphInterface::Get().WaitUntilTaskCompletes(Flush);
    }
    if (IsPending()) return true;
    FinishRegistration();
    GameMode->FinishMapRegistration();
    return false;
}

void UMD_MapRegistrationSubsystem::FinishRegistration()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_MapRegistration_Finalize);
    StopScheduling();
    if (UWorld* World = GetWorld())
    {
        World->UpdateCullDistanceVolumes();
        UE_LOG(LogTemp, Display, TEXT("Map registration completed %d actors in %s"), Actors.Num(), *World->GetMapName());
    }
    Actors.Empty(); NextActor = 0; Ticker.Reset();
    if (UWorld* World = GetWorld())
        if (auto* Shadows = World->GetSubsystem<UMD_WallShadowBatchSubsystem>())
            Shadows->InitializeWorldBatches();
    RestorePhysics();
}

void UMD_MapRegistrationSubsystem::RestorePhysics()
{
    if (bHoldingPhysics)
    {
        if (UWorld* World = GetWorld())
        {
            World->bShouldSimulatePhysics = bPreviousSimulatePhysics;
#if WITH_EDITOR
            World->bEnableTraceCollision = bPreviousTraceCollision;
#endif
        }
        bHoldingPhysics = false;
    }
}

void UMD_MapRegistrationSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    // Draw the covered handoff frame before the new scene allocates its first
    // render resources. Resume on the first core tick, not after all registration.
    if (IsPending() && Cast<AMD_GameModeBase>(World.GetAuthGameMode())) RefreshLoadingScreen();
    // A URL can override the map's default mode. A foreign mode has no start gate,
    // so restore its ordinary synchronous registration before it dispatches BeginPlay.
    if (IsPending() && !Cast<AMD_GameModeBase>(World.GetAuthGameMode()))
    {
        FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
        FRegisterComponentContext Context(&World);
        for (FDeferredActor& Entry : Actors)
        {
            RestoreAutoRegistration(Entry);
            if (AActor* Actor = Entry.Actor.Get()) Actor->IncrementalRegisterComponents(0, &Context);
        }
        Context.Process();
        FinishRegistration();
    }
}

void UMD_MapRegistrationSubsystem::Deinitialize()
{
    StopScheduling();
    for (FDeferredActor& Entry : Actors) RestoreAutoRegistration(Entry);
    Actors.Empty(); NextActor = 0;
    RestorePhysics();
    RefreshLoadingScreen();
    Super::Deinitialize();
}
