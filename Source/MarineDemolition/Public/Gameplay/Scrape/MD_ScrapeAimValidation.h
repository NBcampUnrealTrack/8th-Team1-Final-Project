#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/**
 * 서버가 한 세션 동안 허용하는 조준 원뿔.
 *
 * 화면 픽셀이 아니라 half-tangent로 들고 있으므로 해상도나 창 크기가
 * 달라져도 같은 각도 범위를 뜻한다.
 */
struct MARINEDEMOLITION_API FMD_ScrapeAimAuthorityCone
{
	/** 카메라 정면 기준 가로 half-tangent. 0 이하이면 원뿔이 없는 것으로 본다. */
	float HorizontalHalfTangent = 0.0f;

	/** 카메라 정면 기준 세로 half-tangent. */
	float VerticalHalfTangent = 0.0f;

	/** 화면 가장자리에서 잘라내는 안전 여백 비율. 0~0.49로 클램프한다. */
	float PointerSafeMargin = 0.0f;

	/** 네트워크 지연으로 경계에서 튕기지 않도록 더해 주는 각도 여유. */
	float MarginDegrees = 0.0f;
};

/**
 * 권위 서버가 클라이언트 조준 표본을 받아들일지 판정하는 순수 계산.
 *
 * 시간·거리·각도만 다루고 월드나 컴포넌트를 참조하지 않으므로,
 * 같은 입력이면 서버와 테스트가 항상 같은 결론을 낸다.
 */
class MARINEDEMOLITION_API FMD_ScrapeAimValidation final
{
public:
	/** 두 회전의 최단 각도 차이. NaN 입력은 최댓값을 돌려 거부하게 만든다. */
	static float CalculateRotatorErrorDegrees(
		const FRotator& A,
		const FRotator& B);

	/** 연속한 두 조준 결과가 순간이동 수준으로 벌어졌는지 검사한다. */
	static bool IsPositionDiscontinuous(
		const FMD_ScrapeAimQueryResult& PreviousAim,
		const FMD_ScrapeAimQueryResult& CurrentAim,
		double MaximumDistanceCm);

	/**
	 * 클라이언트 시계로 찍힌 두 표본 사이의 적용 시간 간격을 구한다.
	 *
	 * 되감김이나 과도한 공백은 불연속으로 보고하고, 정상 구간은 최대 표본율과
	 * 허용 표본 나이 사이로 클램프해 한 프레임이 과대 적용되지 않게 한다.
	 */
	static float CalculateSampleDeltaTime(
		double PreviousClientSeconds,
		double CurrentClientSeconds,
		float MaximumSampleAgeSeconds,
		double MaximumSampleRateHz,
		bool& bOutIsDiscontinuous);

	/** 카메라 기준 방향이 세션 권위 원뿔 안에 있는지 검사한다. */
	static bool IsDirectionInsideCone(
		const FVector& CameraRelativeDirection,
		const FMD_ScrapeAimAuthorityCone& Cone);

private:
	FMD_ScrapeAimValidation() = delete;
};
