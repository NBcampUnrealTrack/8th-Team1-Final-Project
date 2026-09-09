#include "World/Wall/MD_PowerConduit.h"

#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"

AMD_PowerConduit::AMD_PowerConduit()
{
	RequiredRepairToolTag = MDGameplayTags::Ability_UtilityRepair_Insulation;

	CutSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Hazard.Cable.Cut"), /*ErrorIfNotFound*/ false);
	SparkLoopSoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Hazard.Cable.SparkLoop"), /*ErrorIfNotFound*/ false);
}

void AMD_PowerConduit::OnUtilityStateChangedLocal(const EMD_UtilityState NewState)
{
	UWorld* const World = GetWorld();
	UMD_AudioSubsystem* const Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	if (!Audio)
	{
		return;
	}

	if (NewState != EMD_UtilityState::Broken)
	{
		// 수리·초기 상태. 스파크가 돌고 있으면 끊는다. 무효 핸들이면 StopHandle 이 무시한다.
		Audio->StopHandle(SparkLoopHandle, SparkStopFadeSeconds);
		SparkLoopHandle = FMD_AudioHandle();
		return;
	}

	// 파열 지점에서 재생한다. BreakLocation 은 복제 속성이고 RepNotify 보다 먼저 적용된다.
	const FVector BreakWorldLocation = BreakLocation;

	// 처음부터 고장인 전선은 "끊기는 순간" 원샷이 없다. 스파크 루프만 켠다
	if (!bStartBroken && CutSoundTag.IsValid())
	{
		Audio->PlayEventAtLocation(CutSoundTag, BreakWorldLocation);
	}

	if (SparkLoopSoundTag.IsValid())
	{
		// 이미 돌고 있으면 겹치지 않게 먼저 끊는다.
		Audio->StopHandle(SparkLoopHandle, 0.0f);
		SparkLoopHandle = Audio->PlayEventAtLocation(SparkLoopSoundTag, BreakWorldLocation);
	}
}

void AMD_PowerConduit::OnBroken(AActor* Cutter)
{
	// 절단 즉시 감점. 처음부터 고장(Cutter == nullptr)이면 감점 없음
	if (Cutter != nullptr)
	{
		if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
		{
			GS->AddScoreEvent(MDGameplayTags::Score_Penalty_CableCut);
		}
	}

	// 담당 조명 그룹 소등 (연출은 BP)
	SetPowerGroupEnabled(false);
}

void AMD_PowerConduit::OnRepaired(AActor* Repairer)
{
	// 절연테이프 복구: 재점등
	SetPowerGroupEnabled(true);
}
