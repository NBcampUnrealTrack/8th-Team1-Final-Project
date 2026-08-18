#include "Data/Merchant/MD_RepeatQuestDefinition.h"

bool FMD_RepeatQuestPhaseData::AcceptsSample(const FGameplayTag& GradeTag, bool bIntact) const
{
	// 빈 허용 목록은 모든 유효 등급을 받고 값이 있으면 정확히 등록된 등급만 받는 판정
	bool bGradeMatches = AcceptedGradeTags.Num() == 0 || AcceptedGradeTags.HasTagExact(GradeTag) == true;

	// 온전한 회수만 허용하는 페이즈에서 손상 샘플이 진행도를 올리지 못하게 하는 판정
	bool bConditionMatches = bAllowDamagedTarget == true || bIntact == true;

	bool bAccepted = GradeTag.IsValid() == true && bGradeMatches == true && bConditionMatches == true;
	return bAccepted;
}

UMD_RepeatQuestDefinition* UMD_RepeatQuestDefinition::LoadDefinition(const FPrimaryAssetId& DefinitionId)
{
	return Cast<UMD_RepeatQuestDefinition>(UMD_QuestDefinition::LoadDefinition(DefinitionId));
}

const FMD_RepeatQuestPhaseData* UMD_RepeatQuestDefinition::GetPhase(int32 PhaseIndex) const
{
	const FMD_RepeatQuestPhaseData* OutPhase = Phases.IsValidIndex(PhaseIndex) == true ? &Phases[PhaseIndex] : nullptr;
	return OutPhase;
}

bool UMD_RepeatQuestDefinition::IsObjectiveComplete(int32 PhaseIndex, int32 CurrentProgress) const
{
	const FMD_RepeatQuestPhaseData* CurrentPhase = GetPhase(PhaseIndex);
	bool bOutComplete = CurrentPhase != nullptr && PhaseIndex == Phases.Num() - 1 && CurrentProgress >= FMath::Max(CurrentPhase->RequiredCount, 1);
	return bOutComplete;
}
