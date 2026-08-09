#include "World/Wall/MD_WaterPipe.h"

#include "Component/Hazard/MD_HazardVolumeComponent.h"
#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "TimerManager.h"

AMD_WaterPipe::AMD_WaterPipe()
{
	HazardVolume = CreateDefaultSubobject<UMD_HazardVolumeComponent>(TEXT("HazardVolume"));
	HazardVolume->SetupAttachment(Root);

	// TODO: RequiredRepairToolTag = 폼건 태그로 지정 (장비 시스템 연결 후)
}

void AMD_WaterPipe::OnBroken(AActor* Cutter)
{
	// 파열 즉시 감점
	if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
	{
		GS->AddScoreEvent(MDGameplayTags::Score_Penalty_PipeBurst);
	}

	// 근접 위험 볼륨 가동 (-8/s)
	if (IsValid(HazardVolume))
	{
		HazardVolume->SetHazardActive(true);
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
