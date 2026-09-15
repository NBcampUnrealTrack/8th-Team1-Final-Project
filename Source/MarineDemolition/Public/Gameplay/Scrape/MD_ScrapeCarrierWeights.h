#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "UObject/StrongObjectPtr.h"

class FMD_ScrapeCarrierWeights;

/** Borrowed synchronous weight view. Dense arrays remain supported for Planar/tests.
 * Lazy views retain no per-atlas doubles; the owner and immutable profile must outlive use. */
class MARINEDEMOLITION_API FMD_ScrapeCarrierWeightView
{
public:
    FMD_ScrapeCarrierWeightView() = default;
    FMD_ScrapeCarrierWeightView(TConstArrayView<double> InDense) : Dense(InDense) {}
    FMD_ScrapeCarrierWeightView(TArrayView<double> InDense) : Dense(InDense) {}
    FMD_ScrapeCarrierWeightView(const TArray<double>& InDense) : Dense(InDense) {}
    FMD_ScrapeCarrierWeightView(const FMD_ScrapeCarrierWeights& Owner);
    int32 Num() const { return Cells.IsEmpty() ? Dense.Num() : Cells.Num(); }
    bool IsEmpty() const { return Num() == 0; }
    bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < Num(); }
    double operator[](const int32 Index) const
    {
        check(IsValidIndex(Index));
        if (Cells.IsEmpty()) return Dense[Index];
        const FMD_ScrapeMeshUVCell& Cell = Cells[Index];
        if (!Cell.IsActive()) return 0.0;
        // Preserve the original multiply, then divide, in this order and precision.
        return Cell.AreaCm2 * TriangleAreaScales[Cell.TriangleIndex] / ReferenceArea;
    }
    struct FIterator
    {
        const FMD_ScrapeCarrierWeightView* View;
        int32 Index;
        double operator*() const { return (*View)[Index]; }
        FIterator& operator++() { ++Index; return *this; }
        bool operator!=(const FIterator& Other) const { return Index != Other.Index; }
    };
    FIterator begin() const { return {this, 0}; }
    FIterator end() const { return {this, Num()}; }

private:
    friend class FMD_ScrapeCarrierWeights;
    TConstArrayView<double> Dense;
    TConstArrayView<FMD_ScrapeMeshUVCell> Cells;
    TConstArrayView<double> TriangleAreaScales;
    double ReferenceArea = 1.0;
};

/** Immutable per-exact-scale metric. Owns8bytes/triangle, never8bytes/atlas cell.
 * Strong profile ownership prevents dangling views when external consumers outlive a wall.
 * Profile mutation still requires InvalidateRuntimeCaches and consumer reinitialization. */
class MARINEDEMOLITION_API FMD_ScrapeCarrierWeights
{
public:
    FMD_ScrapeCarrierWeights() = default;
    /** Explicit dense compatibility input only; production initialization stays lazy. */
    explicit FMD_ScrapeCarrierWeights(TArray<double>&& InDense) : Dense(MoveTemp(InDense))
    {
        for (const double Weight : Dense) TotalWeight += Weight;
    }
    bool Initialize(const UMD_ScrapeMeshUVData& Data, const FTransform& Transform,
        double Deadline = TNumericLimits<double>::Max(), bool* OutPending = nullptr);
    FMD_ScrapeCarrierWeightView GetView() const;
    int32 Num() const { return GetView().Num(); }
    bool IsEmpty() const { return Num() == 0; }
    bool IsValidIndex(int32 Index) const { return GetView().IsValidIndex(Index); }
    double operator[](int32 Index) const { return GetView()[Index]; }
    double GetTotalWeight() const { return TotalWeight; }
    SIZE_T GetAllocatedSize() const { return sizeof(*this) + Dense.GetAllocatedSize() + TriangleAreaScales.GetAllocatedSize(); }

private:
    // An unpublished partial instance is owned only by the profile's preparation cache.
    const UMD_ScrapeMeshUVData* PreparingData = nullptr;
    FVector PreparingScale = FVector::ZeroVector;
    uint64 PreparingRevision = 0;
    int32 PreparingCellCount = 0, PreparingTriangleCount = 0;
    int32 NextTriangle = 0, NextCell = 0;
    bool bInitializing = false, bHasActiveArea = false;
    TStrongObjectPtr<UMD_ScrapeMeshUVData> SurfaceData;
    TArray<double> Dense;
    TArray<double> TriangleAreaScales;
    const FMD_ScrapeMeshUVCell* CellData = nullptr;
    int32 CellCount = 0;
    FGuid LayoutId;
    double ReferenceArea = 1.0;
    double TotalWeight = 0.0;
};

inline FMD_ScrapeCarrierWeightView::FMD_ScrapeCarrierWeightView(const FMD_ScrapeCarrierWeights& Owner)
{
    *this = Owner.GetView();
}
