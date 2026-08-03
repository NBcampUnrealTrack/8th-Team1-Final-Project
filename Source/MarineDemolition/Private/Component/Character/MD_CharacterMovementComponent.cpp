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

void UMD_CharacterMovementComponent::EnterSwimmingFromGround()
{
	bWantsToSwim = true;
	SetMovementMode(MOVE_Swimming);
	Velocity.Z = SwimEntryUpwardVelocity;
}
