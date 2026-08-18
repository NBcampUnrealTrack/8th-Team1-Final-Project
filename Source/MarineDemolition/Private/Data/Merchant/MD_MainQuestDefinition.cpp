#include "Data/Merchant/MD_MainQuestDefinition.h"

UMD_MainQuestDefinition::UMD_MainQuestDefinition()
{
	// 반복 퀘스트와 같은 Asset Manager 스캔 그룹에서 클래스 캐스트로 종류를 구분한다.
	QuestPrimaryAssetType = FPrimaryAssetType(TEXT("Quest"));
}

UMD_MainQuestDefinition* UMD_MainQuestDefinition::LoadDefinition(const FPrimaryAssetId& DefinitionId)
{
	return Cast<UMD_MainQuestDefinition>(UMD_QuestDefinition::LoadDefinition(DefinitionId));
}

bool UMD_MainQuestDefinition::IsValidDefinition() const
{
	return GetPrimaryAssetId().IsValid() == true && RequiredCount > 0;
}

bool UMD_MainQuestDefinition::AcceptsSample(const FGameplayTag& GradeTag, bool bIntact) const
{
	const bool bIsRecovery = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact
		|| ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	const bool bAcceptsCondition = ObjectiveType != EMD_QuestObjectiveType::RecoverIntact || bIntact == true;
	return bIsRecovery == true && bAcceptsCondition == true && GradeTag.IsValid() == true
		&& TargetGradeTag.MatchesTagExact(GradeTag) == true;
}

bool UMD_MainQuestDefinition::TargetsGrade(const FGameplayTag& GradeTag) const
{
	const bool bIsRecovery = ObjectiveType == EMD_QuestObjectiveType::RecoverIntact
		|| ObjectiveType == EMD_QuestObjectiveType::RecoverDamaged;
	return bIsRecovery == true && GradeTag.IsValid() == true
		&& TargetGradeTag.MatchesTagExact(GradeTag) == true;
}
