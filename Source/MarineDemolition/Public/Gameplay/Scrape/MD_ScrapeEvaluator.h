// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

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
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells);

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
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells);

	/**
	 * 일반 제거 판정과 함께 헤라 경로가 각 셀에 요청한 작업량을 기록한다.
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
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
		TArray<float>& OutRequestedRemovalAmounts);

	/**
	 * 캡처한 헤라 경로를 새 오염 자국으로 도포한다.
	 *
	 * 기존 오염의 과거 상태를 복원하지 않는다. 현재 셀 값 위에
	 * RequestedRemovalAmounts * StampRatio만큼 새 오염을 더한다.
	 *
	 * @return 실제로 추가된 오염량의 합계
	 */
	static float ApplyContaminationStampToGrid(
		const FIntPoint& GridDimensions,
		TConstArrayView<float> RequestedRemovalAmounts,
		float StampRatio,
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
		TArray<FMD_ScrapeCellChange>& OutCellChanges);

	/**
	 * Stage3 ordered path가 확정한 하나의 dab을 저장된 위치·basis 그대로 적용한다.
	 * 확률·간격·jitter는 candidate 생성 단계에서 이미 확정되었으므로 여기서 다시
	 * 난수를 뽑지 않는다.
	 */
	static float ApplyContaminationDabToGrid(
		const FMD_ScrapeContaminationDab& Dab,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		float StampAmount,
		float StampDepthCm,
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
		TArray<FMD_ScrapeCellChange>& OutCellChanges);

	/** 기존 직선 구현과 같은 제거 footprint 보호 거리를 계산한다. */
	static double CalculateContaminationPaintProtectionDistanceCm(
		const FMD_ScrapeToolSpec& ToolSpec,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		float StampDepthCm);

	/** identity 기반의 결정적 확률 선택. */
	static bool ShouldSelectContaminationDab(
		const FMD_ScrapeContaminationStampIdentity& Identity,
		float StampChance,
		int32 PatternSeed);

	/** distance bin 안의 결정적 jitter까지 포함한 sub-stroke arc 위치. */
	static double CalculateContaminationDabArcLengthCm(
		const FMD_ScrapeContaminationStampIdentity& Identity,
		float StampDepthCm,
		float StampSpacingCm,
		int32 PatternSeed);

	/**
	 * 새 도장을 헤라의 제거 범위 뒤로 밀어, 다음 프레임 제거가 방금 묻힌
	 * 오염을 다시 깎지 않는 안전한 도포 구간을 만든다.
	 */
	static bool TryBuildProtectedContaminationPaintStroke(
		const FMD_ScrapeStroke& Stroke,
		const FVector2D& PathStartPositionCm,
		const FVector2D& PathAxis,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		float StampDepthCm,
		FMD_ScrapeStroke& OutPaintStroke);

	/** 보호 거리 때문에 남겨 둔 마지막 구간을 입력 종료 시 도포할 stroke로 만든다. */
	static bool TryBuildPendingContaminationPaintStroke(
		const FMD_ScrapeStroke& FullPathStroke,
		const FVector2D& PathAxis,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		float StampDepthCm,
		FMD_ScrapeStroke& OutPaintStroke);

	/**
	 * 경로 위의 무작위 지점에 헤라의 실제 접촉 모양을 페인트 도장처럼 누적한다.
	 *
	 * 도장 후보는 전역 표면 좌표의 고정 간격 bin에서 만들고 PatternSeed로
	 * 선택·위치 흔들림을 결정하므로 같은 직선 경로를 여러 프레임으로 나누어도
	 * 최종 도포 결과가 같다.
	 */
	static float ApplyContaminationPaintStrokeToGrid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		float StampAmount,
		float StampChance,
		float StampDepthCm,
		float StampSpacingCm,
		int32 PatternSeed,
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
		TArray<FMD_ScrapeCellChange>& OutCellChanges);

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
		TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
		FString* OutErrorMessage = nullptr);

	/** 장착한 헤라 스펙을 포함하여 판정 입력을 검사 */
	static bool IsEvaluationInputValid(
		const FMD_ScrapeStroke& Stroke,
		const FMD_ScrapeToolSpec& ToolSpec,
		const FMD_ScrapeSettings& Settings,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
		TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells);

	/**
	 * 벽 조각 배분에 사용할 현재 제거율을 계산한다.
	 * 기본 오염뿐 아니라 포화된 헤라가 다시 묻힌 오염도 남은 양에 포함한다.
	 */
	static float CalculateCurrentCleanRatio(
		TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells);
	static float CalculateCurrentCleanRatio(
		TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
		TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
		TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
		TArray<float>* OutRequestedRemovalAmounts);

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
