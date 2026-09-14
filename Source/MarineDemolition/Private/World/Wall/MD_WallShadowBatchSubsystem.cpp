#include "World/Wall/MD_WallShadowBatchSubsystem.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/World.h"
#include "Components/LightComponent.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"

namespace
{
    TAutoConsoleVariable<int32> CVarWallShadowBatch(TEXT("MD.Wall.ShadowBatch"), 1,
        TEXT("Batch nearby eligible opaque raster shadows using their original single-LOD geometry."));
    // Limit batch bounds: far-away instances must not make every light interact
    // with one map-wide primitive. Transform changes migrate an instance as needed.
    FVector BatchOrigin(const UStaticMeshComponent& Source)
    {
        constexpr double CellSize = 2000.0;
        const FVector P = Source.GetComponentLocation();
        return FVector(FMath::FloorToDouble(P.X / CellSize), FMath::FloorToDouble(P.Y / CellSize),
            FMath::FloorToDouble(P.Z / CellSize)) * CellSize;
    }
    bool RendererEligible(UWorld* World)
    {
        static const auto* VirtualShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
        static const auto* RayShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows"));
        if (!CVarWallShadowBatch.GetValueOnGameThread() || !VirtualShadows || !VirtualShadows->GetInt()
            || (RayShadows && RayShadows->GetInt())) return false;
        for (TObjectIterator<ULightComponent> It; It; ++It)
            if (It->GetWorld() == World && It->GetCastRaytracedShadow() == ECastRayTracedShadow::Enabled) return false;
        return true;
    }
    bool CanBatch(const UStaticMeshComponent& Source, bool bAlreadyBatched = false)
    {
        if (!Source.GetWorld() || !Source.GetWorld()->IsGameWorld()
            || Source.GetWorld()->GetNetMode() == NM_DedicatedServer
            || !Source.GetOwner() || !Source.IsRegistered()
            || Source.GetClass() != UStaticMeshComponent::StaticClass()
            || !Source.GetStaticMesh() || !Source.CastShadow || (!bAlreadyBatched && !Source.bCastDynamicShadow)
            || !Source.IsVisible() || Source.bHiddenInGame || Source.GetOwner()->IsHidden()
            || Source.bCastInsetShadow || Source.bSelfShadowOnly || Source.bCastHiddenShadow
            || Source.bCastVolumetricTranslucentShadow || Source.bCastCinematicShadow
            || Source.GetMinDrawDistance() != 0 || Source.LDMaxDrawDistance != 0 || Source.CachedMaxDrawDistance != 0
            || Source.BoundsScale != 1.0f || Source.GetComponentTransform().ContainsNaN()
            || Source.GetComponentTransform().GetDeterminant() == 0) return false;
        // Keep Nanite and view-dependent mesh LOD selection on their original paths.
        const UStaticMesh* Mesh = Source.GetStaticMesh();
        return !(Mesh->HasValidNaniteData() && !Source.bDisallowNanite)
            && Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() == 1;
    }
    bool MaterialsEligible(const UStaticMeshComponent& Source)
    {
        for (int32 Slot = 0; Slot < Source.GetNumMaterials(); ++Slot)
        {
            const UMaterialInterface* Material = Source.GetMaterial(Slot);
            if (!Material || Material->GetMaterial()->MaterialDomain != MD_Surface) return false;
            const auto Relevance = Material->GetRelevance(GShaderPlatformForFeatureLevel[Source.GetWorld()->GetFeatureLevel()]);
            if (!Relevance.bOpaque || Relevance.bMasked || Relevance.bTwoSided
                || Relevance.bUsesWorldPositionOffset || Relevance.bUsesDisplacement
                || Relevance.bUsesPixelDepthOffset || Relevance.bUsesDistanceCullFade) return false;
        }
        return Source.GetNumMaterials() > 0;
    }
    bool Matches(const UInstancedStaticMeshComponent& Batch, const UStaticMeshComponent& Source)
    {
        return Batch.GetStaticMesh() == Source.GetStaticMesh()
            && Batch.GetComponentLocation() == BatchOrigin(Source)
            && (Batch.GetComponentTransform().GetDeterminant() < 0) == (Source.GetComponentTransform().GetDeterminant() < 0)
            && Batch.IsReverseCulling() == Source.IsReverseCulling()
            && Batch.bCastShadowAsTwoSided == Source.bCastShadowAsTwoSided
            && Batch.bCastContactShadow == Source.bCastContactShadow
            && Batch.bCastFarShadow == Source.bCastFarShadow
            && Batch.LightingChannels.bChannel0 == Source.LightingChannels.bChannel0
            && Batch.LightingChannels.bChannel1 == Source.LightingChannels.bChannel1
            && Batch.LightingChannels.bChannel2 == Source.LightingChannels.bChannel2;
    }
}

void UMD_WallShadowBatchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (InWorld.GetNetMode() == NM_DedicatedServer) return;
    bRendererEligible = RendererEligible(&InWorld);
    // Discover only repeated, eligible ordinary meshes. Newly streamed/spawned
    // non-surface actors retain their original rendering; no hidden asset edits.
    TMap<UStaticMesh*, TArray<UStaticMeshComponent*>> Repeated;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
        if (It->GetWorld() == &InWorld && CanBatch(**It) && MaterialsEligible(**It))
            Repeated.FindOrAdd(It->GetStaticMesh()).Add(*It);
    for (auto& Pair : Repeated)
        if (Pair.Value.Num() >= 4)
            for (auto* Source : Pair.Value) RegisterWall(*Source);
}

bool UMD_WallShadowBatchSubsystem::RegisterWall(UStaticMeshComponent& Source)
{
    if (!GetWorld()->IsGameWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return false;
    if (Entries.Contains(&Source)) return true;
    if (Entries.IsEmpty()) bRendererEligible = RendererEligible(GetWorld());
    FEntry& Entry = Entries.Add(&Source);
    Entry.TransformHandle = Source.TransformUpdated.AddWeakLambda(this,
        [this, WeakSource = TWeakObjectPtr<UStaticMeshComponent>(&Source)](USceneComponent*, EUpdateTransformFlags, ETeleportType)
        { if (auto* Component = WeakSource.Get()) UpdateWallTransform(*Component); });
    RefreshEntry(Source, Entry);
    if (!TickHandle.IsValid()) TickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this,
        &UMD_WallShadowBatchSubsystem::PostActorTick);
    return Entry.Batch.IsValid();
}

void UMD_WallShadowBatchSubsystem::RefreshEntry(UStaticMeshComponent& Source, FEntry& Entry)
{
    if (Entry.bOwnsDynamicShadowFlag && !Entry.Batch.IsValid()) Detach(&Source, Entry);
    bool bMaterialChanged = Source.IsRenderStateDirty() || Entry.Materials.Num() != Source.GetNumMaterials();
    if (!bMaterialChanged)
        for (int32 Slot = 0; Slot < Entry.Materials.Num(); ++Slot)
            if (Entry.Materials[Slot].Get() != Source.GetMaterial(Slot)) { bMaterialChanged = true; break; }
    if (bMaterialChanged)
    {
        Entry.Materials.Reset(Source.GetNumMaterials());
        for (int32 Slot = 0; Slot < Source.GetNumMaterials(); ++Slot) Entry.Materials.Add(Source.GetMaterial(Slot));
        Entry.bMaterialsEligible = MaterialsEligible(Source);
    }
    const bool bEnabled = bRendererEligible;
    if (auto* Existing = Entry.Batch.Get())
    {
        if (bEnabled && Entry.bMaterialsEligible && CanBatch(Source, true) && Matches(*Existing, Source)) return;
        Detach(&Source, Entry);
    }
    if (!bEnabled || !Entry.bMaterialsEligible || !CanBatch(Source)) return;
    UInstancedStaticMeshComponent* Batch = nullptr;
    for (UInstancedStaticMeshComponent* Candidate : Batches) if (Matches(*Candidate, Source)) { Batch = Candidate; break; }
    if (!Batch)
    {
        Batch = NewObject<UInstancedStaticMeshComponent>(GetWorld()->GetWorldSettings(), NAME_None, RF_Transient);
        Batch->SetMobility(EComponentMobility::Movable);
        Batch->SetStaticMesh(Source.GetStaticMesh());
        // Mirror the component, so all relative instance transforms have positive
        // winding. ISM reverse culling then agrees with the original mirrored mesh.
        Batch->SetWorldTransform(FTransform(FQuat::Identity, BatchOrigin(Source),
            FVector(Source.GetComponentTransform().GetDeterminant() < 0 ? -1.0 : 1.0, 1.0, 1.0)));
        Batch->SetReverseCulling(Source.IsReverseCulling());
        Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch->SetGenerateOverlapEvents(false);
        Batch->SetCanEverAffectNavigation(false);
        Batch->SetReceivesDecals(false);
        Batch->SetRenderInMainPass(false);
        Batch->SetRenderInDepthPass(false);
        Batch->SetVisibleInRayTracing(false);
        Batch->SetAffectDynamicIndirectLighting(false);
        Batch->SetAffectDistanceFieldLighting(false);
        Batch->SetCastHiddenShadow(true); // WorldSettings is hidden; individual wall visibility owns membership.
        Batch->bCastShadowAsTwoSided = Source.bCastShadowAsTwoSided;
        Batch->bCastContactShadow = Source.bCastContactShadow;
        Batch->bCastFarShadow = Source.bCastFarShadow;
        Batch->SetLightingChannels(Source.LightingChannels.bChannel0,
            Source.LightingChannels.bChannel1, Source.LightingChannels.bChannel2);
        for (int32 Slot = 0; Slot < Source.GetNumMaterials(); ++Slot)
            Batch->SetMaterial(Slot, UMaterial::GetDefaultMaterial(MD_Surface));
        Batch->RegisterComponent();
        Batches.Add(Batch);
    }
    Entry.Batch = Batch;
    Entry.Id = Batch->AddInstanceById(Source.GetComponentTransform(), true);
    if (!Batch->IsValidId(Entry.Id)) { Entry.Batch.Reset(); return; }
    // Preserve CastShadow as the user's on/off switch. The dynamic flag redirects
    // only raster shadow subjects; main/overlay meshes and collision remain unchanged.
    Source.bCastDynamicShadow = false;
    Source.MarkRenderStateDirty();
    Entry.bOwnsDynamicShadowFlag = true;
}

void UMD_WallShadowBatchSubsystem::UpdateWallTransform(UStaticMeshComponent& Source)
{
    FEntry* Entry = Entries.Find(&Source);
    if (!Entry) return;
    RefreshEntry(Source, *Entry);
    if (auto* Batch = Entry->Batch.Get())
        Batch->UpdateInstanceTransformById(Entry->Id, Source.GetComponentTransform(), true, true);
}

void UMD_WallShadowBatchSubsystem::Detach(UStaticMeshComponent* Source, FEntry& Entry)
{
    if (auto* Batch = Entry.Batch.Get())
    {
        Batch->RemoveInstanceById(Entry.Id);
        if (Batch->GetInstanceCount() == 0)
        {
            Batches.Remove(Batch);
            Batch->DestroyComponent();
        }
    }
    if (Entry.bOwnsDynamicShadowFlag)
    {
        if (Source) { Source->bCastDynamicShadow = true; Source->MarkRenderStateDirty(); }
        Entry.bOwnsDynamicShadowFlag = false;
    }
    Entry.Batch.Reset();
}

void UMD_WallShadowBatchSubsystem::UnregisterWall(UStaticMeshComponent& Source)
{
    FEntry Entry;
    if (!Entries.RemoveAndCopyValue(&Source, Entry)) return;
    Source.TransformUpdated.Remove(Entry.TransformHandle);
    Detach(&Source, Entry);
    if (Entries.IsEmpty()) { FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset(); }
}

void UMD_WallShadowBatchSubsystem::PostActorTick(UWorld* World, ELevelTick, float)
{
    if (World != GetWorld()) return;
    // Arbitrary attached surface meshes can change visibility without a delegate.
    // One compact pass after actor ticks updates membership before rendering, with
    // no geometry work and no per-wall actor/component tick.
    bRendererEligible = RendererEligible(World);
    for (auto It = Entries.CreateIterator(); It; ++It)
    {
        if (auto* Source = It.Key().Get())
        {
            FEntry& Entry = It.Value();
            if (!bRendererEligible) Detach(Source, Entry);
            else
            {
                const auto* Batch = Entry.Batch.Get();
                // Material, lighting and shadow setters mark render state dirty;
                // transform changes are handled by TransformUpdated. Keep only
                // cheap lifetime/visibility checks in the unchanged-frame path.
                if (!Batch || !Source->IsRegistered() || Source->IsRenderStateDirty()
                    || !Source->CastShadow || !Source->IsVisible() || Source->bHiddenInGame
                    || !Source->GetOwner() || Source->GetOwner()->IsHidden()
                    || Source->GetStaticMesh() != Batch->GetStaticMesh())
                    RefreshEntry(*Source, Entry);
            }
        }
        else { Detach(nullptr, It.Value()); It.RemoveCurrent(); }
    }
}

void UMD_WallShadowBatchSubsystem::Deinitialize()
{
    TArray<TWeakObjectPtr<UStaticMeshComponent>> Sources;
    Entries.GetKeys(Sources);
    for (const auto& Source : Sources) if (auto* Component = Source.Get()) UnregisterWall(*Component);
    Entries.Empty();
    FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset();
    for (UInstancedStaticMeshComponent* Batch : Batches) if (Batch) Batch->DestroyComponent();
    Batches.Empty();
    Super::Deinitialize();
}
