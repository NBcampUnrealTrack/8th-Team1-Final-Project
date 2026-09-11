#include "Component/Scanning/MD_ScannerComponent.h"
#include "World/Contamination/MD_SurfaceStain.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimerManager.h"

void UMD_ScannerComponent::AddContaminationHighlight(UMD_ScrapeSurfaceComponent* Surface)
{
    if (!IsValid(Surface) || !ContaminationMaskMaterial || Surface->IsContaminationFullyRemoved()) return;
    UStaticMeshComponent* Base = Surface->GetSurfaceMesh();
    if (!IsValid(Base) || !Base->GetStaticMesh() || !Base->IsVisible()) return;
    auto* Material = UMaterialInstanceDynamic::Create(ContaminationMaskMaterial, this);
    if (!Surface->ConfigureContaminationScanMaterial(Material)) return;
    auto* Mask = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient);
    Mask->SetMobility(EComponentMobility::Movable);
    Mask->SetupAttachment(Base);
    Mask->SetRelativeTransform(FTransform::Identity);
    Mask->SetStaticMesh(Base->GetStaticMesh());
    for (int32 Slot = 0; Slot < Base->GetNumMaterials(); ++Slot) Mask->SetMaterial(Slot, Material);
    Mask->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mask->SetGenerateOverlapEvents(false);
    Mask->SetCanEverAffectNavigation(false);
    Mask->SetCastShadow(false);
    Mask->SetRenderInMainPass(false);
    Mask->SetRenderInDepthPass(false);
    Mask->SetCustomDepthStencilValue(MDScanning::ContaminationStencil);
    Mask->SetRenderCustomDepth(true);
    Mask->RegisterComponent();
    FMD_ContaminationScanVisual& State = ContaminationHighlights.AddDefaulted_GetRef();
    State.Surface = Surface; State.MaskMesh = Mask; State.Material = Material;
    // Film mask identifies thin residue; the existing raised shell supplies its actual depth.
    if (UPrimitiveComponent* Solid = Surface->GetSolidContaminationScanMesh(); Solid && Solid->IsVisible())
        State.SolidState.Apply(Solid, MDScanning::ContaminationStencil);
}

void UMD_ScannerComponent::RefreshContaminationHighlights()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scanner_RefreshCleanup);
    for (int32 Index = ContaminationHighlights.Num() - 1; Index >= 0; --Index)
    {
        FMD_ContaminationScanVisual& State = ContaminationHighlights[Index];
        UMD_ScrapeSurfaceComponent* Surface = State.Surface.Get();
        if (!IsValid(Surface) || !IsValid(Surface->GetOwner()) || Surface->GetOwner()->IsHidden()
            || !IsValid(Surface->GetSurfaceMesh()) || !Surface->GetSurfaceMesh()->IsVisible()
            || Surface->IsContaminationFullyRemoved() || !Surface->ConfigureContaminationScanMaterial(State.Material))
        {
            State.SolidState.Restore();
            if (IsValid(State.MaskMesh)) State.MaskMesh->DestroyComponent();
            ContaminationHighlights.RemoveAtSwap(Index);
            continue;
        }
        // Copy-on-write mask textures, regeneration and authoritative updates remain live.
        UPrimitiveComponent* Solid = Surface->GetSolidContaminationScanMesh();
        if (IsValid(Solid) && Solid->IsVisible()) State.SolidState.Apply(Solid, MDScanning::ContaminationStencil);
        else State.SolidState.Restore();
    }
    if (ContaminationHighlights.IsEmpty() && GetWorld())
        GetWorld()->GetTimerManager().ClearTimer(ContaminationRefreshTimer);
}

void UMD_ScannerComponent::ClearContaminationHighlights()
{
    for (const TWeakObjectPtr<AMD_SurfaceStain>& Stain : StainHighlights)
        if (Stain.IsValid()) Stain->SetScanHighlighted(this, false);
    StainHighlights.Empty();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ContaminationRefreshTimer);
    for (FMD_ContaminationScanVisual& State : ContaminationHighlights)
    {
        State.SolidState.Restore();
        if (IsValid(State.MaskMesh)) State.MaskMesh->DestroyComponent();
    }
    ContaminationHighlights.Empty();
}
