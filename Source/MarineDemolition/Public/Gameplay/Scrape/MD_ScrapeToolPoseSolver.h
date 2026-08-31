// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 헤라 메시의 접촉 소켓을 표면 자세에 맞추기 위한 순수 변환 결과.
 *
 * UObject나 월드 상태를 참조하지 않는다. UE의 FTransform 곱은 왼쪽 로컬
 * 변환을 먼저 적용하므로 SocketLocal * ComponentWorld가 socket world다.
 */
struct MARINEDEMOLITION_API FMD_ScrapeToolPoseSolution
{
	FTransform ContactWorldTransform = FTransform::Identity;
	FTransform ToolWorldTransform = FTransform::Identity;
	FTransform ToolAttachSocketWorldTransform = FTransform::Identity;
	FTransform HandBoneWorldTransform = FTransform::Identity;
};

/** UObject 의존 없이 헤라 접촉 자세와 오른손 IK 목표를 역산한다. */
class MARINEDEMOLITION_API FMD_ScrapeToolPoseSolver final
{
public:
	/**
	 * 입력 normal/tangent를 정규직교화하고 X=normal, Z=stroke tangent인
	 * world contact frame을 만든다. tangent는 180도 동치이므로 이전 유효
	 * tangent와 반대 부호면 뒤집어 프레임별 180도 반전을 막는다.
	 */
	static bool BuildContactWorldTransform(
		const FVector& ContactWorldLocation,
		const FVector& ContactWorldNormal,
		const FVector& StrokeWorldTangent,
		const FVector& PreviousStableWorldTangent,
		const FVector& ContactWorldScale,
		FTransform& OutContactWorldTransform,
		FVector& OutStableStrokeWorldTangent);

	/**
	 * 다음 작성 계약을 역산한다.
	 *
	 *   BladeContactMeshLocal * ToolWorld == ContactWorld
	 *   ToolRelativeToAttachSocket * ToolAttachSocketWorld == ToolWorld
	 *   ToolAttachSocketParentBoneLocal * HandBoneWorld == ToolAttachSocketWorld
	 *
	 * 따라서 기존처럼 도구를 손 소켓에 부착한 채 HandBoneWorld를 IK로
	 * 만들면, 별도의 world-space 도구 이동 없이 날 접촉 소켓이 ContactWorld에
	 * 정확히 놓인다.
	 */
	static bool SolveHandBoneWorldTransform(
		const FTransform& BladeContactMeshLocal,
		const FTransform& ToolRelativeToAttachSocket,
		const FTransform& ToolAttachSocketParentBoneLocal,
		const FTransform& ContactWorldTransform,
		FMD_ScrapeToolPoseSolution& OutSolution);

	/** 실제 두 본 길이를 넘는 stretch와 접을 수 없는 가까운 목표를 거부한다. */
	static bool IsWithinTwoBoneReach(
		const FVector& ShoulderComponentLocation,
		const FVector& TargetHandComponentLocation,
		double UpperArmLength,
		double LowerArmLength,
		double MaximumReachRatio);

	/** 좁은 런타임 검증: 접촉 소켓을 결과 ToolWorld에 합성한 자세를 비교한다. */
	static bool IsContactInvariantSatisfied(
		const FTransform& BladeContactMeshLocal,
		const FTransform& ToolWorldTransform,
		const FTransform& ExpectedContactWorldTransform,
		double LocationToleranceCm = 0.01,
		double RotationToleranceDegrees = 0.05);

private:
	FMD_ScrapeToolPoseSolver() = delete;
};
