#include "Component/Facility/MD_FacilityFaultComponent.h"

#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
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
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
	OnRep_Snapshot(Previous); // Listen server receives the same presentation event as clients.
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
