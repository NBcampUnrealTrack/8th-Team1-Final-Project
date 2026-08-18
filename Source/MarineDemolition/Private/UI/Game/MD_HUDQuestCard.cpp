#include "UI/Game/MD_HUDQuestCard.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Data/Merchant/MD_MainQuestDefinition.h"
#include "Data/Merchant/MD_RepeatQuestDefinition.h"

void UMD_HUDQuestCard::SetMainQuest(const FMD_MainQuestState& QuestState)
{
	const UMD_MainQuestDefinition* Definition = UMD_MainQuestDefinition::LoadDefinition(QuestState.DefinitionId);
	if (IsValid(Definition) == true)
	{
		ApplyMainQuestData(*Definition, QuestState.CurrentProgress);
	}
}

void UMD_HUDQuestCard::SetRepeatQuest(const FMD_RepeatQuestState& QuestState)
{
	// 반복 퀘스트 상태의 PrimaryAssetId와 현재 페이즈를 HUD 표시 정보로 적용하는 처리
	ApplyRepeatQuestData(QuestState);
}

void UMD_HUDQuestCard::ApplyRepeatQuestData(const FMD_RepeatQuestState& QuestState)
{
	const UMD_RepeatQuestDefinition* Definition = UMD_RepeatQuestDefinition::LoadDefinition(QuestState.DefinitionId);
	const FMD_RepeatQuestPhaseData* CurrentPhase = IsValid(Definition) == true ? Definition->GetPhase(QuestState.CurrentPhaseIndex) : nullptr;

	if (IsValid(Txt_QuestKind) == true)
	{
		Txt_QuestKind->SetText(FText::FromString(TEXT("반복")));
	}

	if (IsValid(Txt_QuestName) == true)
	{
		Txt_QuestName->SetText(IsValid(Definition) == true ? Definition->QuestName : FText::GetEmpty());
	}

	if (IsValid(Txt_QuestDescription) == true)
	{
		// 에셋에서 현재 페이즈에 직접 작성한 HUD 목표 내용만 표시하는 본문
		FText PhaseDescription = CurrentPhase != nullptr ? CurrentPhase->HUDDescription : FText::GetEmpty();
		Txt_QuestDescription->SetText(PhaseDescription);
	}

	if (IsValid(PB_QuestProgressRate) == true)
	{
		int32 RequiredCount = CurrentPhase != nullptr ? FMath::Max(CurrentPhase->RequiredCount, 1) : 1;
		int32 ClampedProgress = FMath::Clamp(QuestState.CurrentProgress, 0, RequiredCount);
		float ProgressRate = FMath::SafeDivide<float, float>(ClampedProgress, RequiredCount);
		PB_QuestProgressRate->SetPercent(ProgressRate);
	}
}

void UMD_HUDQuestCard::ApplyMainQuestData(const UMD_MainQuestDefinition& Definition, int32 CurrentProgress)
{
	if (IsValid(Txt_QuestKind) == true)
	{
		// 메인 퀘스트 전용 경로에서 카드 종류를 고정하는 표시
		Txt_QuestKind->SetText(FText::FromString(TEXT("메인")));
	}

	if (IsValid(Txt_QuestName) == true)
	{
		// 퀘스트 제목 전용 영역에 정의된 이름 적용
		Txt_QuestName->SetText(Definition.QuestName);
	}

	if (IsValid(Txt_QuestDescription) == true)
	{
		// 퀘스트 본문 전용 영역에 정의된 목표 설명 적용
		Txt_QuestDescription->SetText(Definition.HUDDescription);
	}

	if (IsValid(PB_QuestProgressRate) == true)
	{
		// 잘못된 목표 수량에서도 0으로 나누지 않도록 보정한 진행률 계산
		int32 RequiredCount = FMath::Max(Definition.RequiredCount, 1);
		int32 ClampedProgress = FMath::Clamp(CurrentProgress, 0, RequiredCount);
		float ProgressRate = FMath::SafeDivide<float, float>(ClampedProgress, RequiredCount);
		PB_QuestProgressRate->SetPercent(ProgressRate);
	}
}
