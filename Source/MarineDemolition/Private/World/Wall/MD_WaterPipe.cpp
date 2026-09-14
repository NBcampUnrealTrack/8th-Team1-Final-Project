#include "World/Wall/MD_WaterPipe.h"

#include "Component/Hazard/MD_HazardVolumeComponent.h"
#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "TimerManager.h"

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
