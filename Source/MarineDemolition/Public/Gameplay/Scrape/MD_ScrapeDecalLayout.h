#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/** 한 면의 Deferred Decal 배치와 마스크 아틀라스 선택값. */
struct MARINEDEMOLITION_API FMD_ScrapeDecalFaceLayout
{
	FVector RelativeLocation = FVector::ZeroVector;
	FQuat RelativeRotation = FQuat::Identity;
	FVector DecalSize = FVector::ZeroVector;
	float MaskVScale = 0.5f;
	float MaskVOffset = 0.0f;
	float MaskUFlip = 0.0f;

	/** 제한을 적용한 뒤 실제로 벽 안쪽을 덮는 깊이(cm). 벽이 결과를 되읽어 보관한다. */
	double AppliedInwardDepthCm = 0.0;
};

/**
 * 벽의 앞면과 뒷면에 배치할 두 Deferred Decal의 순수 계산 규칙.
 *
 * Decal 로컬 X축은 벽 안쪽 투영 방향, 로컬 Y축은 벽 위쪽을 향한다.
 * Unreal Decal UV의 U는 로컬 Z, V는 로컬 Y를 사용하므로 앞면 U만 뒤집어
 * 두 면 모두 셀 좌표의 +X/+Z 방향과 같은 아틀라스 픽셀을 읽게 한다.
 *
 * 투영 상자는 표면 평면을 기준으로 비대칭이다. 바깥으로는 Z-fighting을 막을
 * 최소 여유만 두고 나머지는 전부 벽 안쪽으로 뻗는다. 앞면이 여러 깊이에 걸친
 * 벽 메시를 덮기 위해 투영 깊이를 크게 잡아도 벽 앞 공간의 바닥이나 소품까지
 * 오염이 투영되지 않게 하기 위해서다.
 */
class MARINEDEMOLITION_API FMD_ScrapeDecalLayout final
{
public:
	/**
	 * @param RequestedInwardDepthCm  표면에서 벽 안쪽으로 덮을 깊이. 벽 두께로 제한된다.
	 *        앞/뒤 상자가 겹치지 않게 두께를 나누는 일은 호출부의 책임이다.
	 * @param RequestedOutwardMarginCm  표면 바깥으로 남길 여유. 투영 깊이 절반으로 제한된다.
	 */
	static bool TryCalculateFaceLayout(
		EMD_ScrapeSurfaceFace Face,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FVector2D& SurfaceSizeCm,
		const FVector& MeshLocalBoundsMinimum,
		const FVector& MeshLocalBoundsMaximum,
		const FVector& MeshAbsoluteScale,
		float RequestedInwardDepthCm,
		float RequestedOutwardMarginCm,
		FMD_ScrapeDecalFaceLayout& OutLayout);

private:
	FMD_ScrapeDecalLayout() = delete;
};
