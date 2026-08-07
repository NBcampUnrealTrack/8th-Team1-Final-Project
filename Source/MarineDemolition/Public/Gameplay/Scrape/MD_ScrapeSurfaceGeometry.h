#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/** 앞/뒤 작업면 축 판별과 표면 2D 좌표 변환을 담당하는 순수 계산 유틸리티. */
class MARINEDEMOLITION_API FMD_ScrapeSurfaceGeometry final
{
public:
	/** Auto이면 모델 Bounds와 스케일을 이용해 두께 축을 선택하고 수동 축은 그대로 검증한다. */
	static bool TryResolveSurfaceNormalAxis(
		EMD_ScrapeSurfaceNormalAxis RequestedNormalAxis,
		const FVector& SurfaceBoundsMinimumLocal,
		const FVector& SurfaceBoundsMaximumLocal,
		const FVector& SurfaceAbsoluteScale,
		EMD_ScrapeSurfaceNormalAxis& OutResolvedNormalAxis);

	/** 선택한 축에 수직인 작업면의 가로/세로 월드 크기를 계산한다. */
	static bool TryCalculateSurfaceSize(
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FVector& SurfaceBoundsMinimumLocal,
		const FVector& SurfaceBoundsMaximumLocal,
		const FVector& SurfaceAbsoluteScale,
		FVector2D& OutSurfaceSizeCm);

	/** 선택한 앞/뒤 축에서 오른손 좌표계인 가로, 앞면 법선, 세로 로컬 축을 반환한다. */
	static bool TryGetSurfaceLocalAxes(
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		FVector& OutHorizontalAxis,
		FVector& OutFrontNormal,
		FVector& OutVerticalAxis);

	/** 충돌 법선이 선택한 로컬 축의 앞면 또는 뒷면을 향하는지 판별한다. */
	static bool TryResolveSurfaceFaceFromImpactNormal(
		const FVector& ImpactNormal,
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		float MinimumFaceNormalDot,
		EMD_ScrapeSurfaceFace& OutSurfaceFace);

	/** 표면 로컬 2D cm 좌표를 지정한 앞/뒤 면의 월드 좌표로 변환한다. */
	static bool TryCalculateSurfaceWorldPosition(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FVector& SurfaceCenterLocal,
		const FVector& SurfaceBoundsMinimumLocal,
		const FVector& SurfaceBoundsMaximumLocal,
		FVector& OutWorldPosition);

	/** 월드 좌표를 앞/뒤가 공유하는 표면 로컬 2D cm 좌표로 투영한다. */
	static bool TryProjectWorldPositionToSurface(
		const FVector& WorldPosition,
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FVector& SurfaceCenterLocal,
		FVector2D& OutSurfacePositionCm);

private:
	FMD_ScrapeSurfaceGeometry() = delete;

	static bool TryGetSurfaceWorldAxes(
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		FVector& OutHorizontalAxis,
		FVector& OutFrontNormal,
		FVector& OutVerticalAxis);
};
