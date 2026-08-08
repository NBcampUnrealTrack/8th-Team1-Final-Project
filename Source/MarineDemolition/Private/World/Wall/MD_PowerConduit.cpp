#include "World/Wall/MD_PowerConduit.h"

#include "Game/MD_GameStateBase.h"
#include "GameplayTags/MD_GameplayTags.h"

AMD_PowerConduit::AMD_PowerConduit()
{
	// TODO: RequiredRepairToolTag = 절연테이프 태그로 지정 (장비 시스템 연결 후)
}

void AMD_PowerConduit::OnBroken(AActor* Cutter)
{
	// 절단 즉시 감점
	if (AMD_GameStateBase* GS = GetWorld()->GetGameState<AMD_GameStateBase>())
	{
		GS->AddScoreEvent(MDGameplayTags::Score_Penalty_CableCut);
	}

	// 담당 조명 그룹 소등 (연출은 BP)
	SetPowerGroupEnabled(false);
}

void AMD_PowerConduit::OnRepaired(AActor* Repairer)
{
	// 절연테이프 복구: 재점등
	SetPowerGroupEnabled(true);
}
