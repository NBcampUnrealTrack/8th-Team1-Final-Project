#include "Data/Merchant/MD_RepeatQuestDefinition.h"

#include "Engine/AssetManager.h"

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
	UMD_RepeatQuestDefinition* OutDefinition = nullptr;
	if (DefinitionId.IsValid() == false)
	{
		return OutDefinition;
	}

	// 이미 로드된 원본을 우선 사용하여 카드 갱신마다 불필요한 패키지 로드를 막는 처리
	UAssetManager& AssetManager = UAssetManager::Get();
	OutDefinition = Cast<UMD_RepeatQuestDefinition>(AssetManager.GetPrimaryAssetObject(DefinitionId));
	if (IsValid(OutDefinition) == false)
	{
		const FSoftObjectPath DefinitionPath = AssetManager.GetPrimaryAssetPath(DefinitionId);
		OutDefinition = Cast<UMD_RepeatQuestDefinition>(DefinitionPath.TryLoad());
	}

	// 잘못 등록된 다른 타입의 에셋이 반복 퀘스트 상태에 연결되지 않도록 하는 최종 검사
	if (IsValid(OutDefinition) == true && OutDefinition->GetPrimaryAssetId() != DefinitionId)
	{
		OutDefinition = nullptr;
	}

	return OutDefinition;
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
