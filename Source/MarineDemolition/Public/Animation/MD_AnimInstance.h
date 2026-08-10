// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MD_AnimInstance.generated.h"


class AMD_Character;
class UMD_CharacterMovementComponent;

/**
 * 캐릭터 상태를 애님 블루프린트가 읽을 수 있는 변수로 옮긴다.
 * 판단(어떤 애니메이션을 재생할지)은 ABP 상태머신이 하고, 여기서는 값만 채운다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_AnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	virtual void NativeInitializeAnimation() override;

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|Movement")
	float GroundSpeed = 0.f;

	/** 진행 방향 −180 ~ 180. 좌우·후진 블렌드스페이스용 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|Movement")
	float MovementDirection = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|Movement")
	bool bShouldMove = false;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|Movement")
	bool bIsFalling = false;

	/** 수영 상태. 기획 02 §0.5 — 접지 걷기 / 점프로 수영 전환 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|Movement")
	bool bIsSwimming = false;

	/** 부상. 이동·작업이 느려진 연출용 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|State")
	bool bIsInjured = false;

	/** 사망. 다른 모든 상태를 덮어쓴다 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Anim|State")
	bool bIsDead = false;

private:
	/** 매 프레임 Cast 하지 않으려고 캐시한다 */
	UPROPERTY(Transient)
	TObjectPtr<AMD_Character> OwnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UMD_CharacterMovementComponent> OwnerMovement;

	/** 소유자 캐시를 시도한다. 성공하면 true */
	bool TryCacheOwner();
};
