#include "Gameplay/Scrape/MD_ScrapeMaskAtlas.h"

#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

namespace
{
	/**
	 * 초기 오염량이 이 비율 이상이면 완전히 진하게 보인다.
	 *
	 * 마스크가 잔여 오염량을 그대로 담던 시절 M_Contam_Decal이 쓰던 EdgeMax와
	 * 같은 값이다. 프로시저럴 오염 무늬의 옅은 테두리 표현을 그대로 유지한다.
	 */
	constexpr float PatternSolidFraction = 0.12f;

	/**
	 * 막 두께 곡선의 감마 하한.
	 *
	 * 1보다 작게 하면 얇게 남은 막이 더 진하게 보이지만, 막이 남은 경계 셀에서
	 * 곡선이 급해져 피복률 정보가 뭉개진다. 0.6까지 내리면 자국 경계의 직선
	 * 오차가 0.05cm에서 3.5cm로 무너지므로 여기서 끊는다.
	 */
	constexpr float MinimumFilmGamma = 0.75f;

	/**
	 * 머티리얼의 3차 SmoothStep을 미리 상쇄한다.
	 *
	 * 머티리얼은 마스크를 SmoothStep(EdgeMin, EdgeMax, x)로 불투명도로 바꾼다.
	 * 그대로 두면 한 번 긁을 때마다 줄어드는 잔량이 불투명도에서는 3차로
	 * 휘어서, 첫 통과는 거의 그대로이고 두 번째에 갑자기 사라지는 것처럼 보인다.
	 * 역함수를 미리 곱해 두면 불투명도가 잔량에 정비례해, 긁은 횟수만큼 일정하게
	 * 지워지는 것으로 보인다. 등고선도 선형에 가까워져 경계가 더 곧아진다.
	 */
	float ApplyInverseSmoothStep(const float Value)
	{
		const float SafeValue = FMath::Clamp(Value, 0.0f, 1.0f);
		return 0.5f - FMath::Sin(
			FMath::Asin(1.0f - 2.0f * SafeValue) / 3.0f);
	}
}

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

float FMD_ScrapeMaskAtlas::CalculateVisualMaskValue(
	const float RemainingContamination,
	const float InitialContamination,
	const float CoveredFraction,
	const float FilmOpacityGamma)
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
	if (SafeInitial <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float SafeRemaining = FMath::Clamp(
		RemainingContamination,
		0.0f,
		SafeInitial);
	const float FilmGamma = FMath::IsFinite(FilmOpacityGamma)
		? FMath::Clamp(FilmOpacityGamma, MinimumFilmGamma, 1.0f)
		: 1.0f;

	// 초기 오염량이 얇은 셀의 반투명 표현. 오염 무늬의 옅은 테두리가 이 항으로
	// 남으므로, 아직 긁지 않은 표면의 겉모습은 이 변경 전후가 같다.
	const float AmountOpacity = FMath::SmoothStep(
		0.0f,
		PatternSolidFraction,
		SafeInitial);

	// 셀을 덮인 부분과 안 덮인 부분으로 나눠 면적으로 가중합한다. 두 항을 max로
	// 합치면 안 된다. 경계 셀의 잔여량은 곧 부분 피복 하한이라 막 항과 면적 항이
	// 같은 정보를 담게 되고, 훨씬 급한 막 항이 항상 이겨서 보이는 경계가 다시
	// 피복률 88~100% 구간으로 밀린다. 그러면 계단이 그대로 돌아온다.
	const float SafeCovered = FMath::IsFinite(CoveredFraction)
		? FMath::Clamp(CoveredFraction, 0.0f, 1.0f)
		: 0.0f;
	float EdgeOpacity = 1.0f;
	if (SafeCovered > UE_SMALL_NUMBER)
	{
		// 안 덮인 부분은 원래 오염이 그대로 남아 있으므로 불투명도 1이다.
		// 덮인 부분에는 두께 CoveredRemainingFraction의 막만 남는다. 잔여량은
		// 두 영역의 합이므로 여기서 다시 분해한다.
		const float UncoveredFraction = 1.0f - SafeCovered;
		const float CoveredRemainingFraction = FMath::Clamp(
			(SafeRemaining / SafeInitial - UncoveredFraction) / SafeCovered,
			0.0f,
			1.0f);

		// 막 항은 두께에 정비례하는 것이 기본이다. 급하게 만들면 막이 남은
		// 경계 셀에서 항이 포화되어 피복률과 무관하게 완전 불투명이 되고,
		// 그 쪽 경계가 셀 단위 0/1 톱니로 돌아간다.
		EdgeOpacity = UncoveredFraction
			+ SafeCovered * (FMath::IsNearlyEqual(FilmGamma, 1.0f)
				? CoveredRemainingFraction
				: FMath::Pow(CoveredRemainingFraction, FilmGamma));
	}

	// 작은 값을 쓰면 얇은 오염 무늬는 무늬대로 남는다. 곱하지 않는 이유는 초기
	// 오염량이 다른 셀들 사이에서 경계 등고선이 무늬 밀도를 따라 흔들리지 않게
	// 하기 위한 것이다.
	return ApplyInverseSmoothStep(
		FMath::Clamp(FMath::Min(AmountOpacity, EdgeOpacity), 0.0f, 1.0f));
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
