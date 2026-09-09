#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

namespace
{
    TAutoConsoleVariable<int32> CVarSolidContamination(
        TEXT("MD.Solid.Enabled"), 0,
        TEXT("Experimental solid crust rendering (default off after thickness rollback). RefreshSolidContaminationVisual reapplies this switch."),
        ECVF_Default);
}

void UMD_ScrapeSurfaceComponent::ResolveDefaultSolidMaterial()
{
    if (!ContaminationSolidMaterial && bSolidContaminationEnabled && bContaminationEnabled
        && CVarSolidContamination.GetValueOnGameThread() != 0 && GetWorld()
        && GetWorld()->GetNetMode() != NM_DedicatedServer)
    {
        ContaminationSolidMaterial = DefaultContaminationSolidMaterial.LoadSynchronous();
        bLoadedDefaultSolidMaterial = ContaminationSolidMaterial != nullptr;
    }
}

bool UMD_ScrapeSurfaceComponent::TryGetSolidPrewarmInputs(
    const UMD_ScrapeMeshUVData*& OutData, FVector& OutWallScale,
    FMD_ContaminationSolidSettings& OutSettings)
{
    check(IsInGameThread());
    OutData = nullptr;
    ResolveDefaultSolidMaterial();
    if (!IsRegistered() || !bSolidContaminationEnabled || CVarSolidContamination.GetValueOnGameThread() == 0 ||
        !bContaminationEnabled || !bShowContaminationDecals || !UsesMeshUVSurface() ||
        !IsValid(ContaminationMeshUVData) || !IsValid(ContaminationSolidMaterial) ||
        !GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
        return false;
    if (!SurfaceMesh) ResolveSurfaceMeshFromOwner();
    if (!IsValid(SurfaceMesh) || !SurfaceMesh->IsRegistered()) return false;
    OutData = ContaminationMeshUVData;
    OutWallScale = SurfaceMesh->GetComponentScale();
    OutSettings = SolidContaminationSettings;
    return true;
}

bool UMD_ScrapeSurfaceComponent::ConfigureSolidContaminationVisual()
{
    ResolveDefaultSolidMaterial();
    if (!bSolidContaminationEnabled || CVarSolidContamination.GetValueOnGameThread() == 0
        || !bContaminationEnabled || !bShowContaminationDecals || !bContaminationSurfaceInitialized
        || !UsesMeshUVSurface() || !SurfaceMesh || !ContaminationMeshUVData
        || !ContaminationSolidMaterial || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
    {
        ReleaseSolidContaminationVisual();
        return false;
    }
    if (!ContaminationSolidVisual)
    {
        ContaminationSolidVisual = NewObject<UMD_ContaminationSolidVisualComponent>(
            GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
        ContaminationSolidVisual->RegisterComponent();
    }
    const bool bConfigured = ContaminationSolidVisual->Configure(SurfaceMesh,
        ContaminationMeshUVData, ContaminationSolidMaterial, GetVisualContaminationCells(),
        SolidContaminationSettings);
    if (bConfigured) ContaminationSolidVisual->SetVisualsVisible(true);
    if (!bConfigured)
    {
        UE_LOG(LogTemp, Warning, TEXT("Solid contamination: %s uses the original film; %s"),
            *GetOwner()->GetName(), *ContaminationSolidVisual->GetSummary());
    }
    return bConfigured;
}

void UMD_ScrapeSurfaceComponent::ReleaseSolidContaminationVisual()
{
    if (bLoadedDefaultSolidMaterial)
    {
        ContaminationSolidMaterial = nullptr;
        bLoadedDefaultSolidMaterial = false;
    }
    if (ContaminationSolidVisual)
    {
        ContaminationSolidVisual->DestroyComponent();
        ContaminationSolidVisual = nullptr;
    }
}

void UMD_ScrapeSurfaceComponent::UpdateSolidContaminationVisual(
    const TArray<FMD_ScrapeCellChange>& CellChanges)
{
    if (!ContaminationSolidVisual || CellChanges.IsEmpty()) return;
    TArray<int32, TInlineAllocator<128>> Indices;
    Indices.Reserve(CellChanges.Num());
    for (const FMD_ScrapeCellChange& Change : CellChanges) Indices.Add(Change.CellIndex);
    ContaminationSolidVisual->UpdateCells(Indices, GetVisualContaminationCells());
}

void UMD_ScrapeSurfaceComponent::RefreshSolidContaminationVisual()
{
    ConfigureSolidContaminationVisual();
}

void UMD_ScrapeSurfaceComponent::PreviewSolidContamination()
{
#if WITH_EDITOR
    if (!GetWorld() || GetWorld()->IsGameWorld()) return;
    bEditorSolidPreview = true;
    InitializeSurface();
    RefreshSolidContaminationVisual();
#endif
}

void UMD_ScrapeSurfaceComponent::ClearSolidContaminationPreview()
{
#if WITH_EDITOR
    if (GetWorld() && !GetWorld()->IsGameWorld() && bEditorSolidPreview)
    {
        ShutdownRuntimeState();
        // Preview is an explicit editor allocation; release its retained capacities.
        ContaminationCells.Empty();
        ContaminationGeneratedInitialAmounts.Empty();
        ContaminationMaskAtlasPixels.Empty();
        TransferredPaintMaskAtlasPixels.Empty();
        bEditorSolidPreview = false;
    }
#endif
}

FString UMD_ScrapeSurfaceComponent::GetSolidContaminationStats() const
{
    return ContaminationSolidVisual ? ContaminationSolidVisual->GetSummary()
        : TEXT("Solid contamination inactive");
}

bool UMD_ScrapeSurfaceComponent::TryGetSolidVisualWorldContact(
    const FVector2D& SurfacePositionCm, const FVector& BaseWorldPosition,
    const FVector& WorldNormal, FVector& OutPosition) const
{
    OutPosition = BaseWorldPosition;
    if (!ContaminationSolidVisual || !SurfaceMesh || !UsesMeshUVSurface()
        || BaseWorldPosition.ContainsNaN() || WorldNormal.ContainsNaN()
        || ResolvedContaminationSurfaceSizeCm.X <= 0 || ResolvedContaminationSurfaceSizeCm.Y <= 0)
        return false;

    const FVector2D UV(
        SurfacePositionCm.X / ResolvedContaminationSurfaceSizeCm.X + 0.5,
        SurfacePositionCm.Y / ResolvedContaminationSurfaceSizeCm.Y + 0.5);
    const FTransform& Transform = SurfaceMesh->GetComponentTransform();
    const FVector LocalNormal = (Transform.InverseTransformVectorNoScale(WorldNormal)
        * Transform.GetScale3D()).GetSafeNormal();
    const float Height = ContaminationSolidVisual->SampleVisualOffset(
        Transform.InverseTransformPosition(BaseWorldPosition), UV, LocalNormal);
    if (!FMath::IsFinite(Height) || Height <= 0.0f) return false;
    OutPosition += WorldNormal.GetSafeNormal() * Height;
    return true;
}
