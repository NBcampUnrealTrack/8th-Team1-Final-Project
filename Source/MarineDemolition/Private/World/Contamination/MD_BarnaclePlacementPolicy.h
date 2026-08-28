#pragma once

#include "CoreMinimal.h"

struct FRandomStream;

enum class EMD_BarnaclePlacementFace : uint8
{
	Front,
	Back,
	Top,
	Bottom,
	Left,
	Right,
	Other
};

struct FMD_BarnaclePlacementCandidate
{
	FVector Location = FVector::ZeroVector;
	FVector SurfaceNormal = FVector::ZeroVector;
	EMD_BarnaclePlacementFace Face = EMD_BarnaclePlacementFace::Other;
	int32 SourceIndex = INDEX_NONE;
};

struct FMD_BarnaclePlacementPlan
{
	TArray<int32> OrderedCandidateIndices;
	int32 FrontCount = 0;
	int32 BackCount = 0;
	int32 TopCount = 0;
	int32 BottomCount = 0;
	int32 LeftCount = 0;
	int32 RightCount = 0;
	bool bUsedFrontBackBalance = false;
	bool bUsedTopBottomBalance = false;
	bool bUsedLeftRightBalance = false;
};

namespace MDBarnaclePlacementPolicy
{
	/** 스포너 로컬 +X/-X/+Y/-Y/+Z/-Z 중 허용된 축에 가장 가까운 면을 판정한다. */
	EMD_BarnaclePlacementFace ClassifySurfaceFace(
		const FVector& SurfaceNormal,
		const FVector& ForwardAxis,
		const FVector& RightAxis,
		const FVector& UpAxis,
		bool bAllowFrontBack,
		bool bAllowLeftRight,
		bool bAllowTopBottom);

	/** 좌/우를 사용하지 않는 기존 호출부용 호환 오버로드. */
	EMD_BarnaclePlacementFace ClassifySurfaceFace(
		const FVector& SurfaceNormal,
		const FVector& ForwardAxis,
		const FVector& UpAxis,
		bool bAllowFrontBack,
		bool bAllowTopBottom);

	/** 여섯 면별 거리 하한과 세 대향 면 쌍의 균형을 만족하는 결정론적 배치 계획을 만든다. */
	FMD_BarnaclePlacementPlan BuildPlan(
		const TArray<FMD_BarnaclePlacementCandidate>& Candidates,
		const TArray<FMD_BarnaclePlacementCandidate>& ExistingPlacements,
		int32 RequestedCount,
		float MinimumSeparationCm,
		FRandomStream& RandomStream);
}
