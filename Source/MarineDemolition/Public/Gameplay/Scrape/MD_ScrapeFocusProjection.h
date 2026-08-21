#pragma once

#include "CoreMinimal.h"

struct FMinimalViewInfo;
enum EAspectRatioAxisConstraint : int;

/**
 * 헤라 집중 조준이 사용하는 화면 투영 계산.
 *
 * 결과가 카메라 정보와 뷰포트 종횡비만으로 결정되므로 컴포넌트 상태,
 * 네트워크 권한, 월드를 참조하지 않는다. 클라이언트가 보낸 값을 서버가
 * 같은 식으로 다시 계산해 검증할 수 있어야 하기 때문에 순수 함수로 둔다.
 */
class MARINEDEMOLITION_API FMD_ScrapeFocusProjection final
{
public:
	/** 세로 9:32부터 가로 32:9까지. 이 범위 밖은 조작 불가능한 화면으로 본다. */
	static constexpr float MinimumSupportedAspectRatio = 9.0f / 32.0f;
	static constexpr float MaximumSupportedAspectRatio = 32.0f / 9.0f;

	/** 클라이언트가 요청한 종횡비가 지원 범위 안의 유한값인지 검사한다. */
	static bool IsSupportedAspectRatio(float AspectRatio);

	/**
	 * 카메라의 가로·세로 half-tangent를 구한다.
	 *
	 * 축 제약(MaintainX/Y/MajorAxis)에 따라 어느 축의 FOV가 고정되는지가
	 * 달라지므로, 화면 좌표를 각도로 되돌리려면 두 축을 함께 계산해야 한다.
	 * 오프센터 투영이나 직교 투영처럼 이 규칙이 성립하지 않는 카메라는 실패로 반환한다.
	 */
	static bool TryCalculateHalfTangents(
		const FMinimalViewInfo& ViewInfo,
		EAspectRatioAxisConstraint DefaultAxisConstraint,
		float ViewportAspectRatio,
		bool bUseLegacyMaintainYFOV,
		float& OutHorizontalHalfTangent,
		float& OutVerticalHalfTangent);

	/** 세션 시작 때 합의한 종횡비를 현재 뷰포트가 아직 유지하는지 검사한다. */
	static bool IsViewportAspectCompatible(
		float AgreedAspectRatio,
		const FIntPoint& CurrentViewportSize,
		float AbsoluteTolerance);

	/**
	 * 권위 원뿔이 화면에서 차지하는 정규화 반경을 구한다.
	 *
	 * 반환값은 화면 중앙 기준 0~0.5 범위이며, 포인터가 서버가 허용하지 않는
	 * 각도로 나가지 못하도록 클램프하는 데 사용한다.
	 */
	static float CalculatePointerNormalizedHalfRange(
		float ProjectionHalfTangent,
		float AuthorityHalfTangent,
		float PointerSafeMargin);

private:
	FMD_ScrapeFocusProjection() = delete;
};
