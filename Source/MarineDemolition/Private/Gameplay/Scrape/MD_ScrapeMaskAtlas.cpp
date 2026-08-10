#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"

#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

bool FMD_ScrapeMaskAtlas::TryCalculateAtlasDimensions(
	const FIntPoint& GridDimensions,
	FIntPoint& OutAtlasDimensions)
{
	OutAtlasDimensions = FIntPoint::ZeroValue;

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			GridDimensions,
			CellsPerFace,
			TotalCellCount))
	{
		return false;
	}

	const int64 AtlasHeight64 =
		static_cast<int64>(GridDimensions.Y)
		* FMD_ScrapeSurfaceLayout::FaceCount;
	const int64 AtlasPixelCount64 =
		static_cast<int64>(GridDimensions.X) * AtlasHeight64;
	if (AtlasHeight64 <= 0
		|| AtlasHeight64 > MAX_int32
		|| AtlasPixelCount64 != TotalCellCount)
	{
		return false;
	}

	OutAtlasDimensions = FIntPoint(
		GridDimensions.X,
		static_cast<int32>(AtlasHeight64));
	return true;
}

bool FMD_ScrapeMaskAtlas::TryCalculateHalfTexelUV(
	const FIntPoint& AtlasDimensions,
	FVector2D& OutHalfTexelUV)
{
	OutHalfTexelUV = FVector2D::ZeroVector;
	if (AtlasDimensions.X <= 0 || AtlasDimensions.Y <= 0)
	{
		return false;
	}

	OutHalfTexelUV = FVector2D(
		0.5 / static_cast<double>(AtlasDimensions.X),
		0.5 / static_cast<double>(AtlasDimensions.Y));
	return FMath::IsFinite(OutHalfTexelUV.X)
		&& FMath::IsFinite(OutHalfTexelUV.Y);
}

bool FMD_ScrapeMaskAtlas::TryGetAtlasPixelCoordinate(
	const FIntPoint& GridDimensions,
	const EMD_ScrapeSurfaceFace Face,
	const int32 LocalCellIndex,
	FIntPoint& OutPixelCoordinate)
{
	OutPixelCoordinate = FIntPoint::NoneValue;

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			GridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			Face,
			LocalCellIndex,
			CellsPerFace) == INDEX_NONE)
	{
		return false;
	}

	const int32 FaceRowOffset = Face == EMD_ScrapeSurfaceFace::Back
		? GridDimensions.Y
		: 0;
	OutPixelCoordinate = FIntPoint(
		LocalCellIndex % GridDimensions.X,
		(LocalCellIndex / GridDimensions.X) + FaceRowOffset);
	return true;
}

bool FMD_ScrapeMaskAtlas::TryGetAtlasPixelCoordinateFromCombinedCellIndex(
	const FIntPoint& GridDimensions,
	const int32 CombinedCellIndex,
	FIntPoint& OutPixelCoordinate)
{
	OutPixelCoordinate = FIntPoint::NoneValue;

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			GridDimensions,
			CellsPerFace,
			TotalCellCount))
	{
		return false;
	}

	EMD_ScrapeSurfaceFace Face = EMD_ScrapeSurfaceFace::Front;
	int32 LocalCellIndex = INDEX_NONE;
	return FMD_ScrapeSurfaceLayout::TryDecodeCombinedCellIndex(
			CombinedCellIndex,
			CellsPerFace,
			Face,
			LocalCellIndex)
		&& TryGetAtlasPixelCoordinate(
			GridDimensions,
			Face,
			LocalCellIndex,
			OutPixelCoordinate);
}

bool FMD_ScrapeMaskAtlas::TryGetAtlasPixelIndexFromCombinedCellIndex(
	const FIntPoint& GridDimensions,
	const int32 CombinedCellIndex,
	int32& OutPixelIndex)
{
	OutPixelIndex = INDEX_NONE;

	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	FIntPoint PixelCoordinate = FIntPoint::NoneValue;
	if (!TryCalculateAtlasDimensions(
			GridDimensions,
			AtlasDimensions)
		|| !TryGetAtlasPixelCoordinateFromCombinedCellIndex(
			GridDimensions,
			CombinedCellIndex,
			PixelCoordinate))
	{
		return false;
	}

	OutPixelIndex = PixelCoordinate.X
		+ (PixelCoordinate.Y * AtlasDimensions.X);
	return OutPixelIndex >= 0
		&& OutPixelIndex < AtlasDimensions.X * AtlasDimensions.Y;
}

float FMD_ScrapeMaskAtlas::CalculateRuntimeAwareVisualContamination(
	const float RemainingContamination,
	const float InitialContamination,
	const float GeneratedInitialContamination)
{
	if (!FMath::IsFinite(RemainingContamination)
		|| !FMath::IsFinite(InitialContamination))
	{
		return 0.0f;
	}

	const float SafeInitial = FMath::Clamp(
		InitialContamination,
		0.0f,
		1.0f);
	const float SafeRemaining = FMath::Clamp(
		RemainingContamination,
		0.0f,
		SafeInitial);
	const float SafeGeneratedInitial =
		FMath::IsFinite(GeneratedInitialContamination)
		? FMath::Clamp(GeneratedInitialContamination, 0.0f, 1.0f)
		: SafeInitial;

	if (SafeInitial > SafeGeneratedInitial + UE_SMALL_NUMBER
		&& SafeInitial > UE_SMALL_NUMBER)
	{
		return FMath::Clamp(
			SafeRemaining / SafeInitial,
			0.0f,
			1.0f);
	}

	return SafeRemaining;
}

bool FMD_ScrapeMaskAtlas::TryQuantizeRemainingContamination(
	const float RemainingContamination,
	const float InitialContamination,
	uint8& OutMaskValue)
{
	OutMaskValue = 0;
	if (!FMath::IsFinite(RemainingContamination)
		|| !FMath::IsFinite(InitialContamination))
	{
		return false;
	}

	const float SafeInitial = FMath::Clamp(
		InitialContamination,
		0.0f,
		1.0f);
	if (SafeInitial <= UE_SMALL_NUMBER)
	{
		return true;
	}

	const float SafeRemaining = FMath::Clamp(
		RemainingContamination,
		0.0f,
		SafeInitial);
	OutMaskValue = static_cast<uint8>(FMath::RoundToInt(
		SafeRemaining * static_cast<float>(MAX_uint8)));
	return true;
}

bool FMD_ScrapeMaskAtlas::TryBuildAtlasBuffer(
	const FIntPoint& GridDimensions,
	const TConstArrayView<FMD_ScrapeSurfaceCell> Cells,
	TArray<uint8>& OutAtlasPixels)
{
	OutAtlasPixels.Reset();

	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!TryCalculateAtlasDimensions(
			GridDimensions,
			AtlasDimensions)
		|| !FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			GridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| Cells.Num() != TotalCellCount)
	{
		return false;
	}

	OutAtlasPixels.SetNumUninitialized(TotalCellCount);
	for (int32 CombinedCellIndex = 0;
		CombinedCellIndex < TotalCellCount;
		++CombinedCellIndex)
	{
		int32 PixelIndex = INDEX_NONE;
		uint8 MaskValue = 0;
		const FMD_ScrapeSurfaceCell& Cell = Cells[CombinedCellIndex];
		if (!TryGetAtlasPixelIndexFromCombinedCellIndex(
				GridDimensions,
				CombinedCellIndex,
				PixelIndex)
			|| !TryQuantizeRemainingContamination(
				Cell.RemainingContamination,
				Cell.InitialContamination,
				MaskValue)
			|| !OutAtlasPixels.IsValidIndex(PixelIndex))
		{
			OutAtlasPixels.Reset();
			return false;
		}

		OutAtlasPixels[PixelIndex] = MaskValue;
	}

	return OutAtlasPixels.Num()
		== AtlasDimensions.X * AtlasDimensions.Y;
}

bool FMD_ScrapeMaskAtlas::TryUpdateAtlasPixel(
	const FIntPoint& GridDimensions,
	const int32 CombinedCellIndex,
	const FMD_ScrapeSurfaceCell& Cell,
	const TArrayView<uint8> InOutAtlasPixels,
	FIntPoint& OutPixelCoordinate)
{
	OutPixelCoordinate = FIntPoint::NoneValue;

	FIntPoint AtlasDimensions = FIntPoint::ZeroValue;
	int32 PixelIndex = INDEX_NONE;
	uint8 MaskValue = 0;
	if (!TryCalculateAtlasDimensions(
			GridDimensions,
			AtlasDimensions)
		|| InOutAtlasPixels.Num()
			!= AtlasDimensions.X * AtlasDimensions.Y
		|| !TryGetAtlasPixelCoordinateFromCombinedCellIndex(
			GridDimensions,
			CombinedCellIndex,
			OutPixelCoordinate)
		|| !TryGetAtlasPixelIndexFromCombinedCellIndex(
			GridDimensions,
			CombinedCellIndex,
			PixelIndex)
		|| !TryQuantizeRemainingContamination(
			Cell.RemainingContamination,
			Cell.InitialContamination,
			MaskValue)
		|| !InOutAtlasPixels.IsValidIndex(PixelIndex))
	{
		OutPixelCoordinate = FIntPoint::NoneValue;
		return false;
	}

	InOutAtlasPixels[PixelIndex] = MaskValue;
	return true;
}

FIntRect FMD_ScrapeMaskAtlas::MakeEmptyAtlasRegion()
{
	// 첫 ExpandAtlasRegion 한 번으로 곧바로 그 픽셀만 담게 되는 뒤집힌 사각형.
	return FIntRect(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
}

bool FMD_ScrapeMaskAtlas::IsAtlasRegionEmpty(const FIntRect& AtlasRegion)
{
	return AtlasRegion.Min.X >= AtlasRegion.Max.X
		|| AtlasRegion.Min.Y >= AtlasRegion.Max.Y;
}

void FMD_ScrapeMaskAtlas::ExpandAtlasRegion(
	FIntRect& InOutAtlasRegion,
	const FIntPoint& PixelCoordinate)
{
	InOutAtlasRegion.Min.X = FMath::Min(
		InOutAtlasRegion.Min.X,
		PixelCoordinate.X);
	InOutAtlasRegion.Min.Y = FMath::Min(
		InOutAtlasRegion.Min.Y,
		PixelCoordinate.Y);
	InOutAtlasRegion.Max.X = FMath::Max(
		InOutAtlasRegion.Max.X,
		PixelCoordinate.X + 1);
	InOutAtlasRegion.Max.Y = FMath::Max(
		InOutAtlasRegion.Max.Y,
		PixelCoordinate.Y + 1);
}

bool FMD_ScrapeMaskAtlas::TryPackAtlasRegion(
	const FIntPoint& AtlasDimensions,
	const TConstArrayView<uint8> AtlasPixels,
	const FIntRect& AtlasRegion,
	const TArrayView<uint8> OutPackedPixels)
{
	const int32 RegionWidth = AtlasRegion.Width();
	const int32 RegionHeight = AtlasRegion.Height();
	if (AtlasDimensions.X <= 0
		|| AtlasDimensions.Y <= 0
		|| AtlasPixels.Num() != AtlasDimensions.X * AtlasDimensions.Y
		|| RegionWidth <= 0
		|| RegionHeight <= 0
		|| AtlasRegion.Min.X < 0
		|| AtlasRegion.Min.Y < 0
		|| AtlasRegion.Max.X > AtlasDimensions.X
		|| AtlasRegion.Max.Y > AtlasDimensions.Y
		|| OutPackedPixels.Num() != RegionWidth * RegionHeight)
	{
		return false;
	}

	for (int32 RowIndex = 0; RowIndex < RegionHeight; ++RowIndex)
	{
		const int32 SourceRowStart =
			((AtlasRegion.Min.Y + RowIndex) * AtlasDimensions.X)
			+ AtlasRegion.Min.X;
		FMemory::Memcpy(
			OutPackedPixels.GetData() + (RowIndex * RegionWidth),
			AtlasPixels.GetData() + SourceRowStart,
			RegionWidth);
	}
	return true;
}
