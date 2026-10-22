// Native active-stage RT residency for private fitted meshes.
// Keeps source geometry, raster buffers, visible RT flags and serialized mesh data unchanged.
#include "MD_StainRayTracingResidency.h"
#include "CoreMinimal.h"
#include "Async/Async.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "RHICommandList.h"
#include "StaticMeshResources.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "World/Contamination/MD_SurfaceStain.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Editor/TransBuffer.h"
#endif
#if RHI_RAYTRACING
#include "RayTracingGeometry.h"
#include "RayTracingGeometryManagerInterface.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMDStainRTResidency, Log, All);

namespace MD_StainRayTracingResidency
{
namespace Private
{
static constexpr uint8 AllStages = 0x1f;
static TAutoConsoleVariable<int32> Enabled(TEXT("MD.Stain.PlacedRTResidency"), 1,
    TEXT("Placed-stain RT residency. 0 restores all five stages; 1 manages eligible active stages."));

#if RHI_RAYTRACING
struct FSlot
{
    FRayTracingGeometry* Geometry = nullptr;
    FRayTracingGeometryInitializer SavedInitializer;
    RayTracing::FGeometryGroupHandle GroupHandle;
    int8 LODIndex = 0;
    bool Captured = false;
    bool Released = false;
};
struct FRenderState
{
    // Access slots only on the render thread. Game-thread code reads the atomic status.
    FSlot Slots[5];
    FString DebugName; // Set once at group creation; read-only afterwards on any thread.
    TAtomic<bool> Aborted{false};
    TAtomic<bool> Stale{false};
    TAtomic<uint8> ReleasedMask{0};
};
struct FGroup
{
    UMD_SurfaceStainDefinition* DefinitionKey = nullptr; // Identity only; never dereferenced after invalidation.
    UWorld* WorldKey = nullptr; // Registration-time identity only; never dereferenced during cleanup.
    TWeakObjectPtr<UMD_SurfaceStainDefinition> Definition;
    // Pins FRayTracingGeometry storage until a retired group's fence completes.
    // Only the game thread owns these UObject pins; render lambdas own FRenderState.
    TArray<TStrongObjectPtr<UStaticMesh>> Meshes;
    TSharedRef<FRenderState, ESPMode::ThreadSafe> Render = MakeShared<FRenderState, ESPMode::ThreadSafe>();
    int32 Leases[5] = {};
    int32 ConsumerCount = 0; // Includes registered zero-stage consumers with no displayed mesh.
    uint8 RequestedMask = AllStages;
    bool BaselineOnly = false;
    bool PolicyQueued = false;
    bool ConsumersChecked = false;
};
struct FLease
{
    TWeakObjectPtr<UStaticMeshComponent> Component;
    TSharedPtr<FGroup> Group;
    int32 Stage = INDEX_NONE;
};
static TMap<UMD_SurfaceStainDefinition*, TSharedPtr<FGroup>> Groups;
static TMap<UStaticMeshComponent*, FLease> Components;
struct FRetiredGroup
{
    TSharedPtr<FGroup> Group;
    FRenderCommandFence Fence;
};
static TArray<TUniquePtr<FRetiredGroup>> RetiredGroups;
static FTSTicker::FDelegateHandle RetireTicker;
static FDelegateHandle ExitHandle;
static FDelegateHandle WorldCleanupHandle;
static bool CallbacksInstalled = false;
static bool LastEnabled = true;
#if WITH_EDITOR
static TWeakObjectPtr<UTransBuffer> UndoBuffer;
static FDelegateHandle BeforeUndoHandle;
static FDelegateHandle PrePropertyHandle;
static TMap<TWeakObjectPtr<UStaticMesh>,FDelegateHandle> PreBuildHandles;
struct FPendingStage
{
    TWeakObjectPtr<UStaticMeshComponent> Component;
    TWeakObjectPtr<UMD_SurfaceStainDefinition> Definition;
    TWeakObjectPtr<UStaticMesh> ActiveMesh;
};
static TMap<TWeakObjectPtr<UStaticMeshComponent>,FPendingStage> PendingStages;
static FDelegateHandle AssetPostCompileHandle;
#endif

static bool CanManage(const TCHAR** OutReason=nullptr)
{
    auto Fail=[OutReason](const TCHAR* Reason) { if (OutReason) *OutReason=Reason; return false; };
    if (!IsInGameThread()) return Fail(TEXT("not on the game thread"));
    if (!FApp::CanEverRender() || IsRunningCommandlet() || !GDynamicRHI) return Fail(TEXT("process cannot render"));
    if (!IsRayTracingAllowed() || !IsRayTracingEnabled() || !GRayTracingGeometryManager) return Fail(TEXT("ray tracing disabled"));
    if (Enabled.GetValueOnGameThread()==0) return Fail(TEXT("MD.Stain.PlacedRTResidency is 0"));
    return true;
}
static FString DescribeComponent(const UStaticMeshComponent& Component)
{
    const UWorld* World=Component.GetWorld();
    return FString::Printf(TEXT("%s (world %s, begun play %d)"), *GetNameSafe(Component.GetOwner()),
        *GetPathNameSafe(World), World && World->HasBegunPlay() ? 1 : 0);
}
static int32 FindStage(const FGroup& Group, const UStaticMesh* Mesh)
{
    for (int32 I=0; I<Group.Meshes.Num(); ++I) if (Group.Meshes[I].Get()==Mesh) return I;
    return INDEX_NONE;
}
static uint8 DesiredMask(const FGroup& Group)
{
    uint8 Mask=0;
    for (int32 I=0; I<5; ++I) if (Group.Leases[I]>0) Mask|=uint8(1u<<I);
    return Group.BaselineOnly ? AllStages : Mask;
}
static uint8 LeaseMask(const FGroup& Group)
{
    uint8 Mask=0;
    for (int32 I=0; I<5; ++I) if (Group.Leases[I]>0) Mask|=uint8(1u<<I);
    return Mask;
}
static bool AnyLease(const FGroup& Group)
{
    return Group.ConsumerCount>0;
}
static void Apply(const TSharedRef<FRenderState,ESPMode::ThreadSafe>& State, uint8 Mask, uint8 ForceBuildMask,
    FRHICommandListImmediate& Cmd)
{
    if (State->Stale.Load()) return;
    auto Abort=[&State](const TCHAR* Reason, int32 Stage)
    {
        if (!State->Aborted.Exchange(true))
            UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Aborted %s at stage %d: %s"), *State->DebugName, Stage, Reason);
    };
    // Stage leases require independent native resources. Reject aliases before
    // capture/release so a hidden slot cannot release another slot's displayed RT.
    for (int32 I=0; I<5; ++I)
        for (int32 J=0; J<I; ++J)
            if (State->Slots[I].Geometry==State->Slots[J].Geometry)
            { Abort(TEXT("stage geometries alias"), I); return; }
    // Validate/capture the whole group before releasing any slot. Unsupported or
    // asynchronously rebuilt render data must fail open to the baseline.
    for (int32 SlotIndex=0; SlotIndex<5; ++SlotIndex)
    {
        FSlot& Slot=State->Slots[SlotIndex];
        if (!Slot.Captured)
        {
            if (!Slot.Geometry || !Slot.Geometry->IsInitialized() || !Slot.Geometry->HasValidInitializer()
                || Slot.Geometry->GroupHandle.IsNull())
            { Abort(TEXT("geometry not initialized, initializer invalid or no group"), SlotIndex); return; }
            const auto& Initializer=Slot.Geometry->GetInitializer();
            // Consumed cooked offline bytes cannot be reconstructed here. D3D12
            // sizes a BLAS from a valid offline header even with OfflineData==null.
            // Reject the whole group before release, rather than alter metadata.
            if (Initializer.OfflineDataHeader.IsValid() || Slot.Geometry->RawDataHeader.IsValid()
                || Initializer.Type!=ERayTracingGeometryInitializerType::Rendering || Initializer.bTemplate
                || Initializer.SourceGeometry || Initializer.GeometryType!=RTGT_Triangles
                || Slot.Geometry->DynamicGeometrySharedBufferGenerationID!=FRayTracingGeometry::NonSharedVertexBuffers)
            { Abort(TEXT("offline data, template, source geometry or shared buffers"), SlotIndex); return; }
            Slot.SavedInitializer=Slot.Geometry->GetInitializer();
            Slot.SavedInitializer.OfflineData=nullptr; // Uploaded offline storage is not owned by this cache.
            Slot.GroupHandle=Slot.Geometry->GroupHandle;
            Slot.LODIndex=Slot.Geometry->LODIndex;
            Slot.Captured=true;
        }
    }
    if (State->Aborted.Load()) Mask=AllStages;
    TArray<const FRayTracingGeometry*> Build;
    // New displayed stages must be initialized/built before any old stage is released.
    for (int32 I=0; I<5; ++I)
    {
        FSlot& Slot=State->Slots[I];
        if (!(Mask&(1u<<I))) continue;
        if (Slot.Released)
        {
            Slot.Geometry->SetInitializer(Slot.SavedInitializer);
            Slot.Geometry->GroupHandle=Slot.GroupHandle;
            Slot.Geometry->LODIndex=Slot.LODIndex;
            Slot.Geometry->InitResource(Cmd);
            Slot.Released=false;
        }
        if ((ForceBuildMask&(1u<<I)) && Slot.Geometry->HasPendingBuildRequest()) Build.Add(Slot.Geometry);
    }
    if (!Build.IsEmpty()) GRayTracingGeometryManager->ForceBuildIfPending(Cmd, Build);
    for (int32 I=0; I<5; ++I)
    {
        FSlot& Slot=State->Slots[I];
        if (!(Mask&(1u<<I)) && !Slot.Released)
        {
            Slot.Geometry->ReleaseResource(); // Only RT storage, never raster VB/IB or the UStaticMesh.
            Slot.Released=true;
        }
        GRayTracingGeometryManager->RequestUpdateCachedRenderState(Slot.GroupHandle);
    }
    uint8 Released=0;
    for (int32 I=0; I<5; ++I) if (State->Slots[I].Released) Released|=uint8(1u<<I);
    if (State->ReleasedMask.Exchange(Released)!=Released)
        UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Released stage mask 0x%02x for %s"), Released, *State->DebugName);
}
static void QueuePolicy(FGroup& Group, uint8 Mask, bool Force=false, uint8 ForceBuildMask=AllStages)
{
    if (!Force && Group.PolicyQueued && Group.RequestedMask==Mask) return;
    Group.RequestedMask=Mask; Group.PolicyQueued=true;
    auto State=Group.Render;
    ENQUEUE_RENDER_COMMAND(MD_StainApplyRTResidency)([State,Mask,ForceBuildMask](FRHICommandListImmediate& Cmd)
    {
        if (!IsRayTracingAllowed() || !GRayTracingGeometryManager)
        {
            if (!State->Aborted.Exchange(true))
                UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Aborted %s: ray tracing unavailable on the render thread"), *State->DebugName);
            return;
        }
        Apply(State,Mask,ForceBuildMask,Cmd);
    });
}
static void Fence()
{
    FRenderCommandFence Pending; Pending.BeginFence(); Pending.Wait();
}
static void RetainUntilFence(const TSharedPtr<FGroup>& Group)
{
    if (!Group) return;
    auto Retired=MakeUnique<FRetiredGroup>();
    Retired->Group=Group;
    Retired->Fence.BeginFence();
    RetiredGroups.Add(MoveTemp(Retired));
    if (!RetireTicker.IsValid())
        RetireTicker=FTSTicker::GetCoreTicker().AddTicker(TEXT("MD.Stain.RetireRTResidency"),0.0f,[](float)
        {
            check(IsInGameThread());
            for (int32 I=RetiredGroups.Num()-1; I>=0; --I)
                if (RetiredGroups[I]->Fence.IsFenceComplete()) RetiredGroups.RemoveAtSwap(I);
            if (!RetiredGroups.IsEmpty()) return true;
            RetireTicker.Reset();
            return false;
        });
}
static void Retire(const TSharedPtr<FGroup>& Group)
{
    if (!Group) return;
    // Restore undo/reuse resources, but let normal manager scheduling build hidden
    // stages. No game-thread Wait or forced five-stage BLAS rebuild on deletion.
    QueuePolicy(*Group,AllStages,true,0);
    Groups.Remove(Group->DefinitionKey);
    RetainUntilFence(Group);
}
static void RemoveLease(UStaticMeshComponent* Key, bool OldProxyRemoved=false)
{
    FLease Lease;
    if (!Components.RemoveAndCopyValue(Key,Lease) || !Lease.Group) return;
    if (Lease.Stage>=0 && Lease.Stage<5) --Lease.Group->Leases[Lease.Stage];
    --Lease.Group->ConsumerCount;
    if (!AnyLease(*Lease.Group)) Retire(Lease.Group);
    else if (OldProxyRemoved) QueuePolicy(*Lease.Group,DesiredMask(*Lease.Group),false,LeaseMask(*Lease.Group));
}
static bool Eligible(UStaticMeshComponent& Component, UMD_SurfaceStainDefinition* Definition,
    UStaticMesh* Active, const TCHAR** OutReason=nullptr, int32* OutStage=nullptr,
    bool* OutWaitingForCompilation=nullptr)
{
    if (OutWaitingForCompilation) *OutWaitingForCompilation=false;
    // Same predicate as before; the split only records the first failing condition.
    auto Fail=[OutReason,OutStage](const TCHAR* Reason, int32 Stage=INDEX_NONE)
    { if (OutReason) *OutReason=Reason; if (OutStage) *OutStage=Stage; return false; };
    const AMD_SurfaceStain* Owner=Cast<AMD_SurfaceStain>(Component.GetOwner());
    if (!Owner || !Definition || Definition->GetOuter()!=Owner) return Fail(TEXT("definition is missing or not owned by the stain actor"));
    if (!Definition->UsesSharedVolume() || !Definition->IsUsable() || Definition->VolumeStages.Num()!=5)
        return Fail(TEXT("definition is unusable or does not have five volume stages"));
    if (!Component.IsRegistered() || !Component.GetWorld() || !Component.GetWorld()->Scene)
        return Fail(TEXT("component is not registered in a world with a scene"));
    if (!Active && Owner->GetRemainingSize()!=0) return Fail(TEXT("no active mesh while size remains"));
    if (Component.GetWorld()->HasBegunPlay() && Component.GetMobility()==EComponentMobility::Static
        && Component.GetStaticMesh()!=Active) return Fail(TEXT("static mesh change after BeginPlay"));
    for (int32 I=0; I<5; ++I)
    {
        UStaticMesh* Mesh=Definition->VolumeStages[I].Mesh.Get();
        for (int32 J=0; J<I; ++J)
            if (Definition->VolumeStages[J].Mesh.Get()==Mesh) return Fail(TEXT("stage mesh is shared with another stage"), I);
        if (!Mesh) return Fail(TEXT("stage mesh is missing"), I);
        if (Mesh->IsCompiling())
        {
            if (OutWaitingForCompilation) *OutWaitingForCompilation=true;
            return Fail(TEXT("stage mesh is still compiling"), I);
        }
        const FStaticMeshRenderData* Data=Mesh->GetRenderData();
        if (Mesh->GetOuter()!=Definition || !Mesh->bSupportRayTracing || Mesh->bDoFastBuild
#if WITH_EDITOR
            || Mesh->IsNaniteEnabled()
#else
            || Mesh->HasValidNaniteData()
#endif
            || !Mesh->NeverStream) return Fail(TEXT("stage mesh outer or build flags are unsupported"), I);
        if (!Data || !Data->IsInitialized()) return Fail(TEXT("stage render data is not initialized"), I);
        if (Data->LODResources.Num()!=1 || !Data->RayTracingProxy
            || !Data->RayTracingProxy->bUsingRenderingLODs || Data->RayTracingProxy->LODs.Num()!=1
            || !Data->RayTracingProxy->LODs[0].RayTracingGeometry
            || Data->RayTracingProxy->LODs[0].StreamableData.GetBulkDataSize()!=0)
            return Fail(TEXT("stage ray tracing proxy layout is unsupported"), I);
    }
    return !Active || Definition->VolumeStages.ContainsByPredicate([Active](const auto& S){return S.Mesh==Active;})
        ? true : Fail(TEXT("active mesh is not one of the stages"));
}
static bool HasExternalConsumer(const FGroup& Group, const UStaticMeshComponent* Current)
{
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* C=*It;
        if (C==Current || C->IsTemplate() || !C->IsRegistered() || FindStage(Group,C->GetStaticMesh())==INDEX_NONE) continue;
        const FLease* Lease=Components.Find(C);
        if (!Lease || Lease->Group.Get()!=&Group) return true;
    }
    return false;
}
static bool DataStillMatches(const FGroup& Group)
{
    for (int32 I=0; I<5; ++I)
    {
        UStaticMesh* Mesh=Group.Meshes[I].Get();
        const auto* Data=Mesh && !Mesh->IsCompiling() ? Mesh->GetRenderData() : nullptr;
        if (!Data || !Data->IsInitialized() || !Data->RayTracingProxy || Data->RayTracingProxy->LODs.Num()!=1
            || Data->RayTracingProxy->LODs[0].RayTracingGeometry!=Group.Render->Slots[I].Geometry) return false;
    }
    return true;
}
static void DiscardStaleGroup(const TSharedPtr<FGroup>& Group)
{
    // A rebuilt mesh already owns its fresh baseline render resources. Never try
    // to restore deleted FRayTracingGeometry pointers from the former render data.
    Group->Render->Stale.Store(true);
    for (auto It=Components.CreateIterator(); It; ++It)
        if (It.Value().Group==Group) It.RemoveCurrent();
    Groups.Remove(Group->DefinitionKey);
    RetainUntilFence(Group);
}
static void RestoreTargetGroup(UStaticMesh* Active)
{
    for (auto& Pair : Groups)
        if (FindStage(*Pair.Value,Active)!=INDEX_NONE)
        {
            Pair.Value->BaselineOnly=true;
            if (DataStillMatches(*Pair.Value))
                QueuePolicy(*Pair.Value,AllStages,true,uint8(LeaseMask(*Pair.Value)|(1u<<FindStage(*Pair.Value,Active))));
            else Pair.Value->Render->Stale.Store(true);
        }
}
#if WITH_EDITOR
static void AfterAssetCompilation(const TArray<FAssetCompileData>&)
{
    check(IsInGameThread());
    TArray<FPendingStage> Ready;
    for (auto It=PendingStages.CreateIterator(); It; ++It)
    {
        const FPendingStage& Pending=It.Value();
        UStaticMeshComponent* Component=Pending.Component.Get();
        UMD_SurfaceStainDefinition* Definition=Pending.Definition.Get();
        if (!Component || !Component->IsRegistered() || !Definition || Pending.ActiveMesh.IsStale())
        {
            It.RemoveCurrent();
            continue;
        }
        const bool StillCompiling=Definition->VolumeStages.ContainsByPredicate([](const auto& Stage)
        { return Stage.Mesh && Stage.Mesh->IsCompiling(); });
        if (!StillCompiling)
        {
            Ready.Add(Pending);
            It.RemoveCurrent();
        }
    }
    // SetStage can modify the pending map or trigger another compile notification.
    // Remove ready entries before retrying, and resolve weak references again.
    for (const FPendingStage& Pending : Ready)
    {
        UStaticMeshComponent* Component=Pending.Component.Get();
        UMD_SurfaceStainDefinition* Definition=Pending.Definition.Get();
        if (Component && Component->IsRegistered() && Definition && !Pending.ActiveMesh.IsStale())
            SetStage(*Component,Definition,Pending.ActiveMesh.Get());
    }
}
static bool HasManagedMesh(const UStaticMesh* Mesh)
{
    for (const auto& Pair : Groups) if (FindStage(*Pair.Value,Mesh)!=INDEX_NONE) return true;
    for (const auto& Item : RetiredGroups) if (FindStage(*Item->Group,Mesh)!=INDEX_NONE) return true;
    return false;
}
static void BeforeMeshBuild(UStaticMesh* Mesh)
{
    // StaticMeshBuild.cpp: BeginBuildInternal broadcasts before ReleaseResources.
    if (HasManagedMesh(Mesh)) RestoreAll();
}
static void BeforePropertyChange(UObject* Object, const FEditPropertyChain&)
{
    // Despite its name this event follows virtual PreEditChange(FProperty*).
    // UStaticMesh may already have queued ReleaseResources. RestoreAll validates
    // IsInitialized and discards stale state instead of resurrecting invalid handles.
    if (const auto* Mesh=Cast<UStaticMesh>(Object); Mesh && HasManagedMesh(Mesh)) RestoreAll();
}
static void InstallEditorCallbacks()
{
    // SetStage can run before GEditor->Trans exists. Retry only this weak binding.
    UTransBuffer* Current=GEditor ? Cast<UTransBuffer>(GEditor->Trans) : nullptr;
    if (Current!=UndoBuffer.Get())
    {
        if (auto* Previous=UndoBuffer.Get()) Previous->OnBeforeRedoUndo().Remove(BeforeUndoHandle);
        BeforeUndoHandle.Reset(); UndoBuffer=Current;
        if (Current) BeforeUndoHandle=Current->OnBeforeRedoUndo().AddLambda([](const FTransactionContext&)
        { RestoreAll(); }); // EditorTransaction.cpp: broadcast immediately before Transaction.Apply.
    }
    if (!PrePropertyHandle.IsValid())
        PrePropertyHandle=FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddStatic(&BeforePropertyChange);
}
static void RemoveMeshBuildCallbacks()
{
    for (const auto& Pair : PreBuildHandles)
        if (auto* Mesh=Pair.Key.Get()) Mesh->OnPreMeshBuild().Remove(Pair.Value);
    PreBuildHandles.Reset();
}
#endif
static void BeforeWorldCleanup(UWorld* World, bool, bool)
{
    check(IsInGameThread());
    if (!World) return;
#if WITH_EDITOR
    for (auto It=PendingStages.CreateIterator(); It; ++It)
        if (!It.Value().Component.IsValid() || It.Value().Component->GetWorld()==World) It.RemoveCurrent();
#endif
    TArray<TSharedPtr<FGroup>> ClosingGroups;
    for (const auto& Pair : Groups)
        if (Pair.Value->WorldKey==World) ClosingGroups.AddUnique(Pair.Value);
    for (const auto& Item : RetiredGroups)
        if (Item->Group->WorldKey==World) ClosingGroups.AddUnique(Item->Group);
    if (ClosingGroups.IsEmpty()) return;

    // World.cpp broadcasts before renderer/level resource cleanup. Restore only
    // this world's native RT resources while their storage is pinned and valid.
    // A next-frame retire ticker cannot run before synchronous NewMap GC.
    for (const auto& Group : ClosingGroups)
        if (DataStillMatches(*Group)) QueuePolicy(*Group,AllStages,true,0);
        else Group->Render->Stale.Store(true);
    Fence(); // One shutdown-only wait; all queued accesses finish before pins drop.

#if WITH_EDITOR
    for (const auto& Group : ClosingGroups)
        for (const auto& MeshPin : Group->Meshes)
        {
            FDelegateHandle Handle;
            if (UStaticMesh* Mesh=MeshPin.Get(); Mesh && PreBuildHandles.RemoveAndCopyValue(Mesh,Handle))
                Mesh->OnPreMeshBuild().Remove(Handle);
        }
#endif
    for (auto It=Components.CreateIterator(); It; ++It)
        if (It.Value().Group && It.Value().Group->WorldKey==World) It.RemoveCurrent();
    for (auto It=Groups.CreateIterator(); It; ++It)
        if (It.Value()->WorldKey==World) It.RemoveCurrent();
    RetiredGroups.RemoveAllSwap([World](const auto& Item){return Item->Group->WorldKey==World;});
    if (RetiredGroups.IsEmpty() && RetireTicker.IsValid())
    {
        FTSTicker::RemoveTicker(RetireTicker);
        RetireTicker.Reset();
    }
    ClosingGroups.Reset(); // Release mesh -> definition -> actor -> world pins before GC.
}
static void InstallCallbacks()
{
#if WITH_EDITOR
    InstallEditorCallbacks();
#endif
    if (CallbacksInstalled) return;
    CallbacksInstalled=true;
    LastEnabled=Enabled.GetValueOnGameThread()!=0;
    ExitHandle=FCoreDelegates::OnEnginePreExit.AddStatic(&Shutdown);
    WorldCleanupHandle=FWorldDelegates::OnWorldCleanup.AddStatic(&BeforeWorldCleanup);
#if WITH_EDITOR
    AssetPostCompileHandle=FAssetCompilingManager::Get().OnAssetPostCompileEvent().AddStatic(&AfterAssetCompilation);
#endif
    Enabled.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* V)
    {
        const bool Now=V->GetInt()!=0;
        auto Changed=[Now]()
        {
            if (LastEnabled==Now) return;
            LastEnabled=Now;
            if (!Now) RestoreAll(); // One action on a real transition, no per-frame CVar sink.
        };
        if (IsInGameThread()) Changed(); else AsyncTask(ENamedThreads::GameThread,MoveTemp(Changed));
    }));
}
#endif
} // Private

void SetStage(UStaticMeshComponent& Component, UMD_SurfaceStainDefinition* Definition, UStaticMesh* ActiveMesh)
{
    using namespace Private;
    check(IsInGameThread());
#if RHI_RAYTRACING
    InstallCallbacks();
    const TCHAR* Reason=nullptr;
    int32 ReasonStage=INDEX_NONE;
    bool WaitingForCompilation=false;
    if (!CanManage(&Reason) || !Eligible(Component,Definition,ActiveMesh,&Reason,&ReasonStage,&WaitingForCompilation))
    {
#if WITH_EDITOR
        if (WaitingForCompilation)
            PendingStages.Add(&Component,FPendingStage{&Component,Definition,ActiveMesh});
        else PendingStages.Remove(&Component);
#endif
        UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Unmanaged %s: %s (stage %d)"),
            *DescribeComponent(Component), Reason ? Reason : TEXT("unknown"), ReasonStage);
        // Ineligible/shared aliases may still select a mesh from an already
        // managed group. Restore it before SetStaticMesh can create its proxy.
        RestoreTargetGroup(ActiveMesh);
        RemoveLease(&Component);
        Component.SetStaticMesh(ActiveMesh);
        return;
    }
#if WITH_EDITOR
    PendingStages.Remove(&Component);
#endif
    TSharedPtr<FGroup>& Stored=Groups.FindOrAdd(Definition);
    if (!Stored)
    {
        Stored=MakeShared<FGroup>(); Stored->DefinitionKey=Definition; Stored->Definition=Definition;
        Stored->WorldKey=Component.GetWorld();
        Stored->Render->DebugName=DescribeComponent(Component);
        for (int32 I=0; I<5; ++I)
        {
            UStaticMesh* Mesh=Definition->VolumeStages[I].Mesh.Get();
            Stored->Meshes.Emplace(Mesh);
            Stored->Render->Slots[I].Geometry=Mesh->GetRenderData()->RayTracingProxy->LODs[0].RayTracingGeometry;
#if WITH_EDITOR
            if (!PreBuildHandles.Contains(Mesh))
                PreBuildHandles.Add(Mesh,Mesh->OnPreMeshBuild().AddStatic(&BeforeMeshBuild));
#endif
        }
    }
    TSharedPtr<FGroup> Group=Stored;
    if (!DataStillMatches(*Group))
    {
        UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Discarded stale group %s: render data changed"), *DescribeComponent(Component));
        // Not a supported resource-rebuild notification hook. Never dereference old
        // render pointers after an editor mesh rebuild: disable/restore BEFORE rebuild.
        DiscardStaleGroup(Group);
        Component.SetStaticMesh(ActiveMesh);
        return;
    }
    const int32 Stage=FindStage(*Group,ActiveMesh);
    FLease* Existing=Components.Find(&Component);
    const bool SameLease=Existing && Existing->Group==Group && Existing->Stage==Stage;
    if (!Group->ConsumersChecked)
    {
        // One discovery scan per group, not on every cleaning amount/stage refresh.
        // Late external access remains an explicit integration boundary below.
        if (HasExternalConsumer(*Group,&Component))
        {
            Group->BaselineOnly=true;
            UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Baseline only %s: another component uses a stage mesh"), *DescribeComponent(Component));
        }
        Group->ConsumersChecked=true;
    }
    if (Group->Render->Aborted.Load()) Group->BaselineOnly=true;
    if (SameLease && Component.GetStaticMesh()==ActiveMesh && Group->PolicyQueued
        && Group->RequestedMask==DesiredMask(*Group)) return; // No render-state recreation.
    // Destroy old proxy first. SetStaticMesh may create the new proxy itself while
    // this context is alive; therefore queue activation BEFORE that call. Its dtor
    // only recreates a proxy if SetStaticMesh has not already done so.
    FComponentRecreateRenderStateContext Context(&Component);
    if (!SameLease)
    {
        if (Existing && Existing->Group==Group)
        {
            // A same-definition stage change transfers its lease atomically; do
            // not retire/recreate the whole group at the intermediate zero count.
            if (Existing->Stage>=0 && Existing->Stage<5) --Group->Leases[Existing->Stage];
            Components.Remove(&Component);
        }
        else
        {
            RemoveLease(&Component,true);
            ++Group->ConsumerCount;
        }
        if (Stage>=0 && Stage<5) ++Group->Leases[Stage];
        Components.Add(&Component,FLease{&Component,Group,Stage});
        UE_LOG(LogMDStainRTResidency, Verbose, TEXT("Managed %s: stage %d, baseline only %d"),
            *DescribeComponent(Component), Stage, Group->BaselineOnly ? 1 : 0);
    }
    // A baseline-only group's mask may stay AllStages while its newly selected
    // visible stage still has a normal-priority pending build from an alias restore.
    QueuePolicy(*Group,DesiredMask(*Group),!SameLease,LeaseMask(*Group));
    Component.SetStaticMesh(ActiveMesh);
#else
    Component.SetStaticMesh(ActiveMesh);
#endif
}
void ForgetComponent(UStaticMeshComponent& Component)
{
    check(IsInGameThread());
#if RHI_RAYTRACING
#if WITH_EDITOR
    Private::PendingStages.Remove(&Component);
#endif
    // A still registered consumer is restored to the baseline, not silently stripped.
    if (auto* Lease=Private::Components.Find(&Component))
    {
        if (Component.IsRegistered() && Component.IsRenderStateCreated())
        {
            Lease->Group->BaselineOnly=true;
            if (Private::DataStillMatches(*Lease->Group))
                Private::QueuePolicy(*Lease->Group,Private::AllStages,true,Private::LeaseMask(*Lease->Group));
            else Lease->Group->Render->Stale.Store(true);
        }
    }
    Private::RemoveLease(&Component,true);
#endif
}
void RestoreAll()
{
    check(IsInGameThread());
#if RHI_RAYTRACING
#if WITH_EDITOR
    Private::PendingStages.Reset();
#endif
    if (Private::Groups.IsEmpty() && Private::RetiredGroups.IsEmpty())
    {
#if WITH_EDITOR
        Private::RemoveMeshBuildCallbacks();
#endif
        return;
    }
    for (auto& Pair : Private::Groups)
        if (Private::DataStillMatches(*Pair.Value)) Private::QueuePolicy(*Pair.Value,Private::AllStages,true);
        else Pair.Value->Render->Stale.Store(true);
    for (const auto& Item : Private::RetiredGroups)
        if (Private::DataStillMatches(*Item->Group)) Private::QueuePolicy(*Item->Group,Private::AllStages,true);
        else Item->Group->Render->Stale.Store(true);
    Private::Fence();
#if WITH_EDITOR
    Private::RemoveMeshBuildCallbacks();
#endif
    Private::Components.Reset(); Private::Groups.Reset();
    if (Private::RetireTicker.IsValid()) FTSTicker::RemoveTicker(Private::RetireTicker);
    Private::RetireTicker.Reset();
    Private::RetiredGroups.Reset();
#endif
}
void Shutdown()
{
    check(IsInGameThread());
#if RHI_RAYTRACING
    RestoreAll();
    if (Private::ExitHandle.IsValid()) FCoreDelegates::OnEnginePreExit.Remove(Private::ExitHandle);
    Private::ExitHandle.Reset();
    if (Private::WorldCleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(Private::WorldCleanupHandle);
    Private::WorldCleanupHandle.Reset();
    Private::Enabled.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());
#if WITH_EDITOR
    if (Private::AssetPostCompileHandle.IsValid())
        FAssetCompilingManager::Get().OnAssetPostCompileEvent().Remove(Private::AssetPostCompileHandle);
    Private::AssetPostCompileHandle.Reset();
    if (auto* Buffer=Private::UndoBuffer.Get()) Buffer->OnBeforeRedoUndo().Remove(Private::BeforeUndoHandle);
    Private::UndoBuffer.Reset(); Private::BeforeUndoHandle.Reset();
    FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(Private::PrePropertyHandle);
    Private::PrePropertyHandle.Reset();
#endif
    Private::CallbacksInstalled=false;
#endif
}
} // MD_StainRayTracingResidency

// Eligibility and lifecycle contract; Shipping/cooked execution is not yet verified:
// - Manage actor-owned fitted definitions with five distinct normal, non-Nanite,
//   NeverStream meshes, one rendering LOD and RT proxies using those rendering LODs.
//   Repeated mesh/native geometry identities retain the baseline before any release.
// - ForgetComponent after unregister/destruction. World cleanup restores and drops
//   only that world's pins after one fence; Shutdown must precede module/RHI teardown.
// - A registered zero-size private consumer keeps its mesh/query UObjects but leases
//   no RT stages. Retire/restore all stages only after its final component lease ends.
// - No runtime universal SetStaticMesh notification exists for untracked late
//   consumers. Inline actor ownership is an eligibility boundary, NOT proof that
//   external native code cannot obtain these meshes. RestoreAll before such access.
// - Editor native undo/prebuild hooks restore first. Property pre-change can occur
//   after mesh release; it discards stale state. These do not cover arbitrary direct
//   render-data replacement. RestoreAll BEFORE such edits, RT capability changes,
//   cook, or module unload. Runtime private fitted meshes currently do not rebuild.
// - Offline/cooked headers, templates and shared dynamic buffers fail open before
//   any release. Native runtime/cooked fallback, render-pipe ordering and scene cache
//   invalidation still require validation; the same GPU raster buffers are reused.
// - A first load still initializes all five BLAS before inactive ones are released.
// - Integration must retain unregister/world-cleanup/module callbacks so actor-owned
//   mesh/definition outers are not pinned past their lifetime.
