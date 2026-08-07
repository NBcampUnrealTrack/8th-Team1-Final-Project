#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

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
