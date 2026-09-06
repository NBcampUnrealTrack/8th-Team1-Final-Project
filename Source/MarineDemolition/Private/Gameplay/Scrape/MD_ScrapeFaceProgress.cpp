#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"

#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

bool FMD_ScrapeFaceProgress::IsSurfaceComplete(
	const bool bFrontFullyClean,
	const bool bBackFullyClean,
	const bool bSingleLogicalSurface)
{
	return bFrontFullyClean || (!bSingleLogicalSurface && bBackFullyClean);
}

bool FMD_ScrapeFaceProgress::BuildMeshUVCellCarrierWeights(
    const UMD_ScrapeMeshUVData& SurfaceData, const FTransform& SurfaceTransform,
    TArray<double>& OutWeights)
{
    OutWeights.Reset();
    const double ReferenceArea = FMath::Square(static_cast<double>(SurfaceData.TargetCellSizeCm));
    const FVector Scale = SurfaceTransform.GetScale3D();
    if (SurfaceTransform.ContainsNaN() || !FMath::IsFinite(ReferenceArea) || ReferenceArea <= UE_SMALL_NUMBER
        || FMath::Abs(Scale.X) <= UE_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER
        || FMath::Abs(Scale.Z) <= UE_SMALL_NUMBER) { return false; }
    TArray<double> TriangleAreaScales;
    TriangleAreaScales.SetNumZeroed(SurfaceData.Triangles.Num());
    for (int32 Index = 0; Index < SurfaceData.Triangles.Num(); ++Index)
    {
        const FMD_ScrapeMeshUVTriangle& Triangle = SurfaceData.Triangles[Index];
        const FVector EdgeA = Triangle.B - Triangle.A;
        const FVector EdgeB = Triangle.C - Triangle.A;
        const double LocalDoubleArea = FVector::CrossProduct(EdgeA, EdgeB).Size();
        const double WorldDoubleArea = FVector::CrossProduct(
            SurfaceTransform.TransformVector(EdgeA), SurfaceTransform.TransformVector(EdgeB)).Size();
        if (FMath::IsFinite(LocalDoubleArea) && LocalDoubleArea > 0.0
            && FMath::IsFinite(WorldDoubleArea) && WorldDoubleArea > 0.0)
        { TriangleAreaScales[Index] = WorldDoubleArea / LocalDoubleArea; }
    }
    OutWeights.SetNumZeroed(SurfaceData.Cells.Num());
    bool bHasActiveArea = false;
    for (int32 Index = 0; Index < SurfaceData.Cells.Num(); ++Index)
    {
        const FMD_ScrapeMeshUVCell& Cell = SurfaceData.Cells[Index];
        if (!Cell.IsActive()) { continue; }
        if (!TriangleAreaScales.IsValidIndex(Cell.TriangleIndex)) { OutWeights.Reset(); return false; }
        const double Weight = Cell.AreaCm2 * TriangleAreaScales[Cell.TriangleIndex] / ReferenceArea;
        if (!FMath::IsFinite(Weight) || Weight <= 0.0) { OutWeights.Reset(); return false; }
        OutWeights[Index] = Weight; bHasActiveArea = true;
    }
    return bHasActiveArea;
}

bool FMD_ScrapeFaceProgress::CalculateWeightedCarrierAmounts(
    const TConstArrayView<FMD_ScrapeSurfaceCell> Cells, const TConstArrayView<double> Weights,
    double& OutInitial, double& OutBase, double& OutTransferred)
{
    OutInitial = OutBase = OutTransferred = 0.0;
    if (Cells.Num() != Weights.Num()) { return false; }
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
    {
        const double Weight = Weights[Index];
        if (!FMath::IsFinite(Weight) || Weight < 0.0) { return false; }
        if (Weight == 0.0) { continue; }
        FMD_ScrapeSurfaceCell Cell = Cells[Index]; Cell.Normalize();
        OutInitial += Weight * static_cast<double>(Cell.InitialContamination);
        OutBase += Weight * static_cast<double>(Cell.RemainingContamination);
        OutTransferred += Weight * static_cast<double>(Cell.TransferredContamination);
    }
    return FMath::IsFinite(OutInitial) && FMath::IsFinite(OutBase) && FMath::IsFinite(OutTransferred);
}

float FMD_ScrapeFaceProgress::CalculateAreaWeightedCleanRatio(
    const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
    const UMD_ScrapeMeshUVData& SurfaceData, const FTransform& SurfaceTransform)
{
    TArray<double> Weights;
    double Initial = 0.0, Base = 0.0, Transferred = 0.0;
    return BuildMeshUVCellCarrierWeights(SurfaceData, SurfaceTransform, Weights)
        && CalculateWeightedCarrierAmounts(SurfaceCells, Weights, Initial, Base, Transferred)
        ? FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(Initial, Base + Transferred) : 0.0f;
}

int32 FMD_ScrapeFaceProgress::CalculateCleanPieceCount(
	const int32 TotalPieceCount,
	const float OverallCleanRatio)
{
	if (TotalPieceCount <= 0)
	{
		return 0;
	}

	// 제거율은 float로 생성되므로 조각 수 곱셈도 같은 정밀도에서 반올림한다.
	// double로 승격하면 0.35f가 0.349999...로 보존돼 3.5가 3으로 내려갈 수 있다.
	const int32 CleanPieceCount = FMath::RoundToInt(
		static_cast<float>(TotalPieceCount)
		* SanitizeCleanRatio(OverallCleanRatio));
	return FMath::Clamp(
		CleanPieceCount,
		0,
		TotalPieceCount);
}

float FMD_ScrapeFaceProgress::SanitizeCleanRatio(const float CleanRatio)
{
	return FMath::IsFinite(CleanRatio)
		? FMath::Clamp(CleanRatio, 0.0f, 1.0f)
		: 0.0f;
}
