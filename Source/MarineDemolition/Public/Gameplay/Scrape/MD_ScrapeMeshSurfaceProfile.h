#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

class UStaticMesh;

/** 한 면에서 측정한 실제 표면 깊이 정보. 모든 값은 월드 cm이며 그 면의 Bounds 바깥 평면 기준이다. */
struct MARINEDEMOLITION_API FMD_ScrapeFaceSurfaceProfile
{
	/**
	 * 모든 셀의 가장 앞선 표면을 덮는 데 필요한 깊이.
	 *
	 * 셀마다 정면에서 제일 먼저 보이는 표면의 깊이를 구한 뒤 그 최댓값을 쓴다.
	 * 이 깊이면 정면에서 보이는 표면은 빠짐없이 덮으면서, 그보다 뒤에 있어
	 * 어차피 가려진 내부 층은 칠하지 않는다.
	 */
	double VisibleSurfaceDepthCm = 0.0;

	/** 표면이 하나라도 투영되는 셀 수. 0이면 측정 실패로 간주한다. */
	int32 CoveredCellCount = 0;

	bool IsValid() const
	{
		return CoveredCellCount > 0;
	}
};

/**
 * Static Mesh의 실제 삼각형에서 앞/뒤 표면이 놓인 깊이를 측정하는 순수 계산 유틸리티.
 *
 * 평평한 판재가 아니라 앞면이 여러 깊이에 걸쳐 있는 벽 메시에서도 Decal 투영 상자가
 * 실제 표면을 덮도록, 필요한 투영 깊이를 형상에서 직접 구하기 위해 사용한다.
 *
 * 측정 기준은 긁기 판정과 동일하다. 면 법선과의 내적이 기준값 이상인 삼각형만 그 면의
 * 작업 대상으로 본다. 따라서 긁을 수 없는 급경사 옆면 때문에 투영 상자가 불필요하게
 * 깊어지지 않는다.
 */
class MARINEDEMOLITION_API FMD_ScrapeMeshSurfaceProfile final
{
public:
	/**
	 * 지정한 면의 표면 깊이를 오염 셀 격자 단위로 측정한다.
	 *
	 * CPU 정점 데이터가 없는 메시(bAllowCPUAccess가 꺼진 쿠킹 빌드 등)에서는 실패를
	 * 반환하므로 호출부는 대체 경로를 써야 한다.
	 */
	static bool TryMeasureFaceProfile(
		const UStaticMesh* MeshAsset,
		EMD_ScrapeSurfaceFace Face,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FVector& MeshAbsoluteScale,
		const FIntPoint& GridDimensions,
		const FVector2D& SurfaceSizeCm,
		float MinimumFaceNormalDot,
		FMD_ScrapeFaceSurfaceProfile& OutProfile);

	/**
	 * 측정 결과에서 이 면이 원하는 안쪽 투영 깊이를 계산한다.
	 *
	 * 정면에서 보이는 표면을 모두 덮되, 최소한 요청받은 수동 깊이는 보장한다.
	 * 반대 면과의 충돌은 BalanceFaceDepths가 따로 조정한다.
	 */
	static double CalculateFittedProjectionDepthCm(
		const FMD_ScrapeFaceSurfaceProfile& Profile,
		double RequestedProjectionDepthCm,
		double SurfaceMarginCm);

	/**
	 * 앞/뒤 투영 상자가 서로 겹치지 않도록 벽 두께를 나눠 준다.
	 *
	 * 두 면이 원하는 깊이의 합이 벽 두께를 넘으면 요구량 비율대로 함께 줄인다.
	 * 같은 깊이를 원하는 평평한 벽에서는 각 면이 정확히 절반을 갖는다.
	 * 한 면만 깊은 벽에서는 그 면이 더 많은 두께를 갖는다.
	 */
	static void BalanceFaceDepths(
		double WallThicknessCm,
		double& InOutFrontDepthCm,
		double& InOutBackDepthCm);

private:
	FMD_ScrapeMeshSurfaceProfile() = delete;
};
