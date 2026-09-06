#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

struct FMD_ScrapeCylindricalSurfaceFrame;

/** 한 면의 Deferred Decal 배치와 마스크 아틀라스 선택값. */
struct MARINEDEMOLITION_API FMD_ScrapeDecalFaceLayout
{
	FVector RelativeLocation = FVector::ZeroVector;
	FQuat RelativeRotation = FQuat::Identity;
	FVector DecalSize = FVector::ZeroVector;
	float MaskVScale = 0.5f;
	float MaskVOffset = 0.0f;
	float MaskUFlip = 0.0f;
	float PatchUVScaleU = 1.0f;
	float PatchUVOffsetU = 0.0f;
	float PatchUVScaleV = 1.0f;
	float PatchUVOffsetV = 0.0f;

	/** 제한을 적용한 뒤 실제로 벽 안쪽을 덮는 깊이(cm). 벽이 결과를 되읽어 보관한다. */
	double AppliedInwardDepthCm = 0.0;

	/** The world-space margin kept outside the receiver surface. */
	double AppliedOutwardMarginCm = 0.0;

	/** 제한을 적용한 뒤 실제로 표면 테두리에서 줄인 폭(cm). */
	double AppliedEdgeInsetCm = 0.0;
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
 *
 * 상자의 옆면은 표면 테두리에서 안쪽으로 조금 물러난다. 상자 옆면이 벽 실루엣과
 * 정확히 겹치면 벽의 두께 면이나 맞닿은 바닥처럼 투영축과 거의 평행한 면이
 * 상자 경계에 걸린다. 그런 면에서는 UV가 발산해 텍셀이 길게 늘어나고, 시간적
 * AA의 서브픽셀 흔들림만으로도 상자 안팎 판정이 매 프레임 뒤집혀 지지직거린다.
 */
class MARINEDEMOLITION_API FMD_ScrapeDecalLayout final
{
public:
	/** 테두리 inset이 상자를 없애지 않도록 짧은 축에 대해 허용하는 최대 비율. */
	static constexpr double MaximumEdgeInsetRatio = 0.2;

	/**
	 * @param RequestedInwardDepthCm  표면에서 벽 안쪽으로 덮을 깊이. 벽 두께로 제한된다.
	 *        앞/뒤 상자가 겹치지 않게 두께를 나누는 일은 호출부의 책임이다.
	 * @param RequestedOutwardMarginCm  표면 바깥으로 남길 여유. 투영 깊이 절반으로 제한된다.
	 * @param RequestedEdgeInsetCm  상자 옆면을 표면 테두리에서 안쪽으로 물릴 폭.
	 *        짧은 축 길이의 MaximumEdgeInsetRatio까지만 적용된다.
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
		float RequestedEdgeInsetCm,
		FMD_ScrapeDecalFaceLayout& OutLayout);

	/** Returns how many horizontal projection patches are needed. */
	static int32 CalculateHorizontalPatchCount(
		double SurfaceWidthCm,
		double TargetPatchWidthCm,
		int32 MaximumPatchCount);

	/** Splits one face layout into a continuous horizontal UV patch. */
	static bool TryCalculateHorizontalPatchLayout(
		const FMD_ScrapeDecalFaceLayout& FaceLayout,
		EMD_ScrapeSurfaceNormalAxis SurfaceNormalAxis,
		const FVector& MeshAbsoluteScale,
		int32 PatchIndex,
		int32 PatchCount,
		FMD_ScrapeDecalFaceLayout& OutPatchLayout);

	/** 곡면의 한 구간을 담당하도록 중심 법선에 맞춰 회전한 투영 상자를 계산한다. */
	static bool TryCalculateCylindricalPatchLayout(
		EMD_ScrapeSurfaceFace Face,
		const FTransform& SurfaceTransform,
		const FVector& SurfaceCenterLocal,
		const FVector2D& SurfaceSizeCm,
		const FMD_ScrapeCylindricalSurfaceFrame& CurveFrame,
		float RequestedInwardDepthCm,
		float RequestedOutwardMarginCm,
		float RequestedEdgeInsetCm,
		int32 PatchIndex,
		int32 PatchCount,
		FMD_ScrapeDecalFaceLayout& OutPatchLayout);

	/** 요청한 테두리 inset을 표면 크기가 허용하는 범위로 자른다. */
	static double ClampEdgeInsetCm(
		const FVector2D& SurfaceSizeCm,
		float RequestedEdgeInsetCm);

	/**
	 * 테두리 inset으로 좁아진 데칼이 마스크의 같은 구간만 읽도록 반 텍셀 값을 키운다.
	 *
	 * 머티리얼은 MaskHalfTexelU/V를 그대로 Lerp 양 끝으로 쓰므로, 여기에 inset
	 * 비율을 접어 넣으면 머티리얼 그래프를 바꾸지 않고도 무늬가 셀 위치와 맞는다.
	 * inset이 0이면 기존 반 텍셀 값과 완전히 같다.
	 */
	static bool TryCalculateMaskHalfTexelUV(
		const FIntPoint& AtlasDimensions,
		const FVector2D& SurfaceSizeCm,
		float RequestedEdgeInsetCm,
		FVector2D& OutMaskHalfTexelUV);

private:
	FMD_ScrapeDecalLayout() = delete;
};
