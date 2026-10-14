#include "Component/Facility/MD_FacilityFaultComponent.h"

#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

const FName UMD_FacilityFaultComponent::FaultExemptActorTag(TEXT("MD.Facility.FaultExempt"));

bool UMD_FacilityFaultComponent::IsFaultExempt() const
{
	return IsValid(GetOwner()) && GetOwner()->ActorHasTag(FaultExemptActorTag);
}

UMD_FacilityFaultComponent::UMD_FacilityFaultComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMD_FacilityFaultComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_FacilityFaultComponent, Snapshot);
}

bool UMD_FacilityFaultComponent::HasServerAuthority() const
{
	return IsValid(GetOwner()) && GetOwner()->HasAuthority() && GetWorld()
		&& GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client;
}

double UMD_FacilityFaultComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0.0;
	const AGameStateBase* GS = World->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void UMD_FacilityFaultComponent::BeginPlay()
{
	Super::BeginPlay();
	if (HasServerAuthority())
	{
		RegisterWithManager();
		if (bStartBroken) TryBreak();
		// 서브레벨 재진입 시 이미 고장 난 시설의 비용 시계 재연결
		UpdateFaultCostState(Snapshot);
	}
}

void UMD_FacilityFaultComponent::RegisterWithManager()
{
	if (!HasServerAuthority() || Manager.IsValid()) return;
	AGameStateBase* GS = GetWorld()->GetGameState();
	UMD_FacilityFaultManagerComponent* Found = GS ? GS->FindComponentByClass<UMD_FacilityFaultManagerComponent>() : nullptr;
	if (Found && Found->RegisterFacility(this))
	{
		Manager = Found;
		GetWorld()->GetTimerManager().ClearTimer(RegistrationTimer);
	}
	else if (!GetWorld()->GetTimerManager().IsTimerActive(RegistrationTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(RegistrationTimer, this,
			&UMD_FacilityFaultComponent::RegisterWithManager, 0.5f, true);
	}
}

void UMD_FacilityFaultComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// 제거된 시설의 비용 시계와 사건 연결 해제
	CostFaultId = 0;
	// 월드가 남아 있는 종료 단계에서 차감 예약 정리
	if (IsValid(GetWorld()) == true)
	{
		// 다른 시설의 예약을 유지한 현재 시설의 청구 중단
		GetWorld()->GetTimerManager().ClearTimer(FaultCostTimer);
	}
	MDAudioFeedback::Stop(this, RepairAudio, 0.f);
	MDAudioFeedback::Stop(this, FaultAudio, 0.f);
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RegistrationTimer);
	if (Manager.IsValid()) Manager->UnregisterFacility(this);
	Manager.Reset();
	Super::EndPlay(Reason);
}

bool UMD_FacilityFaultComponent::CanReceiveRandomFault() const
{
	return HasServerAuthority() && !IsFaultExempt() && bRandomFaultEligible && !Snapshot.IsFaultActive()
		&& FMath::IsFinite(RefaultGraceSeconds) && RefaultGraceSeconds >= 0.f
		&& (Snapshot.State != EMD_FacilityState::Repaired
			|| GetServerTime() - Snapshot.RepairedAt >= RefaultGraceSeconds);
}

bool UMD_FacilityFaultComponent::TryBreak()
{
	return TryBreakInternal(false);
}

bool UMD_FacilityFaultComponent::TryBreakInternal(bool bSkipDeviceCondition)
{
	// 추가 효과가 없는 기존 파손 요청을 공통 확정으로 전달
	return TryBreakInternalWithEffects(bSkipDeviceCondition, []() {});
}

bool UMD_FacilityFaultComponent::TryBreakWithEffects(TFunctionRef<void()> Effects)
{
	// 장치 조건을 유지한 파손 효과 연결
	return TryBreakInternalWithEffects(false, Effects);
}

bool UMD_FacilityFaultComponent::TryBreakInternalWithEffects(bool bSkipDeviceCondition, TFunctionRef<void()> Effects)
{
	// 서버 전이와 재진입 여부 확인
	if (HasServerAuthority() == false || bStateTransitionInProgress == true)
	{
		// 기존 사건의 확정 중 중첩 변경 거부
		return false;
	}
	// 장치 조건 평가부터 효과와 알림 종료까지 전이 보호
	TGuardValue<bool> TransitionGuard(bStateTransitionInProgress, true);
	// 고장 제외 시설과 진행 중인 사건 차단
	if (IsFaultExempt() == true || Snapshot.IsFaultActive() == true)
	{
		// 중복 또는 제외 시설의 파손 거부
		return false;
	}
	// 일반 요청에만 장치별 파손 안전 조건 적용
	if (bSkipDeviceCondition == false && CanStartFault() == false)
	{
		// 장치 조건 미충족 파손 거부
		return false;
	}
	// 새 사건 번호와 상태 방송에 사용할 이전 상태 보관
	FMD_FacilitySnapshot Previous = Snapshot;
	// 이전 사건의 작업자와 진척도 초기화
	Snapshot = FMD_FacilitySnapshot();
	// 최대 정수 이후에도 유효한 양의 사건 번호 유지
	Snapshot.FaultId = Previous.FaultId == MAX_int32 ? 1 : Previous.FaultId + 1;
	// 신규 파손 상태 확정
	Snapshot.State = EMD_FacilityState::Broken;
	// 서버 기준 파손 시각 기록
	Snapshot.FaultStartedAt = GetServerTime();
	// 확정된 사건에 대한 장치 효과 실행
	Effects();
	// 효과와 상태가 일치하는 결과 방송
	PublishChange(Previous);
	// 신규 사건 확정 성공 전달
	return true;
}

bool UMD_FacilityFaultComponent::BeginRepair(AActor* Repairer, int32 ExpectedFaultId)
{
	// 서버와 유효한 동일 월드 작업자 확인
	if (HasServerAuthority() == false || IsValid(Repairer) == false || Repairer->GetWorld() != GetWorld())
	{
		// 권한 또는 작업자 오류 거부
		return false;
	}
	// 현재 고장 사건과 진행 중인 전이 확인
	if (bStateTransitionInProgress == true || Snapshot.IsFaultActive() == false || Snapshot.FaultId != ExpectedFaultId)
	{
		// 중첩 변경과 오래된 사건 요청 거부
		return false;
	}
	// 유효한 작업자가 이미 점유한 수리의 중복 시작 차단
	if (Snapshot.State == EMD_FacilityState::Repairing && IsValid(Snapshot.Repairer) == true)
	{
		// 기존 작업자의 수리 유지
		return false;
	}
	// 상태 방송까지 수리 시작 전이 보호
	TGuardValue<bool> TransitionGuard(bStateTransitionInProgress, true);
	// 상태 방송용 이전 사건 보관
	FMD_FacilitySnapshot Previous = Snapshot;
	// 수리 진행 상태 확정
	Snapshot.State = EMD_FacilityState::Repairing;
	// 현재 사건 작업자 확정
	Snapshot.Repairer = Repairer;
	// 확정된 작업 상태 방송
	PublishChange(Previous);
	// 수리 시작 성공 전달
	return true;
}

bool UMD_FacilityFaultComponent::AddRepairProgress(AActor* Repairer, int32 ExpectedFaultId, float ProgressDelta)
{
	// 서버와 작업자 및 현재 전이 확인
	if (HasServerAuthority() == false || IsValid(Repairer) == false || bStateTransitionInProgress == true)
	{
		// 무효 작업과 중첩 진척 처리 차단
		return false;
	}
	// 현재 사건을 점유한 작업자의 요청만 허용
	if (Snapshot.State != EMD_FacilityState::Repairing || Snapshot.FaultId != ExpectedFaultId || Snapshot.Repairer != Repairer)
	{
		// 다른 사건 또는 작업자의 진척 거부
		return false;
	}
	// 유한한 양의 작업량만 허용
	if (FMath::IsFinite(ProgressDelta) == false || ProgressDelta <= 0.f)
	{
		// 무효 작업량 거부
		return false;
	}
	// 완료 효과와 상태 방송까지 진척 전이 보호
	TGuardValue<bool> TransitionGuard(bStateTransitionInProgress, true);
	// 상태 방송용 이전 진척 보관
	FMD_FacilitySnapshot Previous = Snapshot;
	// 합산 전 작업량 제한으로 유한 값의 덧셈 오버플로 방지
	Snapshot.RepairProgress = FMath::Min(1.f, Snapshot.RepairProgress + FMath::Min(ProgressDelta, 1.f));
	// 최대 진척 도달 시 같은 사건의 복구 확정
	if (Snapshot.RepairProgress >= 1.f)
	{
		// 복구 완료 상태 반영
		Snapshot.State = EMD_FacilityState::Repaired;
		// 서버 기준 복구 시각 기록
		Snapshot.RepairedAt = GetServerTime();
		// 완료된 작업자 점유 해제
		Snapshot.Repairer = nullptr;
	}
	// 확정된 진척 또는 복구 상태 방송
	PublishChange(Previous);
	// 작업량 반영 성공 전달
	return true;
}

bool UMD_FacilityFaultComponent::CancelRepair(AActor* Repairer, int32 ExpectedFaultId)
{
	// 서버와 현재 수리 전이 확인
	if (HasServerAuthority() == false || bStateTransitionInProgress == true || Snapshot.State != EMD_FacilityState::Repairing)
	{
		// 무효 취소와 중첩 변경 차단
		return false;
	}
	// 동일 사건 작업자의 취소만 허용
	if (Snapshot.FaultId != ExpectedFaultId || Snapshot.Repairer != Repairer)
	{
		// 다른 작업의 취소 거부
		return false;
	}
	// 취소 알림까지 사건 전이 보호
	TGuardValue<bool> TransitionGuard(bStateTransitionInProgress, true);
	// 상태 방송용 이전 작업 상태 보관
	FMD_FacilitySnapshot Previous = Snapshot;
	// 수리 중단 후 고장 상태 복귀
	Snapshot.State = EMD_FacilityState::Broken;
	// 중단된 작업자 점유 해제
	Snapshot.Repairer = nullptr;
	// 시설에 설정된 취소 시 초기화 규칙 적용
	if (bResetProgressOnCancel == true)
	{
		// 초기화 대상 시설의 진척 제거
		Snapshot.RepairProgress = 0.f;
	}
	// 확정된 취소 상태 방송
	PublishChange(Previous);
	// 취소 성공 전달
	return true;
}

void UMD_FacilityFaultComponent::PublishChange(const FMD_FacilitySnapshot& Previous)
{
	// 기존 상태 확정에 맞춰 해당 사건의 Credit 차감 갱신
	UpdateFaultCostState(Previous);
	// Credit 변경 알림에서 제거된 소유자의 후속 접근 차단
	if (IsValid(GetOwner()) == false || GetOwner()->IsActorBeingDestroyed() == true)
	{
		// 종료된 시설의 복제 및 상태 알림 중단
		return;
	}
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
	OnRep_Snapshot(Previous); // Listen server receives the same presentation event as clients.
}

void UMD_FacilityFaultComponent::UpdateFaultCostState(FMD_FacilitySnapshot Previous)
{
	// 서버에서 확정한 게임 월드의 사건만 비용 처리
	if (HasServerAuthority() == false || IsValid(GetWorld()) == false)
	{
		// 클라이언트의 중복 차감 방지
		return;
	}
	// 문을 제외한 일반 시설만 추가 청구하고 기존 지문 차감 유지
	bool bSupportedKind = Kind == EMD_FacilityKind::CoolingPump || Kind == EMD_FacilityKind::TrashSorter
		|| Kind == EMD_FacilityKind::WiringPanel;
	// 실제 고장 상태와 비용 대상 설정이 모두 맞는 경우에만 청구 유지
	if (Snapshot.IsFaultActive() == true && bFaultCostEnabled == true && bSupportedKind == true)
	{
		// 수리 진행 알림에서는 기존 사건의 청구 시각 유지
		if (CostFaultId != Snapshot.FaultId)
		{
			// 새 고장 또는 재진입한 사건의 비용 시계 연결
			CostFaultId = Snapshot.FaultId;
			// 정상 상태와 서브레벨 이탈 구간의 소급 청구 방지
			LastFaultChargeTime = GetWorld()->GetTimeSeconds();
			// 경과 정수 초에 따른 차감을 확인하는 서버 예약 시작
			GetWorld()->GetTimerManager().SetTimer(FaultCostTimer,
				FTimerDelegate::CreateUObject(this, &ThisClass::ApplyFaultCost, false), 0.1f, true);
		}
		// 같은 고장에 대한 수리 시작과 취소 중에도 차감 유지
		return;
	}
	// 복구한 동일 사건의 미정산 정수 초만 마지막 차감
	if (CostFaultId > 0 && CostFaultId == Previous.FaultId)
	{
		// 정상 상태로 돌아가기 직전까지의 Credit 비용 반영
		ApplyFaultCost(true);
	}
	// Credit 알림 중 월드가 종료된 경우의 타이머 접근 방지
	if (IsValid(GetWorld()) == true)
	{
		// 복구한 시설에 속한 예약만 제거
		GetWorld()->GetTimerManager().ClearTimer(FaultCostTimer);
	}
	// 종료된 사건에 대한 추가 청구 차단
	CostFaultId = 0;
}

void UMD_FacilityFaultComponent::ApplyFaultCost(bool bFinalCharge)
{
	// 유효한 서버와 같은 고장 사건에 한정한 청구 허용
	if (HasServerAuthority() == false || IsValid(GetWorld()) == false || CostFaultId <= 0
		|| CostFaultId != Snapshot.FaultId || bFaultCostEnabled == false)
	{
		// 종료되었거나 다른 사건으로 바뀐 예약의 차감 방지
		return;
	}
	// 복구 시 마지막 정산을 제외한 정상 상태의 차감 방지
	if (Snapshot.IsFaultActive() == false && bFinalCharge == false)
	{
		// 수리가 끝난 시설에 대한 반복 청구 중단
		return;
	}
	// 아직 청구하지 않은 시간 계산
	int64 ElapsedSeconds = FMath::FloorToInt64(FMath::Max(0.0, GetWorld()->GetTimeSeconds() - LastFaultChargeTime));
	// 한 초 미만의 구간은 다음 정산까지 유지
	if (ElapsedSeconds <= 0)
	{
		// 동일 시각의 중복 차감 방지
		return;
	}
	// Credit 변경 알림의 재진입보다 먼저 청구 구간 확정
	LastFaultChargeTime += double(ElapsedSeconds);
	// 현재 실제 참가자 수를 제공하는 게임 상태 조회
	AGameStateBase* GameState = GetWorld()->GetGameState();
	// 비활성 인원과 관전자를 제외할 참가 인원 누계
	int32 ParticipantCount = 0;
	// 초기화가 끝난 게임 상태에서만 유효 인원 집계
	if (IsValid(GameState) == true)
	{
		// 현재 접속한 각 플레이어의 참가 상태 확인
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			// 기존 지문 비용과 동일한 참가자 필터 적용
			if (IsValid(PlayerState) == true && PlayerState->IsInactive() == false
				&& PlayerState->IsSpectator() == false && PlayerState->IsOnlyASpectator() == false)
			{
				// 실제 참가자 한 명의 비용 인원 반영
				++ParticipantCount;
			}
		}
	}
	// 1인도 기본 비용을 부과하고 기존 다인 비용을 유지하는 배율
	int32 Multipliers[] = {1, 1, 2, 4};
	// 잘못된 설정과 정수 오버플로를 제한한 초당 Credit 비용
	int32 LossPerSecond = int32(FMath::Min<int64>(MAX_int32,
		int64(FMath::Max(0, CostUnit)) * Multipliers[FMath::Clamp(ParticipantCount, 1, 4) - 1]));
	// 참가자 부재, 비용 비활성화 또는 종료된 게임 인스턴스의 거래 생략
	if (ParticipantCount <= 0 || LossPerSecond <= 0 || IsValid(GetWorld()->GetGameInstance()) == false)
	{
		// 무료 구간을 이후 인원 증가 시 소급 청구하지 않는 종료
		return;
	}
	// 팀의 실제 Credit을 관리하는 기존 경제 원장 조회
	UMD_MerchantSubsystem* Merchant = GetWorld()->GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
	// 최종 보고서 확정 이전의 고장 비용만 실제 잔액에 반영
	if (IsValid(Merchant) == true && Merchant->IsCreditHistoryFinished() == false)
	{
		// 기존 거래의 정수 범위 안에서 경과 초 비용 합산
		int32 Charge = int32(FMath::Min<int64>(MAX_int32, FMath::Min<int64>(ElapsedSeconds, MAX_int32) * int64(LossPerSecond)));
		// 대출 잔액과 무관하게 개인 기록에 귀속되지 않는 시스템 비용 차감
		Merchant->AddCredits(-Charge);
	}
}

bool UMD_FacilityFaultComponent::CompleteRepairFromValidatedSource(int32 ExpectedFaultId)
{
	// 추가 효과가 없는 기존 외부 수리를 공통 확정으로 전달
	return CompleteRepairWithEffects(ExpectedFaultId, []() {});
}

bool UMD_FacilityFaultComponent::CompleteRepairWithEffects(int32 ExpectedFaultId, TFunctionRef<void()> Effects)
{
	// 서버 전이와 진행 중인 확정 여부 확인
	if (HasServerAuthority() == false || bStateTransitionInProgress == true)
	{
		// 중첩 복구 확정 차단
		return false;
	}
	// 현재 활성 사건의 일치 여부 확인
	if (Snapshot.IsFaultActive() == false || Snapshot.FaultId != ExpectedFaultId)
	{
		// 중복 복구와 오래된 사건 요청 거부
		return false;
	}
	// 장치 효과와 방송 완료까지 동일 사건 보호
	TGuardValue<bool> TransitionGuard(bStateTransitionInProgress, true);
	// 상태 방송용 이전 고장 보관
	FMD_FacilitySnapshot Previous = Snapshot;
	// 복구 완료 상태 확정
	Snapshot.State = EMD_FacilityState::Repaired;
	// 완료 진척 반영
	Snapshot.RepairProgress = 1.f;
	// 서버 기준 복구 시각 기록
	Snapshot.RepairedAt = GetServerTime();
	// 완료된 작업자 점유 해제
	Snapshot.Repairer = nullptr;
	// 확정된 복구의 장치 효과 실행
	Effects();
	// 복구 효과와 일치하는 상태 방송
	PublishChange(Previous);
	// 동일 사건 복구 성공 전달
	return true;
}

void UMD_FacilityFaultComponent::OnRep_Snapshot(const FMD_FacilitySnapshot& Previous)
{
	// Copy before broadcasting so a listener's subsequent transition cannot change this event.
	const FMD_FacilitySnapshot Current = Snapshot;
	RefreshAudio(Previous, Current);
	OnStateChanged.Broadcast(this, Previous, Current);
}

void UMD_FacilityFaultComponent::PlayFeedbackForAll(const TCHAR* Event, const FVector& Location)
{
	if (HasServerAuthority()) MulticastFeedback(FGameplayTag::RequestGameplayTag(FName(Event), false), Location);
}
void UMD_FacilityFaultComponent::MulticastFeedback_Implementation(FGameplayTag Event, FVector_NetQuantize Location)
{
	MDAudioFeedback::At(this, *Event.ToString(), Location);
}
void UMD_FacilityFaultComponent::RefreshAudio(const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current)
{
	if (!GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	if (!Previous.IsFaultActive() && Current.IsFaultActive())
	{
		if (Kind == EMD_FacilityKind::Door) MDAudioFeedback::World(GetOwner(), TEXT("SFX.World.Door.Jammed"));
		if (Kind == EMD_FacilityKind::CoolingPump) MDAudioFeedback::World(GetOwner(), TEXT("SFX.Facility.Pump.Fault"));
		if (Kind == EMD_FacilityKind::TrashSorter) MDAudioFeedback::World(GetOwner(), TEXT("SFX.Facility.Sorter.Fault"));
		if (Kind == EMD_FacilityKind::Fingerprint) MDAudioFeedback::UI(this, TEXT("SFX.Facility.Fingerprint.Request"));
	}
	if (Kind == EMD_FacilityKind::TrashSorter && Current.IsFaultActive() && !FaultAudio.IsValid())
		FaultAudio = MDAudioFeedback::Attached(GetOwner()->GetRootComponent(), TEXT("SFX.Facility.Sorter.FaultLoop"));
	if (!Current.IsFaultActive()) MDAudioFeedback::Stop(this, FaultAudio);
	if (Current.State == EMD_FacilityState::Repairing && !RepairAudio.IsValid())
	{
		AActor* Source = IsValid(Current.Repairer) ? Current.Repairer.Get() : GetOwner();
		if (Previous.State != EMD_FacilityState::Repairing) MDAudioFeedback::World(Source, TEXT("SFX.Facility.Repair.Start"));
		RepairAudio = MDAudioFeedback::Attached(Source->GetRootComponent(), TEXT("SFX.Facility.Repair.Loop"));
	}
	if (Current.State != EMD_FacilityState::Repairing)
	{
		MDAudioFeedback::Stop(this, RepairAudio);
		if (Previous.State == EMD_FacilityState::Repairing && Current.IsFaultActive())
			MDAudioFeedback::World(GetOwner(), TEXT("SFX.Facility.Repair.Cancel"));
	}
	if (Previous.IsFaultActive() && Current.State == EMD_FacilityState::Repaired)
	{
		const TCHAR* Event = Kind == EMD_FacilityKind::Fingerprint ? TEXT("SFX.Facility.Fingerprint.Complete")
			: Kind == EMD_FacilityKind::Breaker ? TEXT("SFX.Facility.Breaker.Complete") : TEXT("SFX.Facility.Repair.Complete");
		MDAudioFeedback::World(GetOwner(), Event);
	}
}
