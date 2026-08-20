#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/** 날 방향만 프레임 분할과 무관하게 계산하기 위한 고정 arc-length 표본. */
struct FMD_ScrapeBladeAxisSample
{
	FVector2D PositionCm = FVector2D::ZeroVector;
	double ArcLengthCm = 0.0;
};

/**
 * 최근 이동 구간만 남겨 두는 표본 창.
 *
 * 매 프레임의 순간 이동 방향을 그대로 쓰면 손 떨림이 그대로 날 각도에 실린다.
 * 일정 arc-length만큼 뒤의 위치와 현재 위치를 잇는 방향을 쓰면, 프레임률이
 * 달라져도 같은 경로에서 같은 방향이 나온다.
 */
struct MARINEDEMOLITION_API FMD_ScrapeBladeAxisWindow
{
	/** 새 stroke를 시작할 때 표본을 모두 버린다. */
	void Reset();

	/**
	 * 표본 하나를 추가하고 창 밖으로 밀려난 오래된 표본을 버린다.
	 *
	 * 창 시작점을 보간할 수 있도록 창보다 한 칸 더 오래된 표본까지는 남긴다.
	 */
	void AddSample(
		const FVector2D& PositionCm,
		double ArcLengthCm,
		double WindowCm,
		double ResampleSpacingCm);

	/**
	 * 창 시작점에서 최신 표본으로 향하는 방향을 구한다.
	 *
	 * 표본이 부족하거나 두 점이 사실상 같은 자리라 방향을 못 만들면 false를
	 * 반환하고 OutAxis를 건드리지 않는다. 호출자는 직전 축을 그대로 쓰면 된다.
	 */
	bool TryCalculateWindowedAxis(
		double WindowCm,
		FVector2D& OutAxis) const;

	int32 GetSampleCount() const;

private:
	TArray<FMD_ScrapeBladeAxisSample> Samples;
};

/**
 * 날 방향축을 다루는 순수 계산.
 *
 * 날 방향은 180도 동치이므로 두 축을 비교할 때는 항상 같은 반구로 맞춘 뒤
 * 각도를 재야 한다. 이 규칙을 한곳에 모아 둔다.
 */
class MARINEDEMOLITION_API FMD_ScrapeBladeAxis final
{
public:
	/** 깊이축에서 폭축을 만들어 판정기가 쓰는 basis로 바꾼다. */
	static FMD_ScrapeBladeBasis MakeBasis(const FVector2D& BladeDepthAxis);

	/** 두 축 사이를 최단 회전 방향으로 보간한다. */
	static FVector2D Interpolate(
		const FVector2D& StartAxis,
		const FVector2D& EndAxis,
		double Alpha);

	/**
	 * 현재 축을 목표 축 쪽으로 최대 각도만큼만 돌린다.
	 *
	 * 이동 거리에 비례한 상한을 두면 급격히 꺾인 경로에서도 날이 순간적으로
	 * 회전하지 않아, 긁힌 자국의 경계가 튀지 않는다.
	 */
	static FVector2D RotateTowards(
		const FVector2D& CurrentAxis,
		const FVector2D& TargetAxis,
		double MaximumTurnRadians);

private:
	FMD_ScrapeBladeAxis() = delete;
};
