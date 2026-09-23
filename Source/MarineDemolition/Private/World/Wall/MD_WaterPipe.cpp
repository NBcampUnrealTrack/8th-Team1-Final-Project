#include "World/Wall/MD_WaterPipe.h"

#include "Component/Hazard/MD_HazardVolumeComponent.h"
#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Types/Persistence/MD_WorldActorSnapshotTypes.h"

AMD_WaterPipe::AMD_WaterPipe()
{
	HazardVolume = CreateDefaultSubobject<UMD_HazardVolumeComponent>(TEXT("HazardVolume"));
	HazardVolume->SetupAttachment(Root);

	RequiredRepairToolTag = MDGameplayTags::Ability_UtilityRepair_Foam;

	GushLoopSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Hazard.Pipe.GushLoop"), /*ErrorIfNotFound*/ false);
}

void AMD_WaterPipe::OnUtilityStateChangedLocal(const EMD_UtilityState NewState)
{
	UWorld* const World = GetWorld();
	UMD_AudioSubsystem* const Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	if (!Audio)
	{
		return;
	}

	if (NewState != EMD_UtilityState::Broken)
	{
		// 봉합·초기 상태. 분출이 돌고 있으면 끊는다. 무효 핸들이면 StopHandle 이 무시한다.
		Audio->StopHandle(GushLoopHandle, GushStopFadeSeconds);
		GushLoopHandle = FMD_AudioHandle();
		return;
	}

	if (GushLoopSoundTag.IsValid())
	{
		// 파열 지점에서 재생한다. BreakLocation 은 복제 속성이고 RepNotify 보다 먼저 적용된다.
		// 이미 돌고 있으면 겹치지 않게 먼저 끊는다.
		Audio->StopHandle(GushLoopHandle, 0.0f);
		GushLoopHandle = Audio->PlayEventAtLocation(GushLoopSoundTag, GetBreakLocation());
	}
}

void AMD_WaterPipe::OnBroken(AActor* Cutter)
{
	// 근접 위험 볼륨 가동 (-8/s)
	if (IsValid(HazardVolume))
	{
		HazardVolume->SetHazardActive(true);
	}

	// 처음부터 고장(Cutter == nullptr)이면 사고만 발생하고 감점은 즉시·지속 모두 없음
	if (Cutter == nullptr)
	{
		return;
	}

	// 파열 즉시 감점
	if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
	{
		GS->AddScoreEvent(MDGameplayTags::Score_Penalty_PipeBurst);
	}

	// 미밀봉 지속 감점 시작
	LeakTicksApplied = 0;
	GetWorld()->GetTimerManager().SetTimer(
		LeakTimerHandle, this, &AMD_WaterPipe::ApplyLeakTick, LeakTickInterval, true);
}

void AMD_WaterPipe::ApplyLeakTick()
{
	// 상한 도달 시 중단
	if (LeakTicksApplied >= MaxLeakTicks)
	{
		GetWorld()->GetTimerManager().ClearTimer(LeakTimerHandle);
		return;
	}

	if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
	{
		GS->AddScoreEvent(MDGameplayTags::Score_Penalty_PipeLeakTick);
	}

	++LeakTicksApplied;
}

void AMD_WaterPipe::OnRepaired(AActor* Repairer)
{
	// 폼건 봉합: 분출·틱 피해·누수 감점 전부 중단
	if (IsValid(HazardVolume))
	{
		HazardVolume->SetHazardActive(false);
	}

	GetWorld()->GetTimerManager().ClearTimer(LeakTimerHandle);
}

FMD_UtilityRecord AMD_WaterPipe::BuildPersistenceRecord()
{
	FMD_UtilityRecord Record = Super::BuildPersistenceRecord();
	// 이미 감점한 횟수를 다시 적용하지 않도록 보관
	Record.LeakTicksApplied = LeakTicksApplied;
	// 배치 파손과 활성 누수 타이머 구분
	Record.bLeakTimerActive = GetWorldTimerManager().IsTimerActive(LeakTimerHandle);
	// 다음 감점까지 남은 시간 보존
	Record.LeakTimeRemaining = FMath::Max(0.0f, GetWorldTimerManager().GetTimerRemaining(LeakTimerHandle));
	// 누수 진행을 포함한 기록 반환
	return Record;
}

bool AMD_WaterPipe::RestorePersistenceRecord(FMD_UtilityRecord Record)
{
	// 잘못된 누수 진행이 무한 감점이나 잘못된 타이머를 만드는 상황 차단
	if (Record.LeakTicksApplied < 0 || Record.LeakTicksApplied > MaxLeakTicks
		|| FMath::IsFinite(Record.LeakTimeRemaining) == false || Record.LeakTimeRemaining < 0.0f)
	{
		// 파손 상태 변경 전 실패 반환
		return false;
	}
	if (Super::RestorePersistenceRecord(Record) == false)
	{
		// 공통 상태를 복원하지 못한 요청 종료
		return false;
	}
	// 이전 월드 상태의 감점 타이머 제거
	GetWorldTimerManager().ClearTimer(LeakTimerHandle);
	// 이미 소모한 감점 횟수 복원
	LeakTicksApplied = Record.LeakTicksApplied;
	// 저장 당시 진행 중이던 누수만 재개
	if (Record.State == EMD_UtilityState::Broken && Record.bLeakTimerActive == true && LeakTicksApplied < MaxLeakTicks)
	{
		// 다음 감점 시각을 저장된 잔여 시간부터 재개
		GetWorldTimerManager().SetTimer(LeakTimerHandle, this, &ThisClass::ApplyLeakTick, LeakTickInterval, true,
			FMath::Max(Record.LeakTimeRemaining, UE_KINDA_SMALL_NUMBER));
	}
	// 누수 진행 복원 성공 반환
	return true;
}
