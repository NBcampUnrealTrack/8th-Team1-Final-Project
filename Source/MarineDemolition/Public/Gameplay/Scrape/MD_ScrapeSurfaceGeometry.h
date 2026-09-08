#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/** 머티리얼과 CPU가 함께 사용하는, 월드 공간에서 해석된 원통 곡면 기준계. */
struct MARINEDEMOLITION_API FMD_ScrapeCylindricalSurfaceFrame final
{
	FVector CenterWorld = FVector::ZeroVector;
	FVector AxisWorld = FVector::ZAxisVector;
	FVector StartDirectionWorld = FVector::XAxisVector;
	FVector TangentDirectionWorld = FVector::YAxisVector;
	double SweepRadians = 0.0;
	double RadiusCm = 0.0;
	double ThicknessCm = 0.0;
	bool bFrontIsOuter = true;
};

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

	/** 원통 중심선의 호 길이와 축 방향 Bounds 높이를 실제 월드 cm로 계산한다. */
	static bool TryCalculateMappedSurfaceSize(
		const FTransform& SurfaceTransform,
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		const FVector& SurfaceBoundsMinimumLocal,
		const FVector& SurfaceBoundsMaximumLocal,
		FVector2D& OutSurfaceSizeCm);

	/** 선택한 앞/뒤 축에서 오른손 좌표계인 가로, 앞면 법선, 세로 로컬 축을 반환한다. */
	static bool TryGetSurfaceLocalAxes(
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		FVector& OutHorizontalAxis,
		FVector& OutFrontNormal,
		FVector& OutVerticalAxis);

	/** 충돌 법선이 선택한 로컬 축의 앞면 또는 뒷면을 향하는지 판별한다. */
	static bool TryResolveSurfaceFaceFromImpactNormal(
		const FVector& WorldPosition,
		const FVector& ImpactNormal,
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
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
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		const FVector2D& SurfaceSizeCm,
		FVector& OutWorldPosition);

	/** 월드 좌표를 앞/뒤가 공유하는 표면 로컬 2D cm 좌표로 투영한다. */
	static bool TryProjectWorldPositionToSurface(
		const FVector& WorldPosition,
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FVector& SurfaceCenterLocal,
		const FVector& SurfaceBoundsMinimumLocal,
		const FVector& SurfaceBoundsMaximumLocal,
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		const FVector2D& SurfaceSizeCm,
		FVector2D& OutSurfacePositionCm);

	/** 지정한 표면 위치에서 앞/뒤 면의 바깥쪽 월드 법선을 반환한다. */
	static bool TryCalculateSurfaceWorldNormal(
		const FVector2D& SurfacePositionCm,
		EMD_ScrapeSurfaceFace SurfaceFace,
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		const FVector2D& SurfaceSizeCm,
		FVector& OutWorldNormal);

	/**
	 * 메시 UV(0~1)를 셀 파이프라인이 쓰는 표면 cm 좌표로 바꾼다.
	 *
	 * 머티리얼은 같은 UV를 마스크 아틀라스 주소로 그대로 쓴다. 즉 이 변환은
	 * 주소를 만들지 않고 이미 존재하는 주소의 단위만 바꾼다. 0~1을 벗어난 UV는
	 * 가장자리로 Clamp하지 않고 실패로 처리한다. Clamp하면 넓은 영역이 한 셀로
	 * 뭉쳐 원래 문제였던 "화면은 넓게, 셀은 좁게" 불일치가 되돌아온다.
	 */
	static bool TryConvertMeshUVToSurfacePositionCm(
		const FVector2D& MeshUV,
		const FVector2D& SurfaceSizeCm,
		FVector2D& OutSurfacePositionCm);

	/** 표면 cm 좌표를 다시 메시 UV(0~1)로 바꾼다. 위 변환의 정확한 역이다. */
	static bool TryConvertSurfacePositionCmToMeshUV(
		const FVector2D& SurfacePositionCm,
		const FVector2D& SurfaceSizeCm,
		FVector2D& OutMeshUV);

	/** 곡면 앞/평면 뒤 모드에서 CPU와 머티리얼이 공유할 소유권 분할 평면을 반환한다. */
	static bool TryResolveCurvedFrontPlanarBackSplitPlane(
		const FTransform& SurfaceTransform,
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		FVector& OutSplitOriginWorld,
		FVector& OutFrontHalfNormalWorld);

	/** 로컬 곡면 설정을 머티리얼에 전달 가능한 월드 기준계로 검증·변환한다. */
	static bool TryResolveCylindricalSurfaceFrame(
		const FTransform& SurfaceTransform,
		const FMD_ScrapeSurfaceMapping& SurfaceMapping,
		FMD_ScrapeCylindricalSurfaceFrame& OutFrame);

private:
	FMD_ScrapeSurfaceGeometry() = delete;

	static bool TryGetSurfaceWorldAxes(
		const FTransform& SurfaceTransform,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		FVector& OutHorizontalAxis,
		FVector& OutFrontNormal,
		FVector& OutVerticalAxis);
};
