#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

bool FMD_ScrapeSurfaceLayout::IsLayoutMetadataValid(
	const int32 LayoutVersion, const FGuid& LayoutId, const FIntPoint& GridDimensions)
{
	if (LayoutVersion < 0 || LayoutVersion > 1
		|| (LayoutVersion == 0 && LayoutId.IsValid())
		|| (LayoutVersion == 1 && !LayoutId.IsValid()))
	{
		return false;
	}
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	return TryCalculateCellCounts(GridDimensions, CellsPerFace, TotalCellCount);
}

bool FMD_ScrapeSurfaceLayout::IsSavedLayoutCompatible(
	const int32 SavedVersion, const FGuid& SavedId, const FIntPoint& SavedGrid,
	const int32 CurrentVersion, const FGuid& CurrentId, const FIntPoint& CurrentGrid)
{
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	return IsLayoutMetadataValid(SavedVersion, SavedId, SavedGrid)
		&& IsLayoutMetadataValid(CurrentVersion, CurrentId, CurrentGrid)
		&& TryCalculateCellCounts(CurrentGrid, CellsPerFace, TotalCellCount)
		&& SavedVersion == CurrentVersion
		&& SavedId == CurrentId
		// 저장 당시 격자 크기와 현재 크기가 일치하는 경우만 허용
		&& SavedGrid == CurrentGrid;
}

bool FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
	const FIntPoint& GridDimensions,
	int32& OutCellsPerFace,
	int32& OutTotalCellCount)
{
	OutCellsPerFace = 0;
	OutTotalCellCount = 0;

	if (GridDimensions.X <= 0 || GridDimensions.Y <= 0)
	{
		return false;
	}

	// 잘못된 큰 입력도 int32 곱셈 오버플로를 일으키지 않도록 먼저 int64로 계산한다.
	const int64 CellsPerFace64 =
		static_cast<int64>(GridDimensions.X)
		* static_cast<int64>(GridDimensions.Y);
	const int64 TotalCellCount64 = CellsPerFace64 * FaceCount;
	if (CellsPerFace64 <= 0
		|| TotalCellCount64 > MaximumCombinedCellCount)
	{
		return false;
	}

	OutCellsPerFace = static_cast<int32>(CellsPerFace64);
	OutTotalCellCount = static_cast<int32>(TotalCellCount64);
	return true;
}

int32 FMD_ScrapeSurfaceLayout::GetFaceCellOffset(
	const EMD_ScrapeSurfaceFace Face,
	const int32 CellsPerFace)
{
	if (!IsSupportedFace(Face) || !IsValidCellsPerFace(CellsPerFace))
	{
		return INDEX_NONE;
	}

	return Face == EMD_ScrapeSurfaceFace::Back
		? CellsPerFace
		: 0;
}

int32 FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
	const EMD_ScrapeSurfaceFace Face,
	const int32 LocalCellIndex,
	const int32 CellsPerFace)
{
	const int32 FaceOffset = GetFaceCellOffset(Face, CellsPerFace);
	if (FaceOffset == INDEX_NONE
		|| LocalCellIndex < 0
		|| LocalCellIndex >= CellsPerFace)
	{
		return INDEX_NONE;
	}

	return FaceOffset + LocalCellIndex;
}

bool FMD_ScrapeSurfaceLayout::TryDecodeCombinedCellIndex(
	const int32 CombinedCellIndex,
	const int32 CellsPerFace,
	EMD_ScrapeSurfaceFace& OutFace,
	int32& OutLocalCellIndex)
{
	OutFace = EMD_ScrapeSurfaceFace::Front;
	OutLocalCellIndex = INDEX_NONE;

	if (!IsValidCellsPerFace(CellsPerFace))
	{
		return false;
	}

	const int32 TotalCellCount = CellsPerFace * FaceCount;
	if (CombinedCellIndex < 0 || CombinedCellIndex >= TotalCellCount)
	{
		return false;
	}

	if (CombinedCellIndex < CellsPerFace)
	{
		OutLocalCellIndex = CombinedCellIndex;
		return true;
	}

	OutFace = EMD_ScrapeSurfaceFace::Back;
	OutLocalCellIndex = CombinedCellIndex - CellsPerFace;
	return true;
}

bool FMD_ScrapeSurfaceLayout::IsSupportedFace(
	const EMD_ScrapeSurfaceFace Face)
{
	return Face == EMD_ScrapeSurfaceFace::Front
		|| Face == EMD_ScrapeSurfaceFace::Back;
}

bool FMD_ScrapeSurfaceLayout::IsValidCellsPerFace(
	const int32 CellsPerFace)
{
	return CellsPerFace > 0
		&& static_cast<int64>(CellsPerFace) * FaceCount
			<= MaximumCombinedCellCount;
}


bool FMD_ScrapeSurfaceLayout::IsCarrierScaleValid(const FVector& Scale)
{
    return !Scale.ContainsNaN() && FMath::Abs(Scale.X) > UE_SMALL_NUMBER
        && FMath::Abs(Scale.Y) > UE_SMALL_NUMBER && FMath::Abs(Scale.Z) > UE_SMALL_NUMBER;
}

bool FMD_ScrapeSurfaceLayout::AreCarrierScalesCompatible(const FVector& SavedScale, const FVector& CurrentScale)
{
    return IsCarrierScaleValid(SavedScale) && IsCarrierScaleValid(CurrentScale)
        && SavedScale.Equals(CurrentScale, 1.0e-4);
}
