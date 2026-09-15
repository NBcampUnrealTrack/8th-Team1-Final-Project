#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"

bool FMD_ScrapeCarrierWeights::Initialize(const UMD_ScrapeMeshUVData& Data, const FTransform& Transform,
    double Deadline, bool* OutPending)
{
    if (OutPending) *OutPending = false;
    const FVector Scale = Transform.GetScale3D();
    const double Area = FMath::Square(static_cast<double>(Data.GetCarrierReferenceCellSizeCm()));
    const auto Fail = [&]
    {
        bInitializing = false; PreparingData = nullptr;
        SurfaceData.Reset(); Dense.Empty(); TriangleAreaScales.Empty();
        CellData = nullptr; CellCount = 0; TotalWeight = 0.0;
        return false;
    };
    if (Data.GetCarrierReferenceCellSizeCm() <= 0.0 || Transform.ContainsNaN() || !FMath::IsFinite(Area) || Area <= UE_SMALL_NUMBER
        || FMath::Abs(Scale.X) <= UE_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER
        || FMath::Abs(Scale.Z) <= UE_SMALL_NUMBER) return Fail();
    if (!bInitializing || PreparingData != &Data || PreparingScale != Scale
        || PreparingRevision != Data.GetRuntimeCacheRevision() || LayoutId != Data.LayoutId || ReferenceArea != Area
        || PreparingCellCount != Data.Cells.Num() || PreparingTriangleCount != Data.Triangles.Num())
    {
        SurfaceData.Reset(); Dense.Empty(); TriangleAreaScales.Empty();
        CellData = nullptr; CellCount = 0; TotalWeight = 0.0; ReferenceArea = Area;
        PreparingData = &Data; PreparingScale = Scale; PreparingRevision = Data.GetRuntimeCacheRevision(); LayoutId = Data.LayoutId;
        PreparingCellCount = Data.Cells.Num(); PreparingTriangleCount = Data.Triangles.Num();
        NextTriangle = NextCell = 0; bHasActiveArea = false; bInitializing = true;
        TriangleAreaScales.Reserve(Data.Triangles.Num());
    }
    const auto Yield = [&]
    {
        if (OutPending) *OutPending = true;
        return false;
    };
    while (NextTriangle < Data.Triangles.Num())
    {
        const int32 End = FMath::Min(NextTriangle + 256, Data.Triangles.Num());
        TriangleAreaScales.AddZeroed(End - NextTriangle);
        for (; NextTriangle < End; ++NextTriangle)
        {
            const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[NextTriangle];
            const FVector EdgeA = Triangle.B - Triangle.A;
            const FVector EdgeB = Triangle.C - Triangle.A;
            const double LocalDoubleArea = FVector::CrossProduct(EdgeA, EdgeB).Size();
            const double WorldDoubleArea = FVector::CrossProduct(EdgeA * Scale, EdgeB * Scale).Size();
            if (FMath::IsFinite(LocalDoubleArea) && LocalDoubleArea > 0.0
                && FMath::IsFinite(WorldDoubleArea) && WorldDoubleArea > 0.0)
                TriangleAreaScales[NextTriangle] = WorldDoubleArea / LocalDoubleArea;
        }
        if (FPlatformTime::Seconds() >= Deadline) return Yield();
    }
    while (NextCell < Data.Cells.Num())
    {
        const int32 End = FMath::Min(NextCell + 4096, Data.Cells.Num());
        for (; NextCell < End; ++NextCell)
        {
            const FMD_ScrapeMeshUVCell& Cell = Data.Cells[NextCell];
            if (!Cell.IsActive()) continue;
            if (!TriangleAreaScales.IsValidIndex(Cell.TriangleIndex)) return Fail();
            const double Weight = Cell.AreaCm2 * TriangleAreaScales[Cell.TriangleIndex] / ReferenceArea;
            if (!FMath::IsFinite(Weight) || Weight <= 0.0) return Fail();
            TotalWeight += Weight;
            bHasActiveArea = true;
        }
        if (NextCell < Data.Cells.Num() && FPlatformTime::Seconds() >= Deadline) return Yield();
    }
    if (!bHasActiveArea) return Fail();
    SurfaceData.Reset(const_cast<UMD_ScrapeMeshUVData*>(&Data));
    CellData = Data.Cells.GetData(); CellCount = Data.Cells.Num(); LayoutId = Data.LayoutId;
    bInitializing = false; PreparingData = nullptr;
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
