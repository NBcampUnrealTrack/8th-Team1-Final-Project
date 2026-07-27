// Fill out your copyright notice in the Description page of Project Settings.


#include "MarineDemolition/Component/MD_CharacterMovementComponent.h"

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
