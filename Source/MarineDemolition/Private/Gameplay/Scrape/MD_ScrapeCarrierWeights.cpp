#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"

bool FMD_ScrapeCarrierWeights::Initialize(const UMD_ScrapeMeshUVData& Data, const FTransform& Transform)
{
    SurfaceData.Reset();
    Dense.Empty();
    TriangleAreaScales.Empty();
    CellData = nullptr;
    CellCount = 0;
    TotalWeight = 0.0;
    ReferenceArea = FMath::Square(static_cast<double>(Data.TargetCellSizeCm));
    const FVector Scale = Transform.GetScale3D();
    if (Transform.ContainsNaN() || !FMath::IsFinite(ReferenceArea) || ReferenceArea <= UE_SMALL_NUMBER
        || FMath::Abs(Scale.X) <= UE_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER
        || FMath::Abs(Scale.Z) <= UE_SMALL_NUMBER) return false;
    TriangleAreaScales.SetNumZeroed(Data.Triangles.Num());
    for (int32 Index = 0; Index < Data.Triangles.Num(); ++Index)
    {
        const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[Index];
        const FVector EdgeA = Triangle.B - Triangle.A;
        const FVector EdgeB = Triangle.C - Triangle.A;
        const double LocalDoubleArea = FVector::CrossProduct(EdgeA, EdgeB).Size();
        const double WorldDoubleArea = FVector::CrossProduct(EdgeA * Scale, EdgeB * Scale).Size();
        if (FMath::IsFinite(LocalDoubleArea) && LocalDoubleArea > 0.0
            && FMath::IsFinite(WorldDoubleArea) && WorldDoubleArea > 0.0)
            TriangleAreaScales[Index] = WorldDoubleArea / LocalDoubleArea;
    }
    bool bHasActiveArea = false;
    for (const FMD_ScrapeMeshUVCell& Cell : Data.Cells)
    {
        if (!Cell.IsActive()) continue;
        if (!TriangleAreaScales.IsValidIndex(Cell.TriangleIndex))
        {
            TriangleAreaScales.Empty();
            return false;
        }
        const double Weight = Cell.AreaCm2 * TriangleAreaScales[Cell.TriangleIndex] / ReferenceArea;
        if (!FMath::IsFinite(Weight) || Weight <= 0.0)
        {
            TriangleAreaScales.Empty();
            return false;
        }
        TotalWeight += Weight;
        bHasActiveArea = true;
    }
    if (!bHasActiveArea)
    {
        TriangleAreaScales.Empty();
        return false;
    }
    // The owner provides GC reachability; its public API exposes only immutable reads.
    SurfaceData.Reset(const_cast<UMD_ScrapeMeshUVData*>(&Data));
    CellData = Data.Cells.GetData();
    CellCount = Data.Cells.Num();
    LayoutId = Data.LayoutId;
    return true;
}

FMD_ScrapeCarrierWeightView FMD_ScrapeCarrierWeights::GetView() const
{
    if (!Dense.IsEmpty()) return FMD_ScrapeCarrierWeightView(Dense);
    if (!SurfaceData || CellData != SurfaceData->Cells.GetData()
        || CellCount != SurfaceData->Cells.Num() || LayoutId != SurfaceData->LayoutId) return {};
    FMD_ScrapeCarrierWeightView View;
    View.Cells = MakeArrayView(SurfaceData->Cells);
    View.TriangleAreaScales = MakeArrayView(TriangleAreaScales);
    View.ReferenceArea = ReferenceArea;
    return View;
}
