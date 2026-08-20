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
	 * 셀 상태를 마스크 텍셀에 담을 불투명도 공간 신호(0~1)로 바꾼다.
	 *
	 * 예전에는 잔여 오염량을 그대로 담았다. 머티리얼이 불투명도를 만드는 구간은
	 * 값 범위의 맨 아래 꼬리(EdgeMin~EdgeMax)라서, 오염이 조금이라도 남은 셀은
	 * 전부 불투명해졌다. 그래서 보이는 경계가 "0인 셀 / 0이 아닌 셀"의 격자
	 * 경계에 딱 붙어, 비스듬한 헤라 자국이 셀 단위 계단으로 보였다.
	 *
	 * 이제 부분 피복 정보를 값의 중간 대역에 담는다. 텍셀 값이 그대로 불투명도가
	 * 되므로, 곧은 헤라 날 경계는 bilinear 보간이 곧은 등고선으로 복원한다.
	 * 대응하는 머티리얼 구간은 UMD_ScrapeSurfaceComponent가 MID로 지정한다.
	 *
	 * 얇게 남은 막(두께)과 부분 피복(면적)은 서로 다른 겉모습을 요구한다. 셀을
	 * 덮인 부분과 안 덮인 부분으로 나눠 면적으로 가중합하므로, 막이 남아 있으면
	 * 자국 전체가 진하게 보이고 막이 사라진 뒤에는 덮인 면적이 경계를 정한다.
	 * 두 정보가 한 값에서 뭉개지지 않게 CoveredFraction을 셀에 따로 저장한다.
	 *
	 * 반환값에는 머티리얼의 3차 SmoothStep 역함수를 미리 곱해 둔다. 그래야
	 * 최종 불투명도가 잔여량에 정비례해, 한 번 긁을 때마다 일정한 양이 지워지는
	 * 것으로 보인다.
	 *
	 * @param FilmOpacityGamma
	 * 막 두께 곡선의 감마. 1은 두께에 정비례한다. 낮추면 얇은 막이 더 진하게
	 * 보이지만 막이 남은 경계 셀에서 피복률 정보가 뭉개진다.
	 */
	static float CalculateVisualMaskValue(
		float RemainingContamination,
		float InitialContamination,
		float CoveredFraction,
		float FilmOpacityGamma);

	/**
	 * 마스크 셀의 값을 0~255로 양자화한다.
	 *
	 * 입력은 게임플레이 잔여량이 아니라 CalculateVisualMaskValue가 만든 불투명도
	 * 신호이고, 그 셀의 InitialContamination이 상한이 된다.
	 */
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
