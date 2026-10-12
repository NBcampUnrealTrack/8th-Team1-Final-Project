#include "Component/Facility/MD_FacilityRepairComponent.h"

#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "TimerManager.h"

UMD_FacilityFaultComponent* UMD_FacilityRepairComponent::ResolveRepairTarget() const
{
	return const_cast<UMD_FacilityRepairComponent*>(this);
}

FMD_FacilitySnapshot UMD_FacilityRepairComponent::GetRepairSnapshot() const
{
	const UMD_FacilityFaultComponent* Target = ResolveRepairTarget();
	return IsValid(Target) ? Target->GetSnapshot() : FMD_FacilitySnapshot();
}

bool UMD_FacilityRepairComponent::HasRepairFault() const
{
	return GetRepairSnapshot().IsFaultActive();
}

float UMD_FacilityRepairComponent::GetManualRepairDuration() const
{
	if (ActiveManualRepairSeconds > 0.f) return ActiveManualRepairSeconds;
	int32 Players = 0;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GameState)
	{
		for (const APlayerState* Player : GameState->PlayerArray)
		{
			// Respawning participants still count; spectators and disconnected states do not.
			if (IsValid(Player) && !Player->IsInactive() && !Player->IsSpectator() && !Player->IsOnlyASpectator()) ++Players;
		}
	}
	return ManualRepairSeconds * FMath::Clamp(Players, 1, 4);
}

bool UMD_FacilityRepairComponent::CanReachForRepair(const APawn* Pawn) const
{
	const UMD_InteractionComponent* Interaction = IsValid(Pawn) ? Pawn->FindComponentByClass<UMD_InteractionComponent>() : nullptr;
	if (!IsValid(Interaction) || !Interaction->CanOwnerInteract() || Interaction->IsHolding()
		|| Pawn->GetWorld() != GetWorld() || !HasRepairFault() || !FMath::IsFinite(RepairReach) || RepairReach <= 0.f) return false;
	FVector Origin;
	FRotator Rotation;
	Pawn->GetController()->GetPlayerViewPoint(Origin, Rotation);
	FHitResult Hit;
	// A wall's coarse simple hull can extend in front of a visibly accessible panel.
	// Use its actual surface for occlusion, while the panel's query box still supplies the hit.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FacilityRepair), true, Pawn);
	return GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Rotation.Vector() * RepairReach, ECC_Visibility, Params)
		&& Hit.GetActor() == GetOwner();
}

bool UMD_FacilityRepairComponent::BeginManualRepair(APawn* Pawn)
{
	if (!GetOwner()->HasAuthority() || !bAllowManualRepair || !CanReachForRepair(Pawn)
		|| !FMath::IsFinite(ManualRepairSeconds) || ManualRepairSeconds <= 0.f) return false;
	UMD_FacilityFaultComponent* Target = ResolveRepairTarget();
	const int32 FaultId = GetRepairSnapshot().FaultId;
	const float Duration = GetManualRepairDuration();
	if (!FMath::IsFinite(Duration) || Duration <= 0.f) return false;
	if (!IsValid(Target) || !Target->BeginRepair(Pawn, FaultId)) return false;
	ManualTarget = Target;
	ManualRepairer = Pawn;
	ManualFaultId = FaultId;
	ActiveManualRepairSeconds = Duration;
	LastWorkTime = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(ManualTimer, this, &ThisClass::AdvanceManualRepair, 0.1f, true);
	return true;
}

void UMD_FacilityRepairComponent::StopManualRepair(APawn* Pawn)
{
	if (!GetOwner()->HasAuthority() || ManualRepairer.Get() != Pawn) return;
	if (UMD_FacilityFaultComponent* Target = ManualTarget.Get()) Target->CancelRepair(Pawn, ManualFaultId);
	GetWorld()->GetTimerManager().ClearTimer(ManualTimer);
	ManualRepairer.Reset();
	ManualTarget.Reset();
	ActiveManualRepairSeconds = 0.f;
}

void UMD_FacilityRepairComponent::AdvanceManualRepair()
{
	APawn* Pawn = ManualRepairer.Get();
	const FMD_FacilitySnapshot State = GetRepairSnapshot();
	if (!IsValid(Pawn) || !ManualTarget.IsValid() || ManualTarget.Get() != ResolveRepairTarget()
		|| !CanReachForRepair(Pawn) || State.FaultId != ManualFaultId
		|| State.State != EMD_FacilityState::Repairing || State.Repairer != Pawn
		|| !bAllowManualRepair || !FMath::IsFinite(ActiveManualRepairSeconds) || ActiveManualRepairSeconds <= 0.f)
	{
		StopManualRepair(Pawn);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const float Work = static_cast<float>((Now - LastWorkTime) / ActiveManualRepairSeconds);
	LastWorkTime = Now;
	ManualTarget->AddRepairProgress(Pawn, ManualFaultId, Work);
	if (!HasRepairFault()) StopManualRepair(Pawn);
}

bool UMD_FacilityRepairComponent::TryRepairWithTape(APawn* Pawn, int32 ExpectedFaultId)
{
	if (!GetOwner()->HasAuthority() || !bAllowInsulationTape || !CanReachForRepair(Pawn)
		|| GetRepairSnapshot().FaultId != ExpectedFaultId) return false;
	UMD_EquipHotbarComponent* Hotbar = Pawn->FindComponentByClass<UMD_EquipHotbarComponent>();
	UMD_ConsumeItemDefinition* Tape = Hotbar ? Cast<UMD_ConsumeItemDefinition>(Hotbar->GetCurrentItem()) : nullptr;
	if (!IsValid(Tape) || !Tape->GetUseAbilityTag().MatchesTagExact(MDGameplayTags::Ability_UtilityRepair_Insulation)) return false;
	UMD_FacilityFaultComponent* Target = ResolveRepairTarget();
	if (!IsValid(Target) || !Target->GetOwner()->HasAuthority() || !Target->IsFaultActive()
		|| Target->GetSnapshot().FaultId != ExpectedFaultId) return false;
	// 수량 예약과 동일 사건의 복구 확정을 단일 성공 결과로 연결
	return Hotbar->TryConsumeItemAndCommit(Tape, 1, [Target, ExpectedFaultId]()
	{
		// 실제 복구 거부 시 예약 수량 복원을 위한 확정 결과 전달
		return Target->CompleteRepairFromValidatedSource(ExpectedFaultId);
	});
}

FText UMD_FacilityRepairComponent::GetRepairPrompt(APawn* Requester) const
{
	// 작업자와 진척도를 같은 복제 상태에서 조회
	FMD_FacilitySnapshot State = GetRepairSnapshot();
	// 활성 고장이 없는 시설의 수리 안내 생략
	if (State.IsFaultActive() == false)
	{
		// 정상 또는 복구 완료 상태의 빈 안내 반환
		return FText::GetEmpty();
	}
	// 점유 여부와 관계없이 현재 서버 진척도를 백분율로 표시
	FText Progress = FText::AsNumber(FMath::FloorToInt(State.RepairProgress * 100.f));
	// 작업자 참조가 아직 도착하지 않은 경우도 본인 작업으로 오인 방지
	if (State.State == EMD_FacilityState::Repairing && (IsValid(Requester) == false || State.Repairer != Requester))
	{
		// 다른 작업자가 점유한 수리의 진행 안내
		return FText::Format(NSLOCTEXT("MDInteraction", "FacilityRepairOccupied", "다른 플레이어가 수리 중 · {0}%"), Progress);
	}
	// 수동 수리 미지원 시설의 기존 도구 안내 유지
	if (bAllowManualRepair == false)
	{
		// 시설에서 허용한 소모품 또는 폼건 수리 안내
		return bAllowInsulationTape == true
			? NSLOCTEXT("MDInteraction", "FacilityNeedsTape", "고장 · 절연테이프로 수리")
			: NSLOCTEXT("MDInteraction", "FacilityNeedsFoam", "고장 · 폼건으로 수리");
	}
	// 수동 수리 입력과 실제 허용된 절연테이프 사용 안내
	return FText::Format(bAllowInsulationTape == true
		? NSLOCTEXT("MDInteraction", "FacilityManualRepair", "[E 유지] 수리 {0}% · 절연테이프 사용 가능")
		: NSLOCTEXT("MDInteraction", "FacilityManualOnlyRepair", "[E 유지] 수리 {0}%"), Progress);
}

void UMD_FacilityRepairComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ManualTarget.IsValid() && GetOwner()->HasAuthority()) StopManualRepair(ManualRepairer.Get());
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ManualTimer);
	Super::EndPlay(Reason);
}
