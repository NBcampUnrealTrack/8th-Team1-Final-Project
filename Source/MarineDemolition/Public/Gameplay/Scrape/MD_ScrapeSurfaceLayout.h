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

	/** 기존 해석적 표면의 자동 해상도와 저장 주소를 유지하기 위한 상한. */
	static constexpr int32 LegacyMaximumCombinedCellCount =
		static_cast<int32>(MAX_uint16);

	/**
	 * 1024 x 1024 두 슬롯까지 허용하는 메모리 상한. uint32 전체 범위를
	 * 할당하지 않으며, 새 메시 UV가 이 한계를 넘으면 초기화를 거부한다.
	 */
	static constexpr int32 MaximumCombinedCellCount = 2 * 1024 * 1024;

	/** 저장 주소의 버전/식별자/격자 계약. 버전 0의 빈 격자만 legacy로 허용한다. */
	static bool IsCarrierScaleValid(const FVector& Scale);
	static bool AreCarrierScalesCompatible(const FVector& SavedScale, const FVector& CurrentScale);

	static bool IsLayoutMetadataValid(
		int32 LayoutVersion, const FGuid& LayoutId, const FIntPoint& GridDimensions);

	/** 옛 평면 기록은 평면에만, MeshUV 기록은 같은 bake 주소에만 복원한다. */
	static bool IsSavedLayoutCompatible(
		int32 SavedVersion, const FGuid& SavedId, const FIntPoint& SavedGrid,
		int32 CurrentVersion, const FGuid& CurrentId, const FIntPoint& CurrentGrid);

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
