#include "Data/Merchant/MD_RepeatQuestDefinition.h"

FMD_QuestDefinitionData UMD_RepeatQuestDefinition::BuildRuntimeDefinition() const
{
	FMD_QuestDefinitionData OutDefinition;
	OutDefinition.QuestId = GetFName();
	OutDefinition.QuestName = QuestName;
	OutDefinition.Kind = EMD_QuestKind::Repeat;
	OutDefinition.ObjectiveType = EMD_QuestObjectiveType::RecoverIntact;
	OutDefinition.RequiredCount = 1;
	OutDefinition.RewardCredits = SuccessReward.Credits;

	// 기존 단일 목표 원장에는 반복 퀘스트의 최초 활성 페이즈만 연결하는 호환 처리
	if (Phases.Num() > 0)
	{
		const FMD_RepeatQuestPhaseData& InitialPhase = Phases[0];
		OutDefinition.ObjectiveType = InitialPhase.bAllowDamagedTarget == true ? EMD_QuestObjectiveType::RecoverDamaged : EMD_QuestObjectiveType::RecoverIntact;
		OutDefinition.RequiredCount = FMath::Max(InitialPhase.RequiredCount, 1);

		const TArray<FGameplayTag>& AcceptedGradeTags = InitialPhase.AcceptedGradeTags.GetGameplayTagArray();
		if (AcceptedGradeTags.Num() > 0)
		{
			OutDefinition.TargetGradeTag = AcceptedGradeTags[0];
		}
	}

	return OutDefinition;
}
