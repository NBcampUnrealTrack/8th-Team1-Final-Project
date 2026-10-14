#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Audio/MD_GameplayAudioFeedback.h"

#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Data/Equipment/MD_ConsumeItemDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Internationalization/Internationalization.h"
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
	const bool bRepaired = Hotbar->TryConsumeItemAndCommit(Tape, 1, [Target, ExpectedFaultId]()
	{
		// 실제 복구 거부 시 예약 수량 복원을 위한 확정 결과 전달
		return Target->CompleteRepairFromValidatedSource(ExpectedFaultId);
	});
	if (bRepaired) Target->PlayFeedbackForAll(TEXT("SFX.Facility.Repair.Tape"), GetOwner()->GetActorLocation());
	return bRepaired;
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
	const int32 DisplayedPercent = FMath::FloorToInt(State.RepairProgress * 100.f);
	// 작업자 참조가 아직 도착하지 않은 경우도 본인 작업으로 오인 방지
	if (State.State == EMD_FacilityState::Repairing && (IsValid(Requester) == false || State.Repairer != Requester))
	{
		FText Progress = FText::AsNumber(DisplayedPercent);
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
	// 요청자를 생략한 조회에서도 현재 월드의 입력 설정을 찾을 컨텍스트 선택
	UObject* WorldContextObject = GetOwner();
	// 실제 요청자가 있는 경우 해당 플레이어의 월드 우선 적용
	if (IsValid(Requester) == true)
	{
		// 수리 안내를 조회한 플레이어의 컨텍스트 사용
		WorldContextObject = Requester;
	}
	// 선택 유형의 공용 문구에 요청자의 실제 상호작용 키 적용
	FText ActionPrompt = UMD_InteractionPresentationLibrary::FindActionPromptByType(ManualRepairPromptType, WorldContextObject);
	// 숨김 유형이나 빈 설정에서 진행률만 행동 안내로 남는 상황 방지
	if (ActionPrompt.IsEmpty() == true)
	{
		// 다시 표시할 때 현재 설정을 반영할 캐시 무효화
		bHasCachedManualRepairPrompt = false;
		// 명시적으로 비워진 수동 수리 안내 생략
		return FText::GetEmpty();
	}
	// 현재 허용된 절연테이프와 홀드 입력을 포함하는 상태 문구 선택
	FText Pattern = bAllowInsulationTape == true
		? NSLOCTEXT("MDInteraction", "FacilityTypedManualRepair", "{0} (길게 누르기) {1}% · 절연테이프 사용 가능")
		: NSLOCTEXT("MDInteraction", "FacilityTypedManualOnlyRepair", "{0} (길게 누르기) {1}%");
	// 언어 변경 시 진행률과 번역을 다시 조합할 현재 로캘 조회
	FCultureRef Locale = FInternationalization::Get().GetCurrentLocale();
	// 상태와 유형 및 실제 표시 문구가 같으면 이전 최종 문구 재사용
	if (bHasCachedManualRepairPrompt == false || CachedManualRepairPercent != DisplayedPercent
		|| CachedManualRepairPromptType != ManualRepairPromptType || CachedManualRepairLocale.Get() != &Locale.Get()
		|| CachedManualRepairPattern.IdenticalTo(Pattern) == false || CachedManualRepairActionPrompt.EqualTo(ActionPrompt) == false)
	{
		// 공용 행동 문구와 현재 수리 진행률의 결합
		CachedManualRepairPrompt = FText::Format(Pattern, ActionPrompt, FText::AsNumber(DisplayedPercent));
		// 다음 조회에서 비교할 표시 진행률 보관
		CachedManualRepairPercent = DisplayedPercent;
		// 설정 문구와 키 변경을 감지할 마지막 행동 안내 보관
		CachedManualRepairActionPrompt = ActionPrompt;
		// 액터의 수동 수리 유형 변경 감지를 위한 선택값 보관
		CachedManualRepairPromptType = ManualRepairPromptType;
		// 절연테이프 지원 여부와 번역 변경 감지를 위한 서식 보관
		CachedManualRepairPattern = FTextSnapshot(Pattern);
		// 언어 변경 감지를 위한 마지막 로캘 보관
		CachedManualRepairLocale = Locale;
		// 현재 상태에 맞는 최종 안내 캐시의 준비 완료
		bHasCachedManualRepairPrompt = true;
	}
	// 상태 변경이 없으면 재조합 없이 이전 안내 반환
	return CachedManualRepairPrompt;
}

void UMD_FacilityRepairComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ManualTarget.IsValid() && GetOwner()->HasAuthority()) StopManualRepair(ManualRepairer.Get());
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ManualTimer);
	Super::EndPlay(Reason);
}
