#pragma once

#include "CoreMinimal.h"

/** 월드 위치에서 반복 가능한 초기 오염량을 만드는 순수 계산 유틸리티. */
class MARINEDEMOLITION_API FMD_ContaminationNoise final
{
public:
	/** 격자 좌표의 부드러운 3D 값 노이즈를 0~1로 반환한다. */
	static float ValueNoise3D(
		const FVector& Position,
		int32 Seed);

	/** 서로 다른 크기의 값 노이즈 세 겹을 합쳐 0~1 얼룩을 만든다. */
	static float FractalNoise3D(
		const FVector& WorldPositionCm,
		float NoiseScaleCm,
		int32 Seed);

	/** 위치와 커버리지 설정을 실제 셀 초기 오염량 0~BaseAmount로 변환한다. */
	static bool TryCalculateInitialContamination(
		const FVector& WorldPositionCm,
		float BaseAmount,
		float NoiseScaleCm,
		int32 Seed,
		float Coverage,
		float MinimumAmount,
		float& OutInitialContamination);

private:
	FMD_ContaminationNoise() = delete;
};
