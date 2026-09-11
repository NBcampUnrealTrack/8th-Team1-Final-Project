// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "Gameplay/Scrape/MD_ScrapeBoundaryState.h"
#include "Gameplay/Scrape/MD_ScrapeCarrierWeights.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

class UMD_ScrapeMeshUVData;
struct FMD_ScrapeMeshUVSample;

/** Physical source-surface contacts; packed atlas addresses are never motion coordinates. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVStroke
{
	FVector StartWorldPosition = FVector::ZeroVector;
	FVector EndWorldPosition = FVector::ZeroVector;
	FVector StartWorldNormal = FVector::ZeroVector;
	FVector EndWorldNormal = FVector::ZeroVector;
	FVector StartWorldDepthAxis = FVector::ZeroVector;
	FVector EndWorldDepthAxis = FVector::ZeroVector;
	int32 StartTriangleIndex = INDEX_NONE;
	int32 EndTriangleIndex = INDEX_NONE;
	EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
	float TravelDistanceCm = 0.0f;
};

/** Exact weighted progress for one unchanged cell state; callers own cache freshness. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVProgress
{
	double InitialCarrierAmount = 0.0;
	double CurrentCarrierAmount = 0.0;
	bool bIsValid = false;
	/** Detailed normalized totals and raw completion predicates for incremental updates. */
	double BaseCarrierAmount = 0.0;
	double TransferredCarrierAmount = 0.0;
	int32 UncleanCellCount = INDEX_NONE;
	int32 UncleanCellCountAtZero = INDEX_NONE;
	float CleanTolerance = 0.0f;
};

/** First contact snapshot, captured before Normalize; requested work remains a density. */
struct MARINEDEMOLITION_API FMD_ScrapeTouchedCell
{
	int32 CellIndex = INDEX_NONE;
	FMD_ScrapeSurfaceCell Before;
	float RequestedRemovalAmount = 0.0f;
};

struct MARINEDEMOLITION_API FMD_ScrapeEvaluationScratch
{
	/** Per-evaluation diagnostic: atlas candidates tested by the physical MeshUV footprint. */
	int64 MeshUVVisitedCellCount = 0;
    bool bCaptureBoundary = false;
    bool bBoundaryOverflow = false;
    TArray<FMD_ScrapeBoundaryFootprint> BoundaryFootprints;
	/** MeshUV only: ascending unique contact snapshots after evaluation. */
	TArray<FMD_ScrapeTouchedCell> MeshUVTouchedCells;
	TArray<int32> MeshUVTouchedCellSlots;
	/** Legacy planar scratch; released when this scratch is used for MeshUV. */
	TBitArray<> TouchedCells;
	TArray<float> RemainingBeforeStroke;
	TArray<float> TransferredBeforeStroke;
	TArray<float> CoveredFractionBeforeStroke;
};

/**
 * 헤라 이동 선분을 표면 격자에 적용하는 순수 C++ 판정기
 *
 * 이 클래스는 UObject나 Actor가 아니며 World, Tick, 렌더링, 네트워크 상태를 미참조.
 * 같은 입력에는 항상 같은 결과를 만들기 때문에 서버 권한 판정과 단위 테스트에 적합.
 *
 * 표면 Actor의 책임:
 *  1. 월드 충돌 지점을 표면 로컬 2D 좌표로 변환
 *  2. 소유 중인 셀 배열과 설정값을 이 판정기에 전달
 *  3. 반환된 CellChanges로 머티리얼/Decal을 갱신
 *  4. ContactType과 bIsSurfaceFullyClean을 이용해 사운드, 이펙트, 해금을 실행
 *
 * 판정기의 책임:
 *  1. 프레임 사이의 헤라 경로를 빠짐없이 샘플링
 *  2. 실제로 닿은 셀의 잔여 오염량만 감소
 *  3. 전체 제거율과 맨 벽면 접촉 여부를 계산해 결과로 반환
 */
class MARINEDEMOLITION_API FMD_ScrapeEvaluator final
{
public:
	/** Follow connected source geometry. Amounts use density * world cell area / target cell area; stored cells remain densities.
	 * Optional carrier weights must have been built for this data and current transform scale.
	 * Sparse capture stores requested densities in Scratch.MeshUVTouchedCells, including bare contacts.
	 * bApplyRemoval=false captures contact work without changing any cell, including coverage and thin films. */
	static FMD_ScrapeResult ApplyStrokeToMeshUVGrid(
		const UMD_ScrapeMeshUVData& Data, const FTransform& MeshToWorld,
		const FMD_ScrapeMeshUVStroke& Stroke, const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings, FMD_ScrapeCellView InOutSurfaceCells,
		TArray<float>* OutRequestedRemovalAmounts = nullptr,
		FMD_ScrapeEvaluationScratch* Scratch = nullptr,
		FMD_ScrapeCarrierWeightView CachedCellCarrierWeights = {},
		const FMD_ScrapeMeshUVProgress* CachedProgressBefore = nullptr,
		FMD_ScrapeMeshUVProgress* OutProgressAfter = nullptr,
		bool bCaptureTouchedRequestedAmounts = false,
		bool bApplyRemoval = true);

	/** Interpolate on connected source geometry; never interpolate packed atlas UVs. */
	static bool TryInterpolateMeshUVStroke(
		const UMD_ScrapeMeshUVData& Data, const FTransform& MeshToWorld,
		const FMD_ScrapeMeshUVStroke& Stroke, double Alpha,
		FMD_ScrapeMeshUVSample& OutSample, FVector& OutWorldDepthAxis);

	/** Returns the exact movement spacing used to subdivide one scrape sweep. */
	static double CalculateStrokeSampleSpacingCm(
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm);

	/**
	 * 한 번의 헤라 이동을 격자 셀 배열에 적용
	 *
	 * @param Stroke
	 * 표면 중앙 기준 로컬 2D 좌표로 기록된 이번 헤라 이동 경로
	 *
	 * @param Settings
	 * 기본 헤라 폭, 거리당 제거량, 샘플 간격, 완료 허용 오차를 담은 설정
	 *
	 * @param GridDimensions
	 * 격자의 가로 셀 수(X)와 세로 셀 수(Y). 두 값 모두 1 이상 필수
	 *
	 * @param SurfaceSizeCm
	 * 격자가 덮는 실제 표면의 가로·세로 크기(cm)
	 * 표면 영역은 (-Size/2)부터 (+Size/2)까지로 간주
	 *
	 * @param InOutSurfaceCells
	 * 표면의 현재 셀 상태 배열
	 * 배열 크기는 GridDimensions.X * GridDimensions.Y와 정확히 일치 필요
	 * 함수가 성공하면 헤라와 겹친 셀의 RemainingContamination이 직접 감소
	 *
	 * @return
	 * 입력 유효성, 접촉 종류, 제거율 변화, 변경된 셀 목록을 담은 결과
	 */
	static FMD_ScrapeResult ApplyStrokeToGrid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeCellView InOutSurfaceCells);

	/**
	 * 장착한 헤라의 형태와 성능을 명시하여 한 번의 이동을 격자에 적용
	 *
	 * 기존 오버로드는 ScrapeSettings 안의 수치로 기본 사각 스펙을 만들어 이 함수로 전달한다.
	 */
	static FMD_ScrapeResult ApplyStrokeToGrid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeCellView InOutSurfaceCells);
	static FMD_ScrapeResult ApplyStrokeToGrid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeCellView InOutSurfaceCells,
		FMD_ScrapeEvaluationScratch& Scratch);

	/**
	 * 헤라 경로가 각 셀에 요청한 작업량을 기록한다.
	 * bApplyRemoval=false이면 제거·정규화·피복 갱신 없이 접촉 작업량만 계산한다.
	 *
	 * 오염이 이미 없는 셀도 경로 기록에는 포함된다. 따라서 포화 헤라가
	 * 깨끗한 곳을 지나갈 때도 같은 모양의 새 오염 자국을 만들 수 있다.
	 */
	static FMD_ScrapeResult ApplyStrokeToGridAndCaptureRequestedAmounts(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeCellView InOutSurfaceCells,
		TArray<float>& OutRequestedRemovalAmounts,
		bool bApplyRemoval = true);
	static FMD_ScrapeResult ApplyStrokeToGridAndCaptureRequestedAmounts(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeCellView InOutSurfaceCells,
		TArray<float>& OutRequestedRemovalAmounts,
		FMD_ScrapeEvaluationScratch& Scratch,
		bool bApplyRemoval = true);

	/**
	 * 캡처한 헤라 경로를 새 오염 자국으로 도포한다.
	 *
	 * 모든 셀이 RequestedRemovalAmounts * StampRatio를 수용할 수 있는지 먼저
	 * 검사하고, 하나라도 상한 1.0을 넘으면 어느 셀도 바꾸지 않는다.
	 * @return 전 셀을 원자적으로 적용했으면 true, 입력 또는 용량 경계를 거부했으면 false
	 */
	static bool TryApplyContaminationStampToGrid(
		const FIntPoint& GridDimensions,
		TConstArrayView<float> RequestedRemovalAmounts,
		float StampRatio,
		FMD_ScrapeCellView InOutSurfaceCells,
		TArray<FMD_ScrapeCellChange>& OutCellChanges,
		float& OutAddedContaminationAmount,
		FMD_ScrapeCarrierWeightView CellCarrierWeights = {});

	/** MeshUV sparse stamp: strictly ascending unique records, atomic O(K) preflight and apply.
	 * RequestedRemovalAmount is density, whereas OutAddedContaminationAmount is weighted carrier. */
	static bool TryApplyContaminationStampToTouchedCells(
		const FIntPoint& GridDimensions,
		TConstArrayView<FMD_ScrapeTouchedCell> TouchedCells,
		float StampRatio,
		FMD_ScrapeCellView InOutSurfaceCells,
		TArray<FMD_ScrapeCellChange>& OutCellChanges,
		float& OutAddedContaminationAmount,
		FMD_ScrapeCarrierWeightView CellCarrierWeights = {});

	/** Full scan for initialization/general callers; no cells are mutated. */
	static bool CalculateMeshUVProgress(
		FMD_ScrapeConstCellView Cells,
		FMD_ScrapeCarrierWeightView CellCarrierWeights,
		float CleanTolerance,
		FMD_ScrapeMeshUVProgress& OutProgress);

	/** Applies sorted first-contact to final-state deltas to an unchanged-state cache.
	 * Negative accumulated rounding error up to 1e-9 * max(1, initial, previous current)
	 * is clamped to zero; larger inconsistencies reject without changing OutProgress. */
	static bool UpdateMeshUVProgressFromTouchedCells(
		const FMD_ScrapeMeshUVProgress& ProgressBefore,
		TConstArrayView<FMD_ScrapeTouchedCell> TouchedCells,
		FMD_ScrapeConstCellView Cells,
		FMD_ScrapeCarrierWeightView CellCarrierWeights,
		FMD_ScrapeMeshUVProgress& OutProgress);

	/**
	 * ApplyStrokeToGrid를 실행할 수 있는 입력인지 부작용 없이 검사
	 *
	 * @param OutErrorMessage
	 * nullptr이 아니라면 실패 이유를 사람이 읽을 수 있는 문자열로 기록
	 * 이 문자열은 개발 중 로그/화면 출력용이며 게임 규칙 비교에는 미사용
	 */
	static bool IsEvaluationInputValid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeConstCellView SurfaceCells,
		FString* OutErrorMessage = nullptr);

	/** 장착한 헤라 스펙을 포함하여 판정 입력을 검사 */
	static bool IsEvaluationInputValid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeConstCellView SurfaceCells,
		FString* OutErrorMessage = nullptr);

	/**
	 * 현재 표면에서 제거된 오염 비율을 0~1 범위로 계산
	 *
	 * 계산식:
	 *  1 - (모든 셀의 잔여 오염량 합 / 모든 셀의 초기 오염량 합)
	 *
	 * 초기 오염량이 0인 셀은 분모에 영향 없음.
	 * 표면 전체가 처음부터 깨끗했다면 별도 작업이 필요 없으므로 1을 반환
	 */
	static float CalculateCleanRatio(
		FMD_ScrapeConstCellView SurfaceCells);

	/**
	 * 벽 조각 배분에 사용할 현재 제거율을 계산한다.
	 * 기본 오염뿐 아니라 포화된 헤라가 다시 묻힌 오염도 남은 양에 포함한다.
	 */
	static float CalculateCurrentCleanRatio(
		FMD_ScrapeConstCellView SurfaceCells);
	static float CalculateCurrentCleanRatio(
		FMD_ScrapeConstCellView SurfaceCells,
		double& OutInitialContaminationAmount,
		double& OutCurrentContaminationAmount);
	static float CalculateCurrentCleanRatio(
		double InitialContaminationAmount,
		double CurrentContaminationAmount);

	/**
	 * 초기 오염이 있던 모든 셀이 허용 오차 이하까지 제거됐는지 검사
	 *
	 * 단순히 반올림된 퍼센트가 100%인지 보는 대신 셀을 직접 검사하므로,
	 * 한 셀에 오염이 남았는데 UI 반올림 때문에 완료되는 오류를 차단
	 */
	static bool IsSurfaceFullyClean(
		FMD_ScrapeConstCellView SurfaceCells,
		float CleanTolerance);

private:
	/** 정적 함수만 제공하는 유틸리티 클래스이므로 인스턴스 생성 금지 */
	FMD_ScrapeEvaluator() = delete;

	static FMD_ScrapeResult ApplyStrokeToGridInternal(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		FMD_ScrapeCellView InOutSurfaceCells,
		TArray<float>* OutRequestedRemovalAmounts,
		FMD_ScrapeEvaluationScratch* Scratch,
		bool bApplyRemoval = true);

	/**
	 * 원형 헤라 브러시가 한 셀을 얼마나 덮는지 0~1 가중치로 근사
	 *
	 * 셀 중심만으로 딱 잘라 판정하면 격자 모양의 거친 가장자리가 생기므로,
	 * 셀 반대각선 길이만큼 경계를 부드럽게 보간
	 */
	static double CalculateCircleCellCoverageWeight(
		double DistanceFromBrushCenterCm,
		double BladeRadiusCm,
		double CellHalfDiagonalCm);

	/** 이동 방향을 기준으로 회전한 사각 헤라가 한 셀을 덮는 가중치를 근사 */
	static double CalculateRectangleCellCoverageWeight(
		const FVector2D& BrushToCellCenterCm,
		const FVector2D& BladeWidthAxis,
		const FVector2D& BladeDepthAxis,
		double BladeHalfWidthCm,
		double BladeHalfDepthCm,
		double CellHalfDiagonalCm);

	/** 브러시 경계의 signed distance를 셀 피복 가중치 0~1로 변환 */
	static double CalculateCoverageWeightFromSignedDistance(
		double SignedDistanceToBrushCm,
		double CellHalfDiagonalCm);
};
