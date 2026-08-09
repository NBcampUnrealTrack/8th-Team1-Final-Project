// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Character/MD_CharacterMovementComponent.h"

bool UMD_CharacterMovementComponent::CanEverSwim() const
{
	return bWantsToSwim;
}

void UMD_CharacterMovementComponent::PhysSwimming(float deltaTime, int32 Iterations)
{
	Super::PhysSwimming(deltaTime, Iterations);

	if (MovementMode != MOVE_Swimming)
	{
		return;
	}

	if (Velocity.Z > 0.f)
	{
		return;
	}

	FFindFloorResult FloorResult;
	FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);

	if (FloorResult.IsWalkableFloor())
	{
		SetMovementMode(MOVE_Walking);
	}

}

void UMD_CharacterMovementComponent::PhysFalling(float deltaTime, int32 Iterations)
{
	// 기획 02 §0.5 규칙 2 — 바닥에서 떨어지면 수영한다.
	// CanEverSwim()이 false라 엔진의 자동 수영 전환(PhysicsVolumeChanged 등)이 전부 막혀 있어, 물속 낙하만 여기서 직접 전환한다.

	if (IsInWater() && !IsSwimming())
	{
		bWantsToSwim = true;

		SetMovementMode(MOVE_Swimming);

		//바뀐 무브먼트모드에 따라 물리모드를 수영상태로 다시 돌린다.
		StartNewPhysics(deltaTime, Iterations);
		return;
	}

	Super::PhysFalling(deltaTime, Iterations);
}

void UMD_CharacterMovementComponent::EnterSwimmingFromGround()
{
	bWantsToSwim = true;
	SetMovementMode(MOVE_Swimming);
	Velocity.Z = SwimEntryUpwardVelocity;
}
