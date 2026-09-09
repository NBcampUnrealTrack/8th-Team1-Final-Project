#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("MD Scrape"), STATGROUP_MD_Scrape, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Stroke Total"), STAT_MD_ScrapeStrokeTotal,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Evaluate"), STAT_MD_ScrapeEvaluate,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Clean Ratio"), STAT_MD_ScrapeCleanRatio,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("G8 Mask Update"), STAT_MD_ScrapeMaskUpdate,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Replication Publish"), STAT_MD_ScrapePublish,
	STATGROUP_MD_Scrape, MARINEDEMOLITION_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Fully Removed Check"),
	STAT_MD_ScrapeFullyRemovedCheck, STATGROUP_MD_Scrape,
	MARINEDEMOLITION_API);

/** Names shared by the component implementation files. */
namespace MD_ScrapeSurfaceComponentInternal
{
	extern const FName ContaminationMaskTextureParameterName;
	extern const FName TransferredPaintMaskTextureParameterName;
	extern const FName ContaminationBaseColorTextureParameterName;
	extern const FName ContaminationShapeMaskTextureParameterName;
	extern const FName ContaminationBaseCoverageParameterName;
	extern const FName ContaminationOpacityParameterName;

	/**
	 * 마스크 값을 불투명도로 바꾸는 구간과 그 위에 얹는 노이즈.
	 *
	 * 마스크 텍셀은 잔여 오염량이 아니라 0~1 불투명도 신호이므로, 이 구간은
	 * 머티리얼 에셋 기본값이 아니라 컴포넌트가 정한다. 두 쪽이 서로 다른 규약을
	 * 쓰면 부분 피복 대역이 구간 밖으로 밀려 자국 경계가 격자 계단으로 돌아간다.
	 */
	extern const FName ContaminationEdgeMinParameterName;
	extern const FName ContaminationEdgeMaxParameterName;
	extern const FName ContaminationEdgeNoiseStrengthParameterName;
	extern const FName ContaminationVisualMaskGammaParameterName;
	extern const FName ContaminationMaskVScaleParameterName;
	extern const FName ContaminationMaskVOffsetParameterName;
	extern const FName ContaminationMaskUFlipParameterName;
	extern const FName ContaminationMaskHalfTexelUParameterName;
	extern const FName ContaminationMaskHalfTexelVParameterName;
	extern const FName ContaminationPatchUVScaleUParameterName;
	extern const FName ContaminationPatchUVOffsetUParameterName;
	extern const FName ContaminationPatchUVScaleVParameterName;
	extern const FName ContaminationPatchUVOffsetVParameterName;
	extern const FName ContaminationSurfaceWidthParameterName;
	extern const FName ContaminationPlanarSurfaceWidthParameterName;
	extern const FName ContaminationUseCylindricalCanonicalUParameterName;
	extern const FName ContaminationSurfaceHeightParameterName;
	extern const FName ContaminationSurfaceThicknessParameterName;
	extern const FName ContaminationDetailTileWorldSizeParameterName;
	extern const FName ContaminationDecalProjectionDirectionParameterName;
	extern const FName ContaminationAngleFadeStartParameterName;
	extern const FName ContaminationAngleFadeEndParameterName;
	extern const FName ContaminationFaceSplitOriginParameterName;
	extern const FName ContaminationFaceSplitNormalParameterName;
	extern const FName ContaminationFaceIsFrontParameterName;
	extern const FName ContaminationFaceOwnershipEnabledParameterName;

	/**
	 * 장식용 오염 무늬를 패치 UV가 아니라 벽 표면의 월드 좌표로 만들기 위한 기준계.
	 *
	 * 패치마다 상자 크기와 방향이 다르므로 데칼 로컬 UV로 무늬를 만들면 패치마다
	 * 무늬 밀도가 달라지고 경계에서 끊긴다. 세 값은 한 벽의 모든 패치가 공유하므로
	 * 어떤 패치가 칠하든 같은 월드 지점은 같은 무늬 좌표를 얻는다.
	 */
	extern const FName ContaminationSurfaceOriginParameterName;
	extern const FName ContaminationSurfaceTangentUParameterName;
	extern const FName ContaminationSurfaceTangentVParameterName;
	extern const FName ContaminationCurveCenterParameterName;
	extern const FName ContaminationCurveAxisParameterName;
	extern const FName ContaminationCurveStartDirectionParameterName;
	extern const FName ContaminationCurveSweepRadiansParameterName;
	extern const FName ContaminationCurveRadiusParameterName;
	extern const FName ContaminationCurveFrontIsOuterParameterName;
}
