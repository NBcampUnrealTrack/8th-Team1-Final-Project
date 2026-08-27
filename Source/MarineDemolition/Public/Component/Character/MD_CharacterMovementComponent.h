// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MD_CharacterMovementComponent.generated.h"

class AMD_Ladder;

/** 프로젝트 전용 이동 물리 모드 식별자 */
UENUM(BlueprintType)
enum class EMDCustomMovementMode : uint8
{
	/** 유효한 프로젝트 전용 이동 물리가 없는 상태 */
	None UMETA(Hidden),

	/** 사다리 축을 따라 이동하는 등반 상태 */
	Ladder
};

UCLASS()
class MARINEDEMOLITION_API UMD_CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()


public:
	/** 전용 이동 설정과 복제 기본값을 구성하는 생성자 */
	UMD_CharacterMovementComponent();

	/** 현재 사다리 참조를 네트워크에 등록하는 복제 설정 함수 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 명시적으로 요청된 수영 상태만 허용하는 판정 함수 */
	virtual bool CanEverSwim() const override;

	/** 바닥 접촉 시 걷기로 복귀시키는 수영 물리 함수 */
	virtual void PhysSwimming(float DeltaTime, int32 Iterations) override;

	// 물속에서 접지가 끊기면 수영으로 전환 (기획 02 §0.5 규칙 2)
	virtual void PhysFalling(float DeltaTime, int32 Iterations) override;

	/** 프로젝트 전용 이동 모드에 맞는 물리를 실행하는 분기 함수 */
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

	/** 사다리 상태에서 전용 최대 속도를 반환하는 함수 */
	virtual float GetMaxSpeed() const override;

	/** 사다리 상태에서 전용 가속도를 반환하는 함수 */
	virtual float GetMaxAcceleration() const override;

	/** 사다리 상태에서 전용 제동 감속도를 반환하는 함수 */
	virtual float GetMaxBrakingDeceleration() const override;

	/** 지면에서 수영 상태로 진입시키는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Movement|Swimming")
	void EnterSwimmingFromGround();

	/** 수영 가능 요청 상태를 설정하는 함수 */
	void SetWantsToSwim(bool bInWantsToSwim) { bWantsToSwim = bInWantsToSwim; }

	/** 수영 가능 요청 상태를 반환하는 함수 */
	bool GetWantsToSwim() const { return bWantsToSwim; }

protected:
	/** 외부 이동 모드 전환 시 남아 있는 사다리 상태를 정리하는 함수 */
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

protected:
	/** 물속에서 바닥을 걸을 때 적용할 최대 이동 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Swimming", meta = (ClampMin = "0.0"))
	float MaxUnderwaterWalkSpeed = 200.0f;

	/** 사다리 축을 따라 이동할 수 있는 최대 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float MaxLadderSpeed = 200.0f;

	/** 사다리 입력에 반응하는 가속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderAcceleration = 1024.0f;

	/** 사다리 입력 해제 시 속도를 멈추는 제동 감속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderBrakingDeceleration = 2048.0f;

	/** 캐릭터 캡슐과 사다리 표면 사이에 추가할 여유 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderSurfaceOffset = 5.0f;

	/** 축에서 벗어난 캐릭터를 매초 되돌리는 스냅 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderSnapSpeed = 600.0f;

	/** 상하단 도달 여부에 적용할 위치 허용 오차 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderBoundaryTolerance = 2.0f;

	/** 상단 기준점에서 이탈 기준점으로 이동하는 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderTopDismountSpeed = 250.0f;

	/** 상단 이탈을 완료한 것으로 판정할 거리 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderTopDismountTolerance = 2.0f;

	/** 사다리 점프 시 바깥 방향으로 부여할 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderJumpHorizontalSpeed = 500.0f;

	/** 사다리 점프 시 월드 위 방향으로 부여할 속도 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Movement|Ladder", meta = (ClampMin = "0.0"))
	float LadderJumpVerticalSpeed = 300.0f;

private:
	/** 서버가 확정하고 클라이언트 이동 물리가 참조하는 현재 사다리 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentLadder)
	TObjectPtr<AMD_Ladder> CurrentLadder = nullptr;

	/** 상단 기준점에서 이탈 기준점으로 이동 중인지 나타내는 상태값 */
	UPROPERTY(Transient)
	bool bIsTopDismounting = false;

	/** 상단 이탈 중 사다리 앞쪽 상승 구간을 완료했는지 나타내는 상태값 */
	UPROPERTY(Transient)
	bool bHasReachedTopDismountLift = false;

	/** 사다리 메시 위로 먼저 올라가기 위한 상단 이탈 중간 위치 */
	UPROPERTY(Transient)
	FVector TopDismountLiftLocation = FVector::ZeroVector;

	/** 지면에서 수영을 시작할 때 부여하는 월드 위 방향 속도 */
	static constexpr float SwimEntryUpwardVelocity = 200.0f;

	/** 수영 물리 진입을 명시적으로 허용하는 상태값 */
	bool bWantsToSwim = false;

public:
	/** 지정 사다리로 스냅한 뒤 등반 상태에 진입하는 함수 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Movement|Ladder")
	bool StartClimbing(AMD_Ladder* Ladder);

	/** 사다리 참조를 해제하고 지정된 일반 이동 모드로 전환하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Movement|Ladder")
	void StopClimbing(EMovementMode ExitMovementMode = MOVE_Falling);

	/** 사다리 바깥 방향으로 점프한 뒤 낙하 상태로 전환하는 함수 */
	UFUNCTION(BlueprintCallable, Category = "MD|Movement|Ladder")
	bool JumpOffLadder();

	/** 현재 사다리 등반 물리를 사용 중인지 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Movement|Ladder")
	bool IsClimbing() const;

	/** 상단 자동 이탈 이동 중인지 확인하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Movement|Ladder")
	bool IsTopDismounting() const;

	/** 현재 등반 중인 사다리 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Movement|Ladder")
	FORCEINLINE AMD_Ladder* GetCurrentLadder() const { return CurrentLadder; }

	/** 애니메이션 재생 방향에 사용할 사다리 축 기준 속도 반환 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Movement|Ladder")
	float GetLadderClimbSpeed() const;

	/** 뒤 입력으로 하단에 도달한 캐릭터를 바닥 상태에 맞게 사다리에서 이탈시키는 함수 */
	bool TryLeaveLadderAtBottom(float RequestedClimbSpeed);

protected:
	/** 입력과 속도를 사다리 축으로 제한하는 등반 물리 함수 */
	void PhysLadder(float DeltaTime, int32 Iterations);

	/** 상단 기준점에서 이탈 기준점까지 충돌을 검사하며 이동하는 함수 */
	void PhysLadderTopDismount(float DeltaTime, int32 Iterations);

	/** 지정 사다리로 진입할 수 있는 상태와 공간인지 확인하는 함수 */
	bool CanStartClimbing(const AMD_Ladder* Ladder) const;

	/** 지정 위치까지 캐릭터 캡슐이 충돌 없이 이동할 수 있는지 확인하는 함수 */
	bool CanSweepToLocation(const FVector& TargetLocation) const;

	/** 두 위치 사이를 캐릭터 캡슐이 충돌 없이 이동할 수 있는지 확인하는 함수 */
	bool CanSweepBetweenLocations(const FVector& StartLocation, const FVector& TargetLocation) const;

	/** 현재 사다리와 캐릭터 캡슐 사이의 스냅 간격을 계산하는 함수 */
	float GetLadderStandOffDistance() const;

	/** 상단 이탈 경로를 검증하고 자동 이탈 상태를 시작하는 함수 */
	bool TryStartTopDismount();

	/** 상단 이탈 위치에서 바닥 상태에 맞는 일반 이동 모드로 복귀하는 함수 */
	void FinishTopDismount();

	/** 하단 도달 시 바닥과 물 상태에 맞는 일반 이동 모드로 복귀하는 함수 */
	void LeaveLadderAtBottom();

private:
	/** 서버가 복제한 사다리 참조에 맞춰 클라이언트 이동 모드를 보정하는 함수 */
	UFUNCTION()
	void OnRep_CurrentLadder();
};
