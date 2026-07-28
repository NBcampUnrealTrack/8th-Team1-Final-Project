// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MD_CharacterMovementComponent.generated.h"

/**
 *
 */
UCLASS()
class MARINEDEMOLITION_API UMD_CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()


public:
	virtual bool CanEverSwim() const override;

	virtual void PhysSwimming(float deltaTime, int32 Iterations) override;

	void EnterSwimmingFromGround();

	void SetWantsToSwim(bool bInWantsToSwim) { bWantsToSwim = bInWantsToSwim; }

	bool GetWantsToSwim() const { return bWantsToSwim; }

private:
	static constexpr float SwimEntryUpwardVelocity = 200.f;

	bool bWantsToSwim = false;
};
