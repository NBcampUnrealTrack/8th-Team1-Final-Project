// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Character/MD_CharacterMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "World/Traversal/MD_Ladder.h"

UMD_CharacterMovementComponent::UMD_CharacterMovementComponent()
{
	// 현재 사다리 참조를 소유 캐릭터와 함께 복제하기 위한 기본 설정 활성화
	SetIsReplicatedByDefault(true);
}

void UMD_CharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	// 부모 이동 컴포넌트의 복제 속성 등록 보장
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 클라이언트가 동일한 사다리 축으로 이동하도록 현재 사다리 참조 등록
	DOREPLIFETIME(UMD_CharacterMovementComponent, CurrentLadder);
}

bool UMD_CharacterMovementComponent::CanEverSwim() const
{
	return bWantsToSwim;
}

void UMD_CharacterMovementComponent::PhysSwimming(float DeltaTime, int32 Iterations)
{
	Super::PhysSwimming(DeltaTime, Iterations);

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

void UMD_CharacterMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	// 기획 02 §0.5 규칙 2 — 바닥에서 떨어지면 수영한다.
	// CanEverSwim()이 false라 엔진의 자동 수영 전환(PhysicsVolumeChanged 등)이 전부 막혀 있어, 물속 낙하만 여기서 직접 전환한다.

	if (IsInWater() && !IsSwimming())
	{
		bWantsToSwim = true;

		SetMovementMode(MOVE_Swimming);

		//바뀐 무브먼트모드에 따라 물리모드를 수영상태로 다시 돌린다.
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	Super::PhysFalling(DeltaTime, Iterations);
}

void UMD_CharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	// 사다리 식별자를 사용하는 전용 등반 물리 실행
	if (CustomMovementMode == static_cast<uint8>(EMDCustomMovementMode::Ladder))
	{
		PhysLadder(DeltaTime, Iterations);
		return;
	}

	// 알 수 없는 프로젝트 전용 모드의 부모 처리 보존
	Super::PhysCustom(DeltaTime, Iterations);
}

void UMD_CharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	// 부모 이동 컴포넌트의 바닥 정보와 캐릭터 알림 갱신 보장
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	// 직전 이동 상태가 사다리 전용 모드였는지 확인하는 판정
	bool bWasClimbing = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == static_cast<uint8>(EMDCustomMovementMode::Ladder);
	bool bIsStillClimbing = MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EMDCustomMovementMode::Ladder);

	// 사다리 상태를 유지하는 동일 모드 전환의 참조 보존
	if (bWasClimbing == false || bIsStillClimbing == true)
	{
		return;
	}

	// 강제 걷기와 사망 잠금에서도 남지 않도록 모든 사다리 임시 상태 정리
	CurrentLadder = nullptr;
	bIsTopDismounting = false;
	bHasReachedTopDismountLift = false;
	TopDismountLiftLocation = FVector::ZeroVector;
}

float UMD_CharacterMovementComponent::GetMaxSpeed() const
{
	// 사다리 상태의 전용 최대 속도 반환
	if (IsClimbing() == true)
	{
		return MaxLadderSpeed;
	}

	// 일반 이동과 수영의 기존 최대 속도 반환
	return Super::GetMaxSpeed();
}

float UMD_CharacterMovementComponent::GetMaxAcceleration() const
{
	// 사다리 상태의 전용 가속도 반환
	if (IsClimbing() == true)
	{
		return LadderAcceleration;
	}

	// 일반 이동과 수영의 기존 가속도 반환
	return Super::GetMaxAcceleration();
}

float UMD_CharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	// 사다리 상태의 전용 제동 감속도 반환
	if (IsClimbing() == true)
	{
		return LadderBrakingDeceleration;
	}

	// 일반 이동과 수영의 기존 제동 감속도 반환
	return Super::GetMaxBrakingDeceleration();
}

void UMD_CharacterMovementComponent::EnterSwimmingFromGround()
{
	bWantsToSwim = true;
	SetMovementMode(MOVE_Swimming);
	Velocity.Z = SwimEntryUpwardVelocity;
}

bool UMD_CharacterMovementComponent::StartClimbing(AMD_Ladder* Ladder)
{
	// 동일한 사다리 등반 요청의 중복 위치 보정 방지
	if (IsClimbing() == true && CurrentLadder == Ladder)
	{
		return true;
	}

	// 상태와 공간 검증에 실패한 사다리 진입 차단
	if (CanStartClimbing(Ladder) == false)
	{
		return false;
	}

	// 캐릭터 캡슐 반지름을 반영하여 사다리를 타는 경우 캐릭터의 위치 계산
	FVector SnapLocation = Ladder->GetClosestClimbLocation(UpdatedComponent->GetComponentLocation(), GetLadderStandOffDistance());

	// 진입 경로가 막힌 경우 벽이나 바닥 내부로 들어가는 스냅 차단
	if (CanSweepToLocation(SnapLocation) == false)
	{
		return false;
	}

	// 수영 자동 전환이 사다리 물리를 덮어쓰지 않도록 수영 요청 해제
	bWantsToSwim = false;

	// 서버와 클라이언트가 동일한 등반 축을 사용하도록 현재 사다리 저장
	CurrentLadder = Ladder;

	// 이전 이동 속도가 사다리 진입 직후 튀는 현상을 막기 위한 속도 초기화
	Velocity = FVector::ZeroVector;
	Acceleration = FVector::ZeroVector;
	bIsTopDismounting = false;
	bHasReachedTopDismountLift = false;
	TopDismountLiftLocation = FVector::ZeroVector;

	// 사다리 축 위치까지 Sweep 방식으로 캐릭터 이동
	FHitResult SnapHit;
	SafeMoveUpdatedComponent(SnapLocation - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, SnapHit);

	// MovementMode를 Custom으로 설정 (중력 미적용)
	SetMovementMode(MOVE_Custom, static_cast<uint8>(EMDCustomMovementMode::Ladder));
	return true;
}

void UMD_CharacterMovementComponent::StopClimbing(EMovementMode ExitMovementMode)
{
	// 사용자 정의 이동 모드로 재진입하여 사다리 참조가 없는 상태가 되는 문제 방지
	EMovementMode SafeExitMovementMode = ExitMovementMode == MOVE_Custom ? MOVE_Falling : ExitMovementMode;

	// 상단 이탈을 포함한 사다리 전용 임시 상태 초기화
	bIsTopDismounting = false;
	bHasReachedTopDismountLift = false;
	TopDismountLiftLocation = FVector::ZeroVector;
	CurrentLadder = nullptr;
	bForceNextFloorCheck = true;	// 사다리 타기 중지 시점에 강제로 바닥을 체크하도록 설정

	// 실제 사다리 전용 이동 모드를 사용 중인 경우에만 일반 이동 모드 복귀
	if (MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EMDCustomMovementMode::Ladder))
	{
		SetMovementMode(SafeExitMovementMode);
	}
}

bool UMD_CharacterMovementComponent::JumpOffLadder()
{
	// 사다리 밖 점프에 사용할 현재 등반 대상 검증
	if (IsClimbing() == false)
	{
		return false;
	}

	// 사다리에서 지정한 점프 방향 얻기
	FVector JumpDirection = CurrentLadder->GetOutwardVector();

	// 사다리 타기 중지
	StopClimbing(MOVE_Falling);

	// 현재 속도에 사다리에서의 점프 속도 적용
	Velocity = JumpDirection * LadderJumpHorizontalSpeed + FVector::UpVector * LadderJumpVerticalSpeed;
	return true;
}

bool UMD_CharacterMovementComponent::IsClimbing() const
{
	// 이동 모드와 사다리 참조가 모두 유효한 경우에만 등반 상태 인정
	if (MovementMode != MOVE_Custom)
	{
		return false;
	}

	if (CustomMovementMode != static_cast<uint8>(EMDCustomMovementMode::Ladder))
	{
		return false;
	}

	if (IsValid(CurrentLadder) == false)
	{
		return false;
	}
	return true;
}

bool UMD_CharacterMovementComponent::IsTopDismounting() const
{
	// 상단 자동 이동이 실제 사다리 상태에서만 유효하도록 판정
	return IsClimbing() == true && bIsTopDismounting == true;
}

float UMD_CharacterMovementComponent::GetLadderClimbSpeed() const
{
	// 사다리를 타고있지 않은 경우 0 반환
	if (IsClimbing() == false)
	{
		return 0.0f;
	}

	// 현재 이동 속도의 사다리 진행 방향(위쪽) 성분 계산
	float UpwardSpeedOnLadder = FVector::DotProduct(Velocity, CurrentLadder->GetClimbUpVector());
	return UpwardSpeedOnLadder;
}

void UMD_CharacterMovementComponent::PhysLadder(float DeltaTime, int32 Iterations)
{
	// 이동 시간이 매우 짧거나 이동을 적용할 캐릭터 또는 컴포넌트가 유효하지 않으면 함수 조기 종료
	if (DeltaTime < MIN_TICK_TIME || HasValidData() == false)
	{
		return;
	}

	// 물리 시뮬레이션이 캐릭터 캡슐을 소유한 경우 이동 컴포넌트 계산 차단
	if (UpdatedComponent->IsSimulatingPhysics() == true)
	{
		return;
	}

	// 현재 등반하고 있는 사다리가 유효하지 않은 경우 등반 중단;
	if (IsValid(CurrentLadder) == false)
	{
		StopClimbing(MOVE_Falling);

		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	// 상단 자동 이탈 중에는 등반 입력 대신 목표 위치 이동 물리 실행
	if (bIsTopDismounting == true)
	{
		PhysLadderTopDismount(DeltaTime, Iterations);
		return;
	}
	// 사다리 타는 중

	// 횡이동과 중력 속도를 제거하기 위한 사다리 축 계산
	FVector ClimbUpVector = CurrentLadder->GetClimbUpVector();
	Acceleration = ClimbUpVector * FVector::DotProduct(Acceleration, ClimbUpVector);
	Velocity = ClimbUpVector * FVector::DotProduct(Velocity, ClimbUpVector);

	// 전용 최대 속도와 제동값을 사용하는 사다리 축 속도 계산
	CalcVelocity(DeltaTime, 0.0f, false, GetMaxBrakingDeceleration());
	float UpwardSpeedOnLadder = FVector::DotProduct(Velocity, ClimbUpVector);
	Velocity = ClimbUpVector * FMath::Clamp(UpwardSpeedOnLadder, -MaxLadderSpeed, MaxLadderSpeed);

	// 경계 도달 방향 판정에 사용할 요청 속도 보존
	float RequestedClimbSpeed = FVector::DotProduct(Velocity, ClimbUpVector);
	FVector CurrentLocation = UpdatedComponent->GetComponentLocation();

	// 프레임 간 축 오차를 서서히 제거하는 스냅 보정량 계산
	FVector SnapLocation = CurrentLadder->GetClosestClimbLocation(CurrentLocation, GetLadderStandOffDistance());
	// 사다리를 타는 위치를 향하여 보정되어야 하는 위치 값 차이(최대 값 제한 적용)
	FVector SnapCorrection = (SnapLocation - CurrentLocation).GetClampedToMaxSize(LadderSnapSpeed * DeltaTime);

	// 이동 예정 위치를 사다리 선분 안으로 제한하여 상하단 초과 방지
	FVector UnclampedTargetLocation = CurrentLocation + Velocity * DeltaTime + SnapCorrection;	// 이동 예정 위치(미 보정)
	FVector ClampedTargetLocation = CurrentLadder->GetClosestClimbLocation(UnclampedTargetLocation, GetLadderStandOffDistance()); // 이동 예정 위치(보정됨)
	FVector MoveDelta = ClampedTargetLocation - CurrentLocation;	// 이동되는 변화량

	// Sweep 방식으로 사다리 축을 따라 이동
	FHitResult MoveHit;
	SafeMoveUpdatedComponent(MoveDelta, UpdatedComponent->GetComponentQuat(), true, MoveHit);

	// 이동 경로를 막은 충돌을 게임플레이 충돌 처리에 전달하고 속도 제거
	if (MoveHit.IsValidBlockingHit() == true)
	{
		HandleImpact(MoveHit, DeltaTime, MoveDelta);
		Velocity = FVector::ZeroVector;
	}

	// 실제 이동 결과를 사다리 축 속도로 환산하여 네트워크 보정 오차 축소
	if (bJustTeleported == false && DeltaTime > MIN_TICK_TIME)
	{
		// 실제 위치 변화량을 시간으로 나누어 실제 속도 계산
		FVector ActualMoveVelocity = (UpdatedComponent->GetComponentLocation() - CurrentLocation) / DeltaTime;

		// 등반 방향의 속도 성분 계산 및 적용
		Velocity = ClimbUpVector * FVector::DotProduct(ActualMoveVelocity, ClimbUpVector);
	}

	// 상하단 판정에 사용할 사다리 길이와 정규화 허용 오차 계산
	float ClimbLength = FMath::Max(CurrentLadder->GetClimbLength(), 1.0f);
	float BoundaryProgressTolerance = FMath::Clamp(LadderBoundaryTolerance / ClimbLength, 0.0f, 1.0f);
	float ClimbProgress = CurrentLadder->GetClimbProgress(UpdatedComponent->GetComponentLocation());

	// 위쪽 입력으로 상단에 도달한 경우 충돌 안전 경로 기반 자동 이탈 시작
	if (RequestedClimbSpeed > 0.0f && ClimbProgress >= 1.0f - BoundaryProgressTolerance)
	{
		TryStartTopDismount();
		return;
	}

	// 아래쪽 입력으로 하단에 도달한 경우 바닥, 추락 또는 수영 상태 복귀
	if (RequestedClimbSpeed < 0.0f && ClimbProgress <= BoundaryProgressTolerance)
	{
		LeaveLadderAtBottom();
	}
}

void UMD_CharacterMovementComponent::PhysLadderTopDismount(float DeltaTime, int32 Iterations)
{
	// 사용중인 사다리가 없거나 이동을 적용할 캐릭터 또는 컴포넌트가 유효하지 등반 중단 및 낙하 상태로 변경
	if (IsValid(CurrentLadder) == false || HasValidData() == false)
	{
		StopClimbing(MOVE_Falling);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	// 상단 이탈 기준점까지 남은 위치 차이와 거리 계산
	FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	FVector TargetLocation = bHasReachedTopDismountLift == true ? CurrentLadder->GetTopExitPointLocation() : TopDismountLiftLocation;
	FVector DeltaLocation = TargetLocation - CurrentLocation;
	float TargetDistance = DeltaLocation.Size();

	// 남은 거리가 매우 짧을 경우 사다리 등반 완료 처리
	if (TargetDistance <= LadderTopDismountTolerance)
	{
		// 상승 구간 완료 후 사다리 최종 이탈 구간 이동으로 전환
		if (bHasReachedTopDismountLift == false)
		{
			bHasReachedTopDismountLift = true;
			Velocity = FVector::ZeroVector;
			return;
		}

		FinishTopDismount();
		return;
	}

	// 프레임 속도와 무관한 일정 속도의 상단 이탈 이동량 계산
	float MoveDistance = FMath::Min(LadderTopDismountSpeed * DeltaTime, TargetDistance);
	FVector MoveDelta = DeltaLocation.GetSafeNormal() * MoveDistance;

	// Sweep 방식으로 사다리 이동 처리 종료 지점까지 이동
	FHitResult MoveHit;
	SafeMoveUpdatedComponent(MoveDelta, UpdatedComponent->GetComponentQuat(), true, MoveHit);

	// 이동 중 새로 발생한 장애물에 캐릭터가 끼지 않도록 자동 이동 중단
	if (MoveHit.IsValidBlockingHit() == true)
	{
		// 충돌 처리 실행
		HandleImpact(MoveHit, DeltaTime, MoveDelta);
		bIsTopDismounting = false;
		bHasReachedTopDismountLift = false;
		TopDismountLiftLocation = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	// 실제 이동 속도 갱신
	Velocity = MoveDelta / DeltaTime;

	// 목표 지점과 매우 가까운 지점에 도달한 경우 사다리 등반 완료 처리
	if (FVector::DistSquared(UpdatedComponent->GetComponentLocation(), TargetLocation) <= FMath::Square(LadderTopDismountTolerance))
	{
		// 상승 구간 완료 후 사다리 최종 이탈 구간 이동으로 전환
		if (bHasReachedTopDismountLift == false)
		{
			bHasReachedTopDismountLift = true;
			Velocity = FVector::ZeroVector;
			return;
		}

		FinishTopDismount();
	}
}

bool UMD_CharacterMovementComponent::CanStartClimbing(const AMD_Ladder* Ladder) const
{
	// 사다리와 캐릭터 이동 데이터가 없는 진입 요청 거부
	if (IsValid(Ladder) == false || IsValid(CharacterOwner) == false || IsValid(UpdatedComponent) == false)
	{
		return false;
	}

	// 사망 등으로 이동이 비활성화된 캐릭터의 등반 진입 거부
	if (MovementMode == MOVE_None)
	{
		return false;
	}

	// 다른 사다리를 등반 중인 캐릭터의 대상 교체 방지
	if (IsValid(CurrentLadder) == true && CurrentLadder != Ladder)
	{
		return false;
	}

	// 서버와 예측 클라이언트 모두 실제 감지 영역 내부에서만 진입 허용
	return Ladder->IsCharacterInsideTrigger(CharacterOwner) == true;
}

bool UMD_CharacterMovementComponent::CanSweepToLocation(const FVector& TargetLocation) const
{
	// 현재 캐릭터 위치를 시작점으로 사용하는 경로 검사 결과 반환
	if (IsValid(UpdatedComponent) == false)
	{
		return false;
	}

	// 현재 위치와 목표 위치 사이에서 캐릭터 Collision의 Sweep 충돌 테스트
	bool bCanSweep = CanSweepBetweenLocations(UpdatedComponent->GetComponentLocation(), TargetLocation);

	return bCanSweep;
}

bool UMD_CharacterMovementComponent::CanSweepBetweenLocations(const FVector& StartLocation, const FVector& TargetLocation) const
{
	// 충돌 검사에 필요한 월드와 캡슐 데이터 검증
	UWorld* World = GetWorld();
	UCapsuleComponent* CapsuleComponent = IsValid(CharacterOwner) == true ? CharacterOwner->GetCapsuleComponent() : nullptr;

	// 필수 충돌 데이터가 없는 위치 이동 거부
	if (IsValid(World) == false || IsValid(CapsuleComponent) == false || IsValid(UpdatedComponent) == false)
	{
		return false;
	}

	// 현재 캐릭터를 제외한 이동 경로 검사 설정 구성
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MDLadderMovementSweep), false, CharacterOwner);
	FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleComponent->GetScaledCapsuleRadius(), CapsuleComponent->GetScaledCapsuleHalfHeight());

	// 캐릭터 캡슐 전체 경로에 차단 충돌이 없는 경우에만 이동 허용
	return World->SweepTestByChannel(StartLocation, TargetLocation, UpdatedComponent->GetComponentQuat(), CapsuleComponent->GetCollisionObjectType(), CapsuleShape, QueryParams) == false;
}

float UMD_CharacterMovementComponent::GetLadderStandOffDistance() const
{
	// 캡슐 데이터가 없는 초기화 구간의 최소 표면 간격 반환
	if (IsValid(CharacterOwner) == false || IsValid(CharacterOwner->GetCapsuleComponent()) == false)
	{
		return LadderSurfaceOffset;
	}

	// 캡슐이 사다리 메시와 겹치지 않도록 반지름과 추가 여유 거리 결합
	return CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() + LadderSurfaceOffset;
}

bool UMD_CharacterMovementComponent::TryStartTopDismount()
{
	// 상단 이탈에 필요한 사다리와 이동 데이터 검증
	if (IsClimbing() == false || IsValid(UpdatedComponent) == false)
	{
		return false;
	}

	// 사다리 앞쪽에서 출구 높이까지 먼저 올라가는 중간 위치 계산
	FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	FVector TopExitLocation = CurrentLadder->GetTopExitPointLocation();
	FVector ClimbUpVector = CurrentLadder->GetClimbUpVector();	// 이동 방향

	float LiftDistance = FMath::Max(FVector::DotProduct(TopExitLocation - CurrentLocation, ClimbUpVector), 0.0f);	// 사다리 위쪽 방향 남은 거리
	TopDismountLiftLocation = CurrentLocation + ClimbUpVector * LiftDistance;
	bool bRequiresLiftSweep = FVector::DistSquared(CurrentLocation, TopDismountLiftLocation) > FMath::Square(LadderTopDismountTolerance);	// 사다리 위쪽 방향을 이동 보정 필요성(추가 상승 구간 필요성) 확인

	// 상승 구간 또는 최종 이탈 구간이 막힌 경우 현재 사다리 위치 유지
	if ((bRequiresLiftSweep == true && CanSweepBetweenLocations(CurrentLocation, TopDismountLiftLocation) == false) || CanSweepBetweenLocations(TopDismountLiftLocation, TopExitLocation) == false)
	{
		Velocity = FVector::ZeroVector;
		TopDismountLiftLocation = FVector::ZeroVector;
		return false;
	}

	// 사용자 입력을 잠그고 자동 상단 이동을 시작하는 상태 설정
	bIsTopDismounting = true;
	bHasReachedTopDismountLift = FVector::DistSquared(CurrentLocation, TopDismountLiftLocation) <= FMath::Square(LadderTopDismountTolerance);
	Velocity = FVector::ZeroVector;
	Acceleration = FVector::ZeroVector;
	return true;
}

void UMD_CharacterMovementComponent::FinishTopDismount()
{
	// 바닥 찾기
	FFindFloorResult FloorResult;
	FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);

	// 물 속에 위치한 경우 사다리 등반 중지 및 수영 상태로 전환
	if (IsInWater() == true)
	{
		StopClimbing(MOVE_Falling);
		bWantsToSwim = true;
		SetMovementMode(MOVE_Swimming);
		return;
	}

	// 걸을 수 있는 바닥이 존재하지 않는 경우 낙하 상태로 변경
	EMovementMode ExitMovementMode = FloorResult.IsWalkableFloor() == true ? MOVE_Walking : MOVE_Falling;
	StopClimbing(ExitMovementMode);
}

void UMD_CharacterMovementComponent::LeaveLadderAtBottom()
{
	// 바닥 찾기
	FFindFloorResult FloorResult;
	FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);

	// 물 속에 위치한 경우 사다리 등반 중지 및 수영 상태로 전환
	if (IsInWater() == true)
	{
		StopClimbing(MOVE_Falling);
		bWantsToSwim = true;
		SetMovementMode(MOVE_Swimming);
		return;
	}

	// 걸을 수 있는 바닥이 존재하지 않는 경우 낙하 상태로 변경
	EMovementMode ExitMovementMode = FloorResult.IsWalkableFloor() == true ? MOVE_Walking : MOVE_Falling;
	StopClimbing(ExitMovementMode);
}

void UMD_CharacterMovementComponent::OnRep_CurrentLadder()
{
	// 유효한 사다리가 설정된 경우 사다리 타기 모드로 변경
	if (IsValid(CurrentLadder) == true)
	{
		bWantsToSwim = false;
		bIsTopDismounting = false;
		bHasReachedTopDismountLift = false;
		TopDismountLiftLocation = FVector::ZeroVector;

		if (MovementMode != MOVE_None && (MovementMode != MOVE_Custom || CustomMovementMode != static_cast<uint8>(EMDCustomMovementMode::Ladder)))
		{
			Velocity = FVector::ZeroVector;
			SetMovementMode(MOVE_Custom, static_cast<uint8>(EMDCustomMovementMode::Ladder));
		}

		return;
	}

	// 유효하지 않은 사다리로 변경된 경우 사다리 등반 중단 및 추락 상태로 변경
	if (MovementMode == MOVE_Custom && CustomMovementMode == static_cast<uint8>(EMDCustomMovementMode::Ladder))
	{
		bIsTopDismounting = false;
		bHasReachedTopDismountLift = false;
		TopDismountLiftLocation = FVector::ZeroVector;
		SetMovementMode(MOVE_Falling);
	}
}
