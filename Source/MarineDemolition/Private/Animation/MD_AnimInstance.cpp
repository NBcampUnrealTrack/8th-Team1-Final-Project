// Fill out your copyright notice in the Description page of Project Settings.


#include "Animation/MD_AnimInstance.h"

#include "Character/MD_Character.h"
#include "Component/Character/MD_CharacterMovementComponent.h"
#include "Player/MD_PlayerState.h"

namespace MD_AnimThresholds
{
	/** 이 속도 미만이면 정지로 본다. 경사·물리 접촉으로 인한 미세 이동을 걸러낸다 */
	static constexpr float MinMoveSpeed = 3.0f;
}

void UMD_AnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	TryCacheOwner();
}

void UMD_AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!IsValid(OwnerCharacter) || !IsValid(OwnerMovement))
	{
		if(!TryCacheOwner())
		return;
	}

	OwnerVelocity = OwnerMovement->Velocity;

	GroundSpeed = OwnerVelocity.Size2D();

	const float GroundAcceleration = OwnerMovement->GetCurrentAcceleration().Size2D();

	bShouldMove = (GroundSpeed > MD_AnimThresholds::MinMoveSpeed) && (GroundAcceleration > 0.0f);

	bIsFalling = OwnerMovement->IsFalling();

	// AMD_Character가 이미 걷기/수영을 판정해 두었으므로 그대로 읽는다.
	bIsSwimming = (OwnerCharacter->GetCurrentMovementState() == EMDMovementState::Swimming);

	// 상태는 PlayerState가 HP에서 계산한다 (기획 02 §6)
	if (const AMD_PlayerState* MDPlayerState = OwnerCharacter->GetPlayerState<AMD_PlayerState>())
	{
		const EMD_HealthState HealthState = MDPlayerState->GetHealthState();

		bIsInjured = (HealthState == EMD_HealthState::Injured);
		bIsDead = (HealthState == EMD_HealthState::Dead);
	}
}

bool UMD_AnimInstance::TryCacheOwner()
{
	OwnerCharacter = Cast<AMD_Character>(TryGetPawnOwner());

	if (!IsValid(OwnerCharacter))
	{
		return false;
	}

	OwnerMovement = Cast<UMD_CharacterMovementComponent>(OwnerCharacter->GetCharacterMovement());

	return IsValid(OwnerMovement);
}
