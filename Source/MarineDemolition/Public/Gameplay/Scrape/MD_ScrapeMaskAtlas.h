#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/**
 * 앞면과 뒷면 오염 셀을 W x 2H G8 마스크로 바꾸는 순수 계산 유틸리티.
 *
 * 앞면은 아틀라스 위쪽 [0, H), 뒷면은 아래쪽 [H, 2H)에 배치한다.
 * UObject나 렌더러에 의존하지 않아 셀 매핑과 양자화를 자동화 테스트에서 검증할 수 있다.
 */
class MARINEDEMOLITION_API FMD_ScrapeMaskAtlas final
{
public:
	/** 격자 크기 W x H에서 G8 아틀라스 크기 W x 2H를 안전하게 계산한다. */
	static bool TryCalculateAtlasDimensions(
		const FIntPoint& GridDimensions,
		FIntPoint& OutAtlasDimensions);

	/** Bilinear 샘플이 픽셀 경계를 넘지 않도록 아틀라스 한 픽셀의 절반 UV를 계산한다. */
	static bool TryCalculateHalfTexelUV(
		const FIntPoint& AtlasDimensions,
		FVector2D& OutHalfTexelUV);

	/** 면 내부 셀 번호를 아틀라스 픽셀 좌표로 바꾼다. */
	static bool TryGetAtlasPixelCoordinate(
		const FIntPoint& GridDimensions,
		EMD_ScrapeSurfaceFace Face,
		int32 LocalCellIndex,
		FIntPoint& OutPixelCoordinate);

	/** 앞뒤 통합 셀 번호를 아틀라스 픽셀 좌표로 바꾼다. */
	static bool TryGetAtlasPixelCoordinateFromCombinedCellIndex(
		const FIntPoint& GridDimensions,
		int32 CombinedCellIndex,
		FIntPoint& OutPixelCoordinate);

	/** 앞뒤 통합 셀 번호를 행 우선 G8 버퍼 번호로 바꾼다. */
	static bool TryGetAtlasPixelIndexFromCombinedCellIndex(
		const FIntPoint& GridDimensions,
		int32 CombinedCellIndex,
		int32& OutPixelIndex);

	/**
	 * 생성 당시보다 Initial이 커진 런타임 오염은 자기 최대량을 기준으로 표시한다.
	 * 기존 오염은 이전과 같이 Remaining 절대값을 그대로 반환한다.
	 */
	static float CalculateRuntimeAwareVisualContamination(
		float RemainingContamination,
		float InitialContamination,
		float GeneratedInitialContamination);

	/** 셀의 현재 잔여 오염량을 0~255로 양자화한다. */
	static bool TryQuantizeRemainingContamination(
		float RemainingContamination,
		float InitialContamination,
		uint8& OutMaskValue);

	/** 2N개 셀 전체에서 W x 2H 행 우선 G8 버퍼를 만든다. */
	static bool TryBuildAtlasBuffer(
		const FIntPoint& GridDimensions,
		TConstArrayView<FMD_ScrapeSurfaceCell> Cells,
		TArray<uint8>& OutAtlasPixels);

	/** 통합 셀 하나의 현재 값을 기존 G8 버퍼에 반영한다. */
	static bool TryUpdateAtlasPixel(
		const FIntPoint& GridDimensions,
		int32 CombinedCellIndex,
		const FMD_ScrapeSurfaceCell& Cell,
		TArrayView<uint8> InOutAtlasPixels,
		FIntPoint& OutPixelCoordinate);

	/** 변경 픽셀이 아직 하나도 없는 상태를 뜻하는 사각형을 만든다. */
	static FIntRect MakeEmptyAtlasRegion();

	/** 사각형이 픽셀을 하나도 담고 있지 않은지 확인한다. */
	static bool IsAtlasRegionEmpty(const FIntRect& AtlasRegion);

	/** 픽셀 좌표 하나를 담도록 사각형을 넓힌다. Min은 포함, Max는 제외한다. */
	static void ExpandAtlasRegion(
		FIntRect& InOutAtlasRegion,
		const FIntPoint& PixelCoordinate);

	/**
	 * 아틀라스에서 사각형에 걸친 행만 빈틈없이 모아 GPU 업로드용 버퍼를 만든다.
	 *
	 * 결과는 행 우선이고 한 행의 길이가 사각형 너비와 같으므로,
	 * 업로드 시 SrcPitch로 사각형 너비를 그대로 넘기면 된다.
	 */
	static bool TryPackAtlasRegion(
		const FIntPoint& AtlasDimensions,
		TConstArrayView<uint8> AtlasPixels,
		const FIntRect& AtlasRegion,
		TArrayView<uint8> OutPackedPixels);

private:
	FMD_ScrapeMaskAtlas() = delete;
};
