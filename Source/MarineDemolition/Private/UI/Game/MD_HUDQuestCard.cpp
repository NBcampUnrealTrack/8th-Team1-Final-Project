#include "UI/Game/MD_HUDQuestCard.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void UMD_HUDQuestCard::SetMainQuest(const FMD_MainQuestState& QuestState)
{
	// 메인 퀘스트임을 명시하여 데이터 에셋의 종류 입력 오류와 무관한 HUD 표시 보장
	ApplyQuestData(EMD_QuestKind::Main, QuestState.Definition, QuestState.CurrentProgress);
}

void UMD_HUDQuestCard::SetRepeatQuest(const FMD_RepeatQuestState& QuestState)
{
	// 반복 퀘스트임을 명시하여 수락 목록의 카드 종류 표시 보장
	ApplyQuestData(EMD_QuestKind::Repeat, QuestState.Definition, QuestState.CurrentProgress);
}

void UMD_HUDQuestCard::ApplyQuestData(EMD_QuestKind QuestKind, const FMD_QuestDefinitionData& Definition, int32 CurrentProgress)
{
	// 카드 상단에서 메인과 반복 퀘스트를 즉시 구분하기 위한 종류 텍스트
	FText QuestKindText = QuestKind == EMD_QuestKind::Main ? FText::FromString(TEXT("메인")) : FText::FromString(TEXT("반복"));
	if (IsValid(Txt_QuestKind) == true)
	{
		Txt_QuestKind->SetText(QuestKindText);
	}

	if (IsValid(Txt_QuestName) == true)
	{
		// 퀘스트 제목 전용 영역에 정의된 이름 적용
		Txt_QuestName->SetText(Definition.QuestName);
	}

	if (IsValid(Txt_QuestDescription) == true)
	{
		// 퀘스트 본문 전용 영역에 정의된 목표 설명 적용
		Txt_QuestDescription->SetText(Definition.Description);
	}

	if (IsValid(PB_QuestProgressRate) == true)
	{
		// 잘못된 목표 수량에서도 0으로 나누지 않도록 보정한 진행률 계산
		int32 RequiredCount = FMath::Max(Definition.RequiredCount, 1);
		int32 ClampedProgress = FMath::Clamp(CurrentProgress, 0, RequiredCount);
		float ProgressRate = FMath::SafeDivide<float, float>(RequiredCount, ClampedProgress);
		PB_QuestProgressRate->SetPercent(ProgressRate);
	}
}
