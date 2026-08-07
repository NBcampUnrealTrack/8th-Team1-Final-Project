#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/**
 * 앞면과 뒷면이 공유하는 단일 오염 셀 배열의 인덱스 규칙.
 *
 * 한 면의 셀 수를 N이라고 할 때 앞면은 [0, N), 뒷면은 [N, 2N)을 사용한다.
 * 이 클래스는 셀 상태를 소유하지 않는 순수 계산 유틸리티다.
 */
class MARINEDEMOLITION_API FMD_ScrapeSurfaceLayout final
{
public:
	static constexpr int32 FaceCount = 2;

	/**
	 * uint16 복제 인덱스에서 MAX_uint16을 무효값으로 남기기 위한 총 셀 수 상한.
	 * 유효한 마지막 인덱스는 MaximumCombinedCellCount - 1이다.
	 */
	static constexpr int32 MaximumCombinedCellCount =
		static_cast<int32>(MAX_uint16);

	/** 격자 크기에서 한 면의 셀 수와 앞뒤를 합친 총 셀 수를 안전하게 계산한다. */
	static bool TryCalculateCellCounts(
		const FIntPoint& GridDimensions,
		int32& OutCellsPerFace,
		int32& OutTotalCellCount);

	/** 앞면은 0, 뒷면은 CellsPerFace를 반환한다. 잘못된 입력은 INDEX_NONE. */
	static int32 GetFaceCellOffset(
		EMD_ScrapeSurfaceFace Face,
		int32 CellsPerFace);

	/** 한 면 안의 로컬 셀 번호를 단일 배열의 통합 셀 번호로 바꾼다. */
	static int32 ToCombinedCellIndex(
		EMD_ScrapeSurfaceFace Face,
		int32 LocalCellIndex,
		int32 CellsPerFace);

	/** 단일 배열의 통합 셀 번호를 면과 면 내부 로컬 셀 번호로 되돌린다. */
	static bool TryDecodeCombinedCellIndex(
		int32 CombinedCellIndex,
		int32 CellsPerFace,
		EMD_ScrapeSurfaceFace& OutFace,
		int32& OutLocalCellIndex);

private:
	FMD_ScrapeSurfaceLayout() = delete;

	static bool IsSupportedFace(EMD_ScrapeSurfaceFace Face);
	static bool IsValidCellsPerFace(int32 CellsPerFace);
};
