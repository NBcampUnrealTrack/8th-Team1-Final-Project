#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"

namespace
{
    FVector MeshNormalToWorld(const FTransform& Transform, const FVector& Normal)
    {
        const FVector Scale = Transform.GetScale3D();
        if (Scale.GetAbs().GetMin() <= UE_SMALL_NUMBER)
        {
            return FVector::ZeroVector;
        }
        return Transform.TransformVectorNoScale(Normal / Scale).GetSafeNormal();
    }
}

bool UMD_ScrapeSurfaceComponent::UsesMeshUVSurface() const
{
    return ContaminationSurfaceMapping.Mode == EMD_ScrapeSurfaceMappingMode::MeshUV;
}

bool UMD_ScrapeSurfaceComponent::TryCalculateContaminationCellCounts(
    int32& OutCellsPerFace, int32& OutStateCellCount) const
{
    if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
        ContaminationGridDimensions, OutCellsPerFace, OutStateCellCount))
    {
        return false;
    }
    // All exposed physical faces already have unique addresses in the first atlas.
    // Preserve these external atlas addresses, including save and network layout IDs.
    if (UsesMeshUVSurface())
    {
        OutStateCellCount = OutCellsPerFace;
    }
    return true;
}

const UMD_ScrapeMeshUVData* UMD_ScrapeSurfaceComponent::GetContaminationMeshUVData() const
{
    return UsesMeshUVSurface() ? ContaminationMeshUVData.Get() : nullptr;
}

bool UMD_ScrapeSurfaceComponent::IsContaminationCellActive(const int32 CellIndex) const
{
    if (GetContaminationStorageIndex(CellIndex) == INDEX_NONE)
    {
        return false;
    }
    return !UsesMeshUVSurface()
        || (ContaminationMeshUVData && ContaminationMeshUVData->IsActiveCell(CellIndex));
}

int32 UMD_ScrapeSurfaceComponent::GetContaminationActiveCellCount() const
{
    return UsesMeshUVSurface()
        ? (ContaminationMeshUVData ? ContaminationMeshUVData->ValidCellCount : 0)
        : ContaminationCells.Num();
}

int32 UMD_ScrapeSurfaceComponent::GetContaminationLayoutVersion() const
{
    return UsesMeshUVSurface() ? 1 : 0;
}

FGuid UMD_ScrapeSurfaceComponent::GetContaminationLayoutId() const
{
    return UsesMeshUVSurface() && ContaminationMeshUVData
        ? ContaminationMeshUVData->LayoutId : FGuid();
}

FIntPoint UMD_ScrapeSurfaceComponent::GetContaminationGridDimensions() const
{
    return UsesMeshUVSurface() && ContaminationMeshUVData
        ? ContaminationMeshUVData->AtlasDimensions : ContaminationGridDimensions;
}

bool UMD_ScrapeSurfaceComponent::CanRestoreContaminationLayout(
    const int32 Version, const FGuid& LayoutId, const FIntPoint& Grid) const
{
    return AreContaminationCarrierWeightsCurrent() && FMD_ScrapeSurfaceLayout::IsSavedLayoutCompatible(
        Version, LayoutId, Grid, GetContaminationLayoutVersion(),
        GetContaminationLayoutId(), GetContaminationGridDimensions());
}

bool UMD_ScrapeSurfaceComponent::TryGetScrapeSurfaceWorldNormalAtPosition(
    const FVector2D& Position, const EMD_ScrapeSurfaceFace Face, FVector& OutNormal) const
{
    OutNormal = FVector::ZeroVector;
    if (!bContaminationEnabled || !bContaminationSurfaceInitialized || !SurfaceMesh)
    {
        return false;
    }
    if (!UsesMeshUVSurface())
    {
        return FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldNormal(
            Position, Face, SurfaceMesh->GetComponentTransform(),
            ResolvedContaminationSurfaceNormalAxis, ContaminationSurfaceMapping,
            ResolvedContaminationSurfaceSizeCm, OutNormal);
    }
    FVector2D UV;
    FMD_ScrapeMeshUVSample Sample;
    if (!AreContaminationCarrierWeightsCurrent() || Face != EMD_ScrapeSurfaceFace::Front || !ContaminationMeshUVData
        || !FMD_ScrapeSurfaceGeometry::TryConvertSurfacePositionCmToMeshUV(
            Position, ResolvedContaminationSurfaceSizeCm, UV)
        || !FMD_ScrapeMeshUVGeometry::SampleCanonicalUV(*ContaminationMeshUVData, UV, Sample))
    {
        return false;
    }
    OutNormal = MeshNormalToWorld(SurfaceMesh->GetComponentTransform(), Sample.NormalLocal);
    return !OutNormal.IsNearlyZero();
}

bool UMD_ScrapeSurfaceComponent::TryBuildMeshUVStroke(
    const FMD_ScrapeStroke& Stroke, FMD_ScrapeMeshUVStroke& OutStroke) const
{
    OutStroke = FMD_ScrapeMeshUVStroke();
    if (!UsesMeshUVSurface() || !AreContaminationCarrierWeightsCurrent()
        || !bContaminationSurfaceInitialized || !SurfaceMesh
        || !ContaminationMeshUVData || Stroke.SurfaceFace != EMD_ScrapeSurfaceFace::Front)
    {
        return false;
    }
    FVector2D StartUV, EndUV;
    FMD_ScrapeMeshUVSample Start, End;
    if (!FMD_ScrapeSurfaceGeometry::TryConvertSurfacePositionCmToMeshUV(
            Stroke.StartPositionCm, ResolvedContaminationSurfaceSizeCm, StartUV)
        || !FMD_ScrapeSurfaceGeometry::TryConvertSurfacePositionCmToMeshUV(
            Stroke.EndPositionCm, ResolvedContaminationSurfaceSizeCm, EndUV)
        || !FMD_ScrapeMeshUVGeometry::SampleCanonicalUV(*ContaminationMeshUVData, StartUV, Start)
        || !FMD_ScrapeMeshUVGeometry::SampleCanonicalUV(*ContaminationMeshUVData, EndUV, End))
    {
        return false;
    }
    const FTransform& Transform = SurfaceMesh->GetComponentTransform();
    OutStroke.StartWorldPosition = Transform.TransformPosition(Start.PositionLocal);
    OutStroke.EndWorldPosition = Transform.TransformPosition(End.PositionLocal);
    OutStroke.StartWorldNormal = MeshNormalToWorld(Transform, Start.NormalLocal);
    OutStroke.EndWorldNormal = MeshNormalToWorld(Transform, End.NormalLocal);
    OutStroke.StartWorldDepthAxis = Stroke.StartWorldBladeDepthAxis;
    OutStroke.EndWorldDepthAxis = Stroke.EndWorldBladeDepthAxis;
    OutStroke.StartTriangleIndex = Start.TriangleIndex;
    OutStroke.EndTriangleIndex = End.TriangleIndex;
    OutStroke.SurfaceFace = EMD_ScrapeSurfaceFace::Front;
    OutStroke.TravelDistanceCm = FVector::Distance(
        OutStroke.StartWorldPosition, OutStroke.EndWorldPosition);
    return !OutStroke.StartWorldNormal.IsNearlyZero() && !OutStroke.EndWorldNormal.IsNearlyZero();
}


bool UMD_ScrapeSurfaceComponent::InitializeContaminationCellCarrierWeights()
{
    SharedContaminationCellCarrierWeights.Reset();
    ContaminationCarrierWeightScale = FVector::OneVector;
    if (!UsesMeshUVSurface()) { return true; }
    if (!SurfaceMesh || !ContaminationMeshUVData) { return false; }
    SharedContaminationCellCarrierWeights = ContaminationMeshUVData->GetSharedCellCarrierWeights(
        SurfaceMesh->GetComponentTransform());
    if (!SharedContaminationCellCarrierWeights) { return false; }
    ContaminationCarrierWeightScale = SurfaceMesh->GetComponentScale();
    return true;
}

FMD_ScrapeCarrierWeightView UMD_ScrapeSurfaceComponent::GetContaminationCellCarrierWeights() const
{
    return SharedContaminationCellCarrierWeights
        ? SharedContaminationCellCarrierWeights->GetView() : FMD_ScrapeCarrierWeightView();
}

double UMD_ScrapeSurfaceComponent::GetContaminationCellCarrierWeight(const int32 CellIndex) const
{
    if (!UsesMeshUVSurface()) { return ContaminationCells.IsValidIndex(CellIndex) ? 1.0 : 0.0; }
    const FMD_ScrapeCarrierWeightView Weights = GetContaminationCellCarrierWeights();
    return Weights.IsValidIndex(CellIndex) ? Weights[CellIndex] : 0.0;
}

bool UMD_ScrapeSurfaceComponent::AreContaminationCarrierWeightsCurrent() const
{
    return !UsesMeshUVSurface() || (SurfaceMesh && ContaminationMeshUVData
        && GetContaminationCellCarrierWeights().Num() == ContaminationMeshUVData->Cells.Num()
        && !GetContaminationCellCarrierWeights().IsEmpty()
        && SurfaceMesh->GetComponentScale().Equals(ContaminationCarrierWeightScale, 1.0e-4));
}

FVector UMD_ScrapeSurfaceComponent::GetContaminationCarrierWeightScale() const
{
    return UsesMeshUVSurface() ? ContaminationCarrierWeightScale : FVector::OneVector;
}

int32 UMD_ScrapeSurfaceComponent::GetContaminationAtlasCellCount() const
{
    return ContaminationCellLayout ? ContaminationCellLayout->AtlasToActive.Num() : ContaminationCells.Num();
}

int32 UMD_ScrapeSurfaceComponent::GetContaminationStorageIndex(const int32 AtlasCellIndex) const
{
    if (!ContaminationCellLayout)
    {
        return ContaminationCells.IsValidIndex(AtlasCellIndex) ? AtlasCellIndex : INDEX_NONE;
    }
    const int32 Slot = ContaminationCellLayout->AtlasToActive.IsValidIndex(AtlasCellIndex)
        ? ContaminationCellLayout->AtlasToActive[AtlasCellIndex] : INDEX_NONE;
    return Slot >= 0 && Slot < GetContaminationStorageCellCount() ? Slot : INDEX_NONE;
}

int32 UMD_ScrapeSurfaceComponent::GetContaminationStorageCellCount() const
{
    return !ContaminationPagedCells.IsEmpty() ? ContaminationPagedCells.Num() : ContaminationCells.Num();
}

FMD_ScrapeSurfaceCell UMD_ScrapeSurfaceComponent::ReadContaminationStorageCell(const int32 Index) const
{
    return !ContaminationPagedCells.IsEmpty() ? ContaminationPagedCells.Read(Index) : ContaminationCells[Index];
}

FMD_ScrapeSurfaceCell& UMD_ScrapeSurfaceComponent::EditContaminationStorageCell(const int32 Index)
{
    return !ContaminationPagedCells.IsEmpty() ? ContaminationPagedCells.Edit(Index) : ContaminationCells[Index];
}

void UMD_ScrapeSurfaceComponent::SetContaminationStorageCell(const int32 Index, const FMD_ScrapeSurfaceCell& Cell)
{
    if (!ContaminationPagedCells.IsEmpty()) { ContaminationPagedCells.Set(Index, Cell); }
    else { ContaminationCells[Index] = Cell; }
}

bool UMD_ScrapeSurfaceComponent::HasGeneratedInitialContamination(const int32 Index) const
{
    return !ContaminationPagedCells.IsEmpty() ? ContaminationPagedCells.IsValidIndex(Index)
        : ContaminationGeneratedInitialAmounts.IsValidIndex(Index);
}

float UMD_ScrapeSurfaceComponent::GetGeneratedInitialContamination(const int32 Index) const
{
    if (!HasGeneratedInitialContamination(Index)) { return 0.0f; }
    return !ContaminationPagedCells.IsEmpty() ? ContaminationPagedCells.GetInitialCell(Index).InitialContamination
        : ContaminationGeneratedInitialAmounts[Index];
}

bool UMD_ScrapeSurfaceComponent::HasLocalPredictedContaminationState() const
{
    return !LocalPredictedPagedCells.IsEmpty() || !LocalPredictedContaminationCells.IsEmpty();
}

FMD_ScrapeCellView UMD_ScrapeSurfaceComponent::GetContaminationCellView()
{
    return !ContaminationPagedCells.IsEmpty()
        ? FMD_ScrapeCellView(&ContaminationPagedCells, ContaminationCellLayout.Get())
        : FMD_ScrapeCellView(MakeArrayView(ContaminationCells), ContaminationCellLayout.Get());
}

FMD_ScrapeConstCellView UMD_ScrapeSurfaceComponent::GetContaminationCellView() const
{
    return !ContaminationPagedCells.IsEmpty()
        ? FMD_ScrapeConstCellView(&ContaminationPagedCells, ContaminationCellLayout.Get())
        : FMD_ScrapeConstCellView(MakeArrayView(ContaminationCells), ContaminationCellLayout.Get());
}

bool UMD_ScrapeSurfaceComponent::GetContaminationCellState(const int32 AtlasCellIndex, FMD_ScrapeSurfaceCell& OutCell) const
{
    const int32 StorageIndex = GetContaminationStorageIndex(AtlasCellIndex);
    OutCell = FMD_ScrapeSurfaceCell();
    OutCell.InitialContamination = OutCell.RemainingContamination = 0.0f;
    if (StorageIndex < 0 || StorageIndex >= GetContaminationStorageCellCount()) { return false; }
    OutCell = ReadContaminationStorageCell(StorageIndex);
    return true;
}
