#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"

struct FMD_ScrapeSurfaceCell;
class UMD_ScrapeMeshUVData;

/** 오염 제거율을 벽 조각 개수로 변환하는 순수 계산 유틸리티. */
class MARINEDEMOLITION_API FMD_ScrapeFaceProgress final
{
public:
	/** 단일 메시 UV는 Front만, 기존 양면 표면은 어느 한 면 완료 규칙을 쓴다. */
	static bool IsSurfaceComplete(
		bool bFrontFullyClean,
		bool bBackFullyClean,
		bool bSingleLogicalSurface);

	/** Density stays 0..1; carrier weight is world area / target cell area. Inactive weights are zero. */
	static bool BuildMeshUVCellCarrierWeights(const UMD_ScrapeMeshUVData& SurfaceData,
		const FTransform& SurfaceTransform, TArray<double>& OutWeights);
	static bool CalculateWeightedCarrierAmounts(FMD_ScrapeConstCellView Cells,
		FMD_ScrapeCarrierWeightView Weights, double& OutInitial, double& OutBase, double& OutTransferred);

	/** Clean progress uses the same physical carrier measure as removal/recovery. */
	static float CalculateAreaWeightedCleanRatio(
		FMD_ScrapeConstCellView SurfaceCells,
		const UMD_ScrapeMeshUVData& SurfaceData,
		const FTransform& SurfaceTransform);

	/** 전체 제거율에 맞춰 깨끗한 벽 조각 수를 반올림해 계산한다. */
	static int32 CalculateCleanPieceCount(
		int32 TotalPieceCount,
		float OverallCleanRatio);

private:
	FMD_ScrapeFaceProgress() = delete;

	static float SanitizeCleanRatio(float CleanRatio);
};
