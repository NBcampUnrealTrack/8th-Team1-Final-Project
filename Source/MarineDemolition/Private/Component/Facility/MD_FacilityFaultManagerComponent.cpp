#include "Component/Facility/MD_FacilityFaultManagerComponent.h"

#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "World/Wall/MD_PowerConduit.h"

UMD_FacilityFaultManagerComponent::UMD_FacilityFaultManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 중도 합류자에게 현재 메시 슬롯별 정전 집계 전달
	SetIsReplicatedByDefault(true);
}

void UMD_FacilityFaultManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 변경 이벤트 수신 시점과 무관한 현재 정전 상태 복제
	DOREPLIFETIME(UMD_FacilityFaultManagerComponent, PowerLightStates);
}

bool UMD_FacilityFaultManagerComponent::HasServerAuthority() const
{
	return IsValid(GetOwner()) && GetOwner()->HasAuthority() && GetWorld()
		&& GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UMD_FacilityFaultManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	Random.Initialize(RandomSeed != 0 ? RandomSeed : FMath::Rand());
	StartFaultSchedules();
}

void UMD_FacilityFaultManagerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// 복원 도중의 재질 콜백에서 신규 적용 차단
	bPowerLightsShuttingDown = true;
	// 월드 종료 이후 남은 중첩 재질 적용 예약 해제
	if (IsValid(GetWorld()) == true)
	{
		// 종료 대상 머신의 다음 틱 예약 정리
		GetWorld()->GetTimerManager().ClearTimer(PowerLightRefreshTimer);
	}
	// 원본 재질의 GC 보호를 유지하면서 복원 순회 분리
	TArray<FMD_PowerMaterialBaseline> LightsToRestore = LocalPowerLights;
	// 이전 월드의 전선 참조 제거
	PowerSources.Reset();
	// 이전 월드의 정전 집계 제거
	PowerLightStates.Reset();
	// 각 머신에서 최초 등록한 메시 슬롯 재질 복원
	for (FMD_PowerMaterialBaseline& Light : LightsToRestore)
	{
		// 제거된 컴포넌트와 범위를 벗어난 슬롯의 복원 생략
		if (IsPowerMeshValid(Light.MeshComponent, Light.MaterialIndex) == true)
		{
			// 동일 재질의 중복 렌더 갱신 생략
			if (Light.MeshComponent->GetMaterial(Light.MaterialIndex) != Light.OriginalMaterial)
			{
				// 다른 슬롯을 유지하면서 최초 재질로 복원
				Light.MeshComponent->SetMaterial(Light.MaterialIndex, Light.OriginalMaterial);
			}
		}
	}
	// 복원 완료 이후 최초 재질의 보관 참조 해제
	LocalPowerLights.Reset();
	StopFaultSchedules();
	for (const auto& Weak : Facilities)
	{
		if (UMD_FacilityFaultComponent* Facility = Weak.Get())
			Facility->OnStateChanged.RemoveDynamic(this, &UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged);
	}
	Facilities.Empty();
	Super::EndPlay(Reason);
}

bool UMD_FacilityFaultManagerComponent::RegisterFacility(UMD_FacilityFaultComponent* Facility)
{
	// 현재 서버 월드에 속한 유효 시설만 상태 레지스트리에 등록
	if (HasServerAuthority() == false || IsValid(Facility) == false || Facility->GetWorld() != GetWorld()
		|| IsValid(Facility->GetOwner()) == false || Facility->GetOwner()->HasAuthority() == false)
	{
		// 권한 또는 시설 참조가 맞지 않는 등록 거부
		return false;
	}
	// 다른 관리자의 종료 연결을 덮어쓰는 중복 소유 방지
	if (Facility->Manager.IsValid() == true && Facility->Manager.Get() != this)
	{
		// 기존 관리자의 사건 소유권 유지
		return false;
	}
	// 이미 종료된 시설의 약한 참조 제거
	Facilities.RemoveAll([](TWeakObjectPtr<UMD_FacilityFaultComponent>& Weak)
		{
			// 더 이상 상태를 제공하지 못하는 시설 제외 여부 전달
			return Weak.IsValid() == false;
		});
	// 반복 등록에서도 같은 사건 제공자를 한 번만 보관
	Facilities.AddUnique(Facility);
	// 자동 등록 재시도 전에 직접 등록된 시설의 종료 해제 경로 연결
	Facility->Manager = this;
	// 명시적 등록으로 충족된 초기 관리자 대기 예약 해제
	GetWorld()->GetTimerManager().ClearTimer(Facility->RegistrationTimer);
	// 고장과 복구의 지속 상태 변경을 한 번만 구독
	Facility->OnStateChanged.AddUniqueDynamic(this, &UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged);
	// 시설과 관리자 양방향 등록 성공 전달
	return true;
}

void UMD_FacilityFaultManagerComponent::UnregisterFacility(UMD_FacilityFaultComponent* Facility)
{
	// 서버에서만 시설 등록 해제
	if (HasServerAuthority() == false)
	{
		// 클라이언트의 레지스트리 변경 거부
		return;
	}
	// 아직 유효한 시설의 상태 변경 구독과 역참조 정리
	if (IsValid(Facility) == true)
	{
		// 해제 이후 늦은 고장 알림의 재집계 차단
		Facility->OnStateChanged.RemoveDynamic(this, &UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged);
		// 현재 관리자가 소유한 수명 연결만 해제
		if (Facility->Manager.Get() == this)
		{
			// 같은 시설의 후속 정상 등록 허용
			Facility->Manager.Reset();
		}
	}
	// 지정 시설과 이미 제거된 시설의 레지스트리 항목 정리
	Facilities.RemoveAll([Facility](TWeakObjectPtr<UMD_FacilityFaultComponent>& Weak)
		{
			// 해제 대상이거나 수명이 끝난 시설 여부 전달
			return Weak.IsValid() == false || Weak.Get() == Facility;
		});
}

TArray<UMD_FacilityFaultComponent*> UMD_FacilityFaultManagerComponent::GetRegisteredFacilities() const
{
	TArray<UMD_FacilityFaultComponent*> Result;
	for (const auto& Weak : Facilities)
		if (UMD_FacilityFaultComponent* Facility = Weak.Get()) Result.Add(Facility);
	return Result;
}

TArray<UMD_FacilityFaultComponent*> UMD_FacilityFaultManagerComponent::GetActiveFaults() const
{
	TArray<UMD_FacilityFaultComponent*> Result;
	for (UMD_FacilityFaultComponent* Facility : GetRegisteredFacilities())
		if (Facility->IsFaultActive()) Result.Add(Facility);
	return Result;
}

void UMD_FacilityFaultManagerComponent::HandleFacilityStateChanged(UMD_FacilityFaultComponent* Facility,
	const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current)
{
	// 진척률만 바뀐 수리의 동일 정전 집계 재구성 생략
	if (Previous.IsFaultActive() != Current.IsFaultActive())
	{
		// 고장 기여가 시작되거나 끝난 경우에만 현재 사건 재집계
		RebuildPowerLightStates();
	}
	OnFacilityStateChanged.Broadcast(Facility, Previous, Current);
}

void UMD_FacilityFaultManagerComponent::StartFaultSchedules()
{
	if (!HasServerAuthority()) return;
	StopFaultSchedules();
	TSet<FName> UsedIds;
	for (const FMD_FacilityFaultSchedule& Schedule : Schedules)
	{
		if (!Schedule.bEnabled) continue;
		if (Schedule.ScheduleId.IsNone() || UsedIds.Contains(Schedule.ScheduleId) || !Schedule.HasValidInterval())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MD_Facility] Ignoring invalid/duplicate fault schedule: %s"), *Schedule.ScheduleId.ToString());
			continue;
		}
		UsedIds.Add(Schedule.ScheduleId);
		RunningSchedules.Add(Schedule);
	}
	ScheduleTimers.SetNum(RunningSchedules.Num());
	for (int32 Index = 0; Index < RunningSchedules.Num(); ++Index) ArmSchedule(Index);
}

void UMD_FacilityFaultManagerComponent::StopFaultSchedules()
{
	if (!HasServerAuthority()) return;
	ScheduleGeneration = ScheduleGeneration == MAX_int32 ? 1 : ScheduleGeneration + 1;
	for (FTimerHandle& Timer : ScheduleTimers) GetWorld()->GetTimerManager().ClearTimer(Timer);
	ScheduleTimers.Empty();
	RunningSchedules.Empty();
	// Stopping new occurrences never repairs existing incidents or resets their work.
}

void UMD_FacilityFaultManagerComponent::ArmSchedule(int32 Index)
{
	if (!RunningSchedules.IsValidIndex(Index)) return;
	const FMD_FacilityFaultSchedule& Schedule = RunningSchedules[Index];
	const float Delay = Random.FRandRange(Schedule.MinIntervalSeconds, Schedule.MaxIntervalSeconds);
	GetWorld()->GetTimerManager().SetTimer(ScheduleTimers[Index],
		FTimerDelegate::CreateUObject(this, &UMD_FacilityFaultManagerComponent::Occur, Index, ScheduleGeneration), Delay, false);
}

void UMD_FacilityFaultManagerComponent::Occur(int32 Index, int32 Generation)
{
	if (!HasServerAuthority() || Generation != ScheduleGeneration || !RunningSchedules.IsValidIndex(Index)) return;
	// Copy: event listeners may stop/reconfigure schedules during the fault transition.
	const FMD_FacilityFaultSchedule Schedule = RunningSchedules[Index];
	TriggerRandomFault(Schedule.Kind, Schedule.Group);
	if (Generation == ScheduleGeneration) ArmSchedule(Index);
}

UMD_FacilityFaultComponent* UMD_FacilityFaultManagerComponent::TriggerRandomFault(EMD_FacilityKind Kind, FName Group)
{
	if (!HasServerAuthority()) return nullptr;
	TArray<UMD_FacilityFaultComponent*> Candidates;
	for (UMD_FacilityFaultComponent* Facility : GetRegisteredFacilities())
	{
		if (Facility->Kind == Kind && (Group.IsNone() || Facility->Group == Group) && Facility->CanReceiveRandomFault())
			Candidates.Add(Facility);
	}
	if (Candidates.IsEmpty()) return nullptr;
	UMD_FacilityFaultComponent* Chosen = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
	return Chosen->TryBreak() ? Chosen : nullptr;
}

void UMD_FacilityFaultManagerComponent::UpdatePowerSource(AMD_PowerConduit* Source, TArray<FMD_PowerLightState> Lights)
{
	// 서버 확정과 종료 중 재등록 여부 확인
	if (HasServerAuthority() == false || bPowerLightsShuttingDown == true)
	{
		// 클라이언트 및 종료된 월드의 집계 변경 거부
		return;
	}
	// 같은 월드의 유효한 서버 전선만 허용
	if (IsValid(Source) == false || Source->GetWorld() != GetWorld() || Source->HasAuthority() == false)
	{
		// 다른 월드 또는 무효 전선 참조 거부
		return;
	}
	// 제거 중인 전선의 재등록 차단
	if (Source->IsActorBeingDestroyed() == true)
	{
		// 종료된 전선의 정전 기여 추가 방지
		return;
	}
	// 전선별 중복 없는 메시 슬롯 설정 구성
	TArray<FMD_PowerLightState> Targets;
	// 등록 대상 수에 맞춘 임시 배열의 반복 확장 방지
	Targets.Reserve(Lights.Num());
	// 실제 메시 슬롯별 정전 집계를 위한 참조 정규화
	for (FMD_PowerLightState& Light : Lights)
	{
		// 같은 월드의 유효한 메시 슬롯만 집계
		if (IsPowerMeshValid(Light.MeshComponent, Light.MaterialIndex) == false)
		{
			// 잘못된 메시 또는 슬롯 참조 제외
			continue;
		}
		// 같은 전선의 중복 슬롯에 대한 최초 설정 유지
		if (Targets.ContainsByPredicate([&Light](FMD_PowerLightState& Target)
			{
				// 메시와 슬롯이 모두 같은 설정의 중복 여부 전달
				return Target.MeshComponent == Light.MeshComponent && Target.MaterialIndex == Light.MaterialIndex;
			}) == true)
		{
			// 중복 참조로 정전 사건 수 증가 방지
			continue;
		}
		// 고장 수가 아닌 BP의 대상과 재질 설정만 등록
		Light.ActiveFaultCount = 0;
		// 현재 전선이 담당하는 슬롯으로 정규화
		Light.bHasPowerSource = true;
		// 해당 슬롯의 최초 설정 보관
		Targets.Add(Light);
	}
	// 재등록 중에도 기존 전선의 우선순서를 유지할 항목 조회
	FPowerSource* Existing = PowerSources.FindByPredicate([Source](FPowerSource& Registered)
		{
			// 같은 전선 인스턴스의 등록 항목 여부 전달
			return Registered.Source.Get() == Source;
		});
	// 별도 시설 등록보다 먼저 도착한 고장 변경도 현재 상태로 비교
	bool bFaultActive = IsValid(Source->FacilityRepair) == true && Source->FacilityRepair->GetSnapshot().IsFaultActive() == true;
	// 이미 등록된 전선은 실제 설정이나 고장 기여가 달라진 경우만 갱신
	if (Existing != nullptr)
	{
		// 동일 슬롯 목록과 고장 상태의 재등록 여부 확인
		bool bUnchanged = IsValid(Source->FacilityRepair) == true && Existing->Targets.Num() == Targets.Num() && Existing->bFaultActive == bFaultActive;
		// 등록 순서와 재질 설정까지 비교하여 최초 전선 우선순위 보존
		for (int32 Index = 0; bUnchanged == true && Index < Targets.Num(); ++Index)
		{
			// 같은 위치의 실제 메시와 슬롯 비교
			bUnchanged = Existing->Targets[Index].MeshComponent == Targets[Index].MeshComponent
				&& Existing->Targets[Index].MaterialIndex == Targets[Index].MaterialIndex;
			// 같은 슬롯을 유지하는 경우 BP의 두 재질 변경 확인
			if (bUnchanged == true)
			{
				// 정상 및 정전 재질이 모두 같은 경우에만 기존 등록 재사용
				bUnchanged = Existing->Targets[Index].NormalMaterial == Targets[Index].NormalMaterial
					&& Existing->Targets[Index].BlackoutMaterial == Targets[Index].BlackoutMaterial;
			}
			// 메시 재준비 후 집계에서 빠진 슬롯의 재등록 필요 여부 확인
			if (bUnchanged == true)
			{
				// 유효한 담당 집계가 남아 있는 슬롯에만 기존 등록 재사용
				bUnchanged = PowerLightStates.ContainsByPredicate([&Targets, Index](FMD_PowerLightState& State)
					{
						// 같은 메시 슬롯에 현재 전선 기여가 남아 있는지 확인
						return State.bHasPowerSource == true && State.MeshComponent == Targets[Index].MeshComponent
							&& State.MaterialIndex == Targets[Index].MaterialIndex;
					});
			}
		}
		// 준비 대기의 동일 등록에서 전체 서버 집계와 배열 교체 생략
		if (bUnchanged == true)
		{
			// 명시적 BP 재적용은 현재 집계로 즉시 처리
			ApplyPowerLightStates();
			// 기존 전선 순서와 슬롯 저장소 유지
			return;
		}
		// 최초 등록 순서를 유지하면서 현재 설정 반영
		Existing->Targets = MoveTemp(Targets);
	}
	else
	{
		// 새 전선만 등록 배열의 끝에 추가
		FPowerSource& Registered = PowerSources.AddDefaulted_GetRef();
		// 공통 고장 상태를 제공하는 전선 연결
		Registered.Source = Source;
		// 중복 없는 현재 메시 슬롯 설정 보관
		Registered.Targets = MoveTemp(Targets);
	}
	// 새로운 참조와 현재 고장 상태를 즉시 반영
	RebuildPowerLightStates();
}

void UMD_FacilityFaultManagerComponent::RemovePowerSource(AMD_PowerConduit* Source)
{
	// 서버 확정과 종료 중 재집계 여부 확인
	if (HasServerAuthority() == false || bPowerLightsShuttingDown == true)
	{
		// 클라이언트 또는 종료된 관리자의 집계 변경 거부
		return;
	}
	// 현재 월드의 유효한 전선만 직접 제거
	if (IsValid(Source) == true && Source->GetWorld() == GetWorld())
	{
		// 나머지 전선 순서를 유지하면서 해당 기여만 해제
		PowerSources.RemoveAll([Source](FPowerSource& Registered)
			{
				// 제거 대상 전선의 등록 항목 여부 전달
				return Registered.Source.Get() == Source;
			});
	}
	// 이미 무효화된 전선 참조도 함께 정리
	RebuildPowerLightStates();
}

void UMD_FacilityFaultManagerComponent::RegisterLocalPowerLights(TArray<FMD_PowerLightState> Lights)
{
	// 종료된 머신의 원래 재질 재등록 차단
	if (bPowerLightsShuttingDown == true)
	{
		// 종료 복원 상태 유지
		return;
	}
	// 복제보다 먼저 도착한 메시 슬롯의 최초 재질 보관
	for (FMD_PowerLightState& Light : Lights)
	{
		// 유효한 실제 메시 슬롯에 한해 캐시 확보
		CacheLocalPowerLight(Light.MeshComponent, Light.MaterialIndex);
	}
	// 이미 수신된 정전 집계와 BP 재질 설정 즉시 반영
	ApplyPowerLightStates();
}

int32 UMD_FacilityFaultManagerComponent::GetPowerOutageCount(UStaticMeshComponent* MeshComponent, int32 MaterialIndex)
{
	// 현재 월드의 유효한 메시 슬롯만 조회
	if (IsPowerMeshValid(MeshComponent, MaterialIndex) == false)
	{
		// 무효 참조에 정전 사건 없음 전달
		return 0;
	}
	// 서버 또는 복제된 현재 메시 슬롯 집계 검색
	for (FMD_PowerLightState& State : PowerLightStates)
	{
		// 해당 메시의 지정 슬롯에 남아 있는 사건만 반환
		if (State.MeshComponent == MeshComponent && State.MaterialIndex == MaterialIndex)
		{
			// 음수 사건 수의 외부 노출 차단
			return FMath::Max(0, State.ActiveFaultCount);
		}
	}
	// 정전 기여가 없는 메시 슬롯은 정상 상태 전달
	return 0;
}

void UMD_FacilityFaultManagerComponent::RebuildPowerLightStates()
{
	// 서버와 월드 종료 여부 확인
	if (HasServerAuthority() == false || bPowerLightsShuttingDown == true)
	{
		// 외부 머신 또는 종료 후 집계 변경 거부
		return;
	}
	// 제거된 전선 참조를 남은 등록 순서 유지하며 정리
	PowerSources.RemoveAll([this](FPowerSource& Registered)
		{
			// 동일 등록 항목에서 다시 사용할 실제 전선 조회
			AMD_PowerConduit* Source = Registered.Source.Get();
			// 제거되었거나 다른 월드의 전선 여부 확인
			if (IsValid(Source) == false || Source->GetWorld() != GetWorld() || Source->IsActorBeingDestroyed() == true)
			{
				// 더 이상 기여하지 않는 전선 제거 지정
				return true;
			}
			// 공통 고장 상태가 없는 전선 제외 여부 전달
			return IsValid(Source->FacilityRepair) == false;
		});
	// 현재 등록 순서에 따른 메시 슬롯별 설정과 사건 수 구성
	TArray<FMD_PowerLightState> CurrentStates;
	// 최초 전선의 재질 설정을 유지하며 전체 고장 기여 합산
	for (FPowerSource& Registered : PowerSources)
	{
		// 등록 항목의 실제 서버 전선 조회
		AMD_PowerConduit* Source = Registered.Source.Get();
		// 해당 전선이 담당하는 모든 슬롯에 사용할 고장 상태 보관
		bool bFaultActive = Source->FacilityRepair->GetSnapshot().IsFaultActive();
		// 다음 동일 설정 재등록에서 비교할 확정 고장 기여 보관
		Registered.bFaultActive = bFaultActive;
		// 동일 전선의 중복 없는 메시 슬롯을 각각 반영
		for (FMD_PowerLightState& Target : Registered.Targets)
		{
			// 제거된 메시와 유효 범위를 벗어난 슬롯 제외
			if (IsPowerMeshValid(Target.MeshComponent, Target.MaterialIndex) == false)
			{
				// 무효 대상의 재질 설정과 집계 생략
				continue;
			}
			// 앞서 등록된 전선이 담당하는 동일 메시 슬롯 조회
			FMD_PowerLightState* Existing = CurrentStates.FindByPredicate([&Target](FMD_PowerLightState& State)
				{
					// 메시 컴포넌트와 재질 슬롯의 동일 대상 여부 전달
					return State.MeshComponent == Target.MeshComponent && State.MaterialIndex == Target.MaterialIndex;
				});
			// 공유 슬롯은 최초 설정을 유지하면서 고장 수만 추가
			if (Existing != nullptr)
			{
				// 수리 중인 전선도 복구 완료 전까지 정전 기여 유지
				if (bFaultActive == true)
				{
					// 같은 전선의 사건을 슬롯당 한 번 합산
					++Existing->ActiveFaultCount;
				}
			}
			else
			{
				// 처음 발견한 슬롯에 최초 전선의 BP 재질 설정 보관
				FMD_PowerLightState State = Target;
				// 현재 전선의 서버 확정 고장만 집계에 반영
				State.ActiveFaultCount = bFaultActive == true ? 1 : 0;
				// 현재 담당 전선이 남아 있는 슬롯 표시
				State.bHasPowerSource = true;
				// 최초 발견 순서로 새 집계 항목 추가
				CurrentStates.Add(State);
			}
		}
	}
	// 기존 메시 슬롯 순서를 유지해 불필요한 복제 순서 변경 방지
	TArray<FMD_PowerLightState> UpdatedStates = PowerLightStates;
	// 이미 제거된 메시와 무효 슬롯의 강한 복제 참조 정리
	UpdatedStates.RemoveAll([this](FMD_PowerLightState& State)
		{
			// 복제할 수 없는 실제 대상의 제거 여부 전달
			return IsPowerMeshValid(State.MeshComponent, State.MaterialIndex) == false;
		});
	// 이전 참조가 해제된 슬롯에도 최초 재질 복원 상태 유지
	for (FMD_PowerLightState& State : UpdatedStates)
	{
		// 기존 순서에 반영할 현재 슬롯의 집계 위치 조회
		int32 CurrentIndex = CurrentStates.IndexOfByPredicate([&State](const FMD_PowerLightState& Current)
			{
				// 동일 메시 슬롯의 현재 설정 여부 전달
				return Current.MeshComponent == State.MeshComponent && Current.MaterialIndex == State.MaterialIndex;
			});
		// 현재 전선이 남아 있는 슬롯의 설정과 고장 수 갱신
		if (CurrentIndex != INDEX_NONE)
		{
			// 최초 전선의 현재 재질 설정 및 합산 결과 반영
			State = CurrentStates[CurrentIndex];
			// 기존 순서에 반영한 슬롯의 신규 추가 중복 방지
			CurrentStates.RemoveAt(CurrentIndex);
		}
		else
		{
			// 담당 전선이 사라진 슬롯의 정전 기여 해제
			State.ActiveFaultCount = 0;
			// BP 정상 재질 대신 최초 재질로 복원할 상태 표시
			State.bHasPowerSource = false;
		}
	}
	// 새롭게 연결된 슬롯만 지속 복제 배열 뒤에 추가
	UpdatedStates.Append(CurrentStates);
	// 값이 같은 집계의 불필요한 네트워크 갱신 생략
	bool bChanged = UpdatedStates.Num() != PowerLightStates.Num();
	// 동일 개수일 때 실제 메시 슬롯과 설정 변경 검사
	for (int32 Index = 0; bChanged == false && Index < UpdatedStates.Num(); ++Index)
	{
		// 메시 슬롯 또는 정전 사건 수의 변경 여부 확인
		bChanged = UpdatedStates[Index].MeshComponent != PowerLightStates[Index].MeshComponent
			|| UpdatedStates[Index].MaterialIndex != PowerLightStates[Index].MaterialIndex
			|| UpdatedStates[Index].ActiveFaultCount != PowerLightStates[Index].ActiveFaultCount;
		// 대상과 집계가 같아도 BP 재질 설정 변경 확인
		if (bChanged == false)
		{
			// 정상 및 정전 재질 또는 담당 전선 유무의 변경 여부 확인
			bChanged = UpdatedStates[Index].NormalMaterial != PowerLightStates[Index].NormalMaterial
				|| UpdatedStates[Index].BlackoutMaterial != PowerLightStates[Index].BlackoutMaterial
				|| UpdatedStates[Index].bHasPowerSource != PowerLightStates[Index].bHasPowerSource;
		}
	}
	// 변경된 집계와 재질 설정만 지속 복제 상태에 반영
	if (bChanged == true)
	{
		// 현재 서버 집계로 복제 배열 교체
		PowerLightStates = MoveTemp(UpdatedStates);
		// 휴면 중인 소유자도 최신 정전 상태 전송
		GetOwner()->FlushNetDormancy();
		// 다음 네트워크 갱신에 현재 슬롯별 상태 반영
		GetOwner()->ForceNetUpdate();
	}
	// 서버 머신의 실제 메시 재질에도 같은 상태 적용
	ApplyPowerLightStates();
}

FMD_PowerMaterialBaseline* UMD_FacilityFaultManagerComponent::CacheLocalPowerLight(UStaticMeshComponent* MeshComponent, int32 MaterialIndex)
{
	// 현재 월드에 남아 있는 유효한 메시 슬롯만 보관
	if (IsPowerMeshValid(MeshComponent, MaterialIndex) == false)
	{
		// 무효 대상에 적용 가능한 캐시 없음 전달
		return nullptr;
	}
	// 동일 슬롯의 최초 재질을 한 번의 검색으로 조회
	FMD_PowerMaterialBaseline* Existing = LocalPowerLights.FindByPredicate([MeshComponent, MaterialIndex](FMD_PowerMaterialBaseline& Baseline)
		{
			// 같은 메시 슬롯의 기존 캐시 여부 전달
			return Baseline.MeshComponent == MeshComponent && Baseline.MaterialIndex == MaterialIndex;
		});
	// 최초 등록 시점의 원래 재질 유지
	if (Existing != nullptr)
	{
		// 재검색 없이 기존 GC 보호 캐시 반환
		return Existing;
	}
	// 새 메시 슬롯의 GC 보호 캐시 항목 확보
	FMD_PowerMaterialBaseline& Baseline = LocalPowerLights.AddDefaulted_GetRef();
	// 최초 등록한 실제 메시 컴포넌트 보관
	Baseline.MeshComponent = MeshComponent;
	// 다른 슬롯과 분리할 재질 인덱스 보관
	Baseline.MaterialIndex = MaterialIndex;
	// 정상 재질 미설정과 참조 해제 시 사용할 최초 재질 보관
	Baseline.OriginalMaterial = MeshComponent->GetMaterial(MaterialIndex);
	// 생성한 원본 캐시를 재검색 없이 반환
	return &Baseline;
}

void UMD_FacilityFaultManagerComponent::OnRep_PowerLightStates()
{
	// 중도 합류 및 메시 참조 지연 도착의 현재 상태 적용
	ApplyPowerLightStates();
}

void UMD_FacilityFaultManagerComponent::ApplyPowerLightStates()
{
	// 종료 중 복원 상태와 유효한 월드 확인
	if (bPowerLightsShuttingDown == true || IsValid(GetWorld()) == false)
	{
		// 종료되었거나 준비되지 않은 머신의 적용 생략
		return;
	}
	// 재질 콜백의 중첩 적용은 같은 스택에서 재실행 차단
	if (bApplyingPowerLights == true)
	{
		// 한 번의 다음 틱 예약으로 최신 집계 재적용
		if (GetWorld()->GetTimerManager().IsTimerActive(PowerLightRefreshTimer) == false)
		{
			// 무한 콜백 재귀를 피하는 지연 상태 적용
			PowerLightRefreshTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::ApplyPowerLightStates);
		}
		// 현재 재질 적용 보호 유지
		return;
	}
	// 현재 적용으로 대체할 이전 프레임의 재시도 예약 정리
	GetWorld()->GetTimerManager().ClearTimer(PowerLightRefreshTimer);
	// 메시 에셋이 늦게 준비되는 복제 대상의 재적용 여부 보관
	bool bPendingPowerMeshes = false;
	// 실제 재질 변경이 끝날 때까지 동일 스택 적용 보호
	TGuardValue<bool> ApplyGuard(bApplyingPowerLights, true);
	// 재질 콜백이 복제 배열을 바꾸어도 현재 순회 보존
	TArray<FMD_PowerLightState> StatesToApply = PowerLightStates;
	// 현재 복제 집계를 각 머신의 메시 슬롯에 반영
	for (FMD_PowerLightState& State : StatesToApply)
	{
		// 이전 재질 콜백에서 종료된 관리자의 잔여 순회 차단
		if (bPowerLightsShuttingDown == true)
		{
			// 종료 복원 이후 오래된 정전 상태의 재적용 방지
			return;
		}
		// 지연 도착 또는 제거된 복제 메시 슬롯 제외
		if (IsPowerMeshValid(State.MeshComponent, State.MaterialIndex) == false)
		{
			// 복제 참조가 해석된 현재 월드의 메시만 에셋 준비 대기
			if (IsValid(State.MeshComponent) == true && State.MaterialIndex >= 0 && State.MeshComponent->GetWorld() == GetWorld())
			{
				// 제거 중인 대상에 대한 불필요한 재시도 차단
				if (IsValid(State.MeshComponent->GetOwner()) == true && State.MeshComponent->IsBeingDestroyed() == false
					&& State.MeshComponent->GetOwner()->IsActorBeingDestroyed() == false)
				{
					// 존재하는 에셋의 잘못된 슬롯과 아직 없는 에셋 구분
					if (IsValid(State.MeshComponent->GetStaticMesh()) == false)
					{
						// 복제 상태가 바뀌지 않아도 에셋 준비 이후 다시 적용
						bPendingPowerMeshes = true;
					}
				}
			}
			// 실제 메시가 준비된 다음 적용으로 연기
			continue;
		}
		// 최초 재질 보관과 기존 캐시 조회를 한 번의 검색으로 처리
		FMD_PowerMaterialBaseline* Baseline = CacheLocalPowerLight(State.MeshComponent, State.MaterialIndex);
		// 준비되지 않은 최초 재질 정보의 적용 생략
		if (Baseline == nullptr)
		{
			// 다음 복제 또는 등록에서 재질 적용 재시도
			continue;
		}
		// 전선 참조 해제 시 복원할 최초 재질을 기본값으로 확보
		UMaterialInterface* Material = Baseline->OriginalMaterial;
		// 담당 전선이 남아 있는 슬롯에만 BP 설정 재질 적용
		if (State.bHasPowerSource == true)
		{
			// 하나 이상의 고장이 남아 있으면 정전 상태 유지
			if (State.ActiveFaultCount > 0)
			{
				// 정전 재질 미설정 시 현재 재질 교체 생략
				if (IsValid(State.BlackoutMaterial) == false)
				{
					// 임의의 정전 재질이나 가시성 변경 방지
					continue;
				}
				// 최초 담당 전선이 지정한 정전 재질 선택
				Material = State.BlackoutMaterial;
			}
			else
			{
				// 정상 재질을 지정한 경우에만 최초 재질 대신 선택
				if (IsValid(State.NormalMaterial) == true)
				{
					// 고장이 모두 복구된 슬롯의 BP 정상 재질 선택
					Material = State.NormalMaterial;
				}
			}
		}
		// 동일 재질의 중복 콜백과 렌더 갱신 생략
		if (State.MeshComponent->GetMaterial(State.MaterialIndex) != Material)
		{
			// 가시성과 다른 재질 슬롯을 유지하면서 해당 슬롯만 교체
			State.MeshComponent->SetMaterial(State.MaterialIndex, Material);
		}
	}
	// 지연 준비 중인 메시가 남은 경우에만 주기적인 준비 확인 예약
	if (bPendingPowerMeshes == true && bPowerLightsShuttingDown == false)
	{
		// 재진입 콜백이 예약한 갱신과 같은 단일 타이머 사용
		if (GetWorld()->GetTimerManager().IsTimerActive(PowerLightRefreshTimer) == false)
		{
			// 다음 틱의 재진입 갱신은 유지하고 에셋 준비 확인만 0.1초 간격으로 제한
			GetWorld()->GetTimerManager().SetTimer(PowerLightRefreshTimer, this, &ThisClass::ApplyPowerLightStates, 0.1f, false);
		}
	}
}

bool UMD_FacilityFaultManagerComponent::IsPowerMeshValid(UStaticMeshComponent* MeshComponent, int32 MaterialIndex)
{
	// 같은 월드의 유효한 메시와 음수가 아닌 슬롯만 허용
	if (IsValid(MeshComponent) == false || MaterialIndex < 0 || MeshComponent->GetWorld() != GetWorld())
	{
		// 다른 월드 또는 잘못된 대상의 재질 변경 차단
		return false;
	}
	// 메시 에셋의 실제 슬롯 범위와 소유 액터 확인
	if (MaterialIndex >= MeshComponent->GetNumMaterials() || IsValid(MeshComponent->GetOwner()) == false)
	{
		// 재질 배열 확장으로 존재하지 않는 슬롯의 생성 방지
		return false;
	}
	// 컴포넌트 또는 소유 액터 제거 도중의 메시 재질 변경 차단
	if (MeshComponent->IsBeingDestroyed() == true || MeshComponent->GetOwner()->IsActorBeingDestroyed() == true)
	{
		// 수명 종료 대상의 적용 생략
		return false;
	}
	// 현재 사용할 수 있는 실제 메시 슬롯 여부 전달
	return true;
}
