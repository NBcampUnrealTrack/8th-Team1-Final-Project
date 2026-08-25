#include "UI/Game/MD_HUDQuestCard.h"

#include "Components/TextBlock.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "UI/Merchant/MD_MerchantUITypes.h"

void UMD_HUDQuestCard::SetMainQuest(const FMD_MainQuestState& QuestState)
{
	// 메인 퀘스트 상태에서 카드 표시에 필요한 공통 값만 전달한다.
	SetQuest(QuestState.DefinitionId, QuestState.CurrentPhaseIndex, QuestState.CurrentProgress, EMD_QuestTab::Main);
}

void UMD_HUDQuestCard::SetRepeatQuest(const FMD_RepeatQuestState& QuestState)
{
	// 반복 퀘스트 상태에서 카드 표시에 필요한 공통 값만 전달한다.
	SetQuest(QuestState.DefinitionId, QuestState.CurrentPhaseIndex, QuestState.CurrentProgress, EMD_QuestTab::Repeat);
}

void UMD_HUDQuestCard::SetQuest(FPrimaryAssetId DefinitionId, int32 PhaseIndex, int32 CurrentProgress, EMD_QuestTab QuestType)
{
	// 공통 식별자로 퀘스트 표시 원본을 불러온다.
	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(DefinitionId);

	// 원본을 불러오지 못한 카드에는 잘못된 정보를 표시하지 않는다.
	// 존재하지 않는 페이즈의 진행 정보는 카드에 적용하지 않는다.
	if (IsValid(Definition) == false || Definition->HasPhase(PhaseIndex) == false)
	{
		return;
	}

	// 기본 종류 문구는 메인 퀘스트로 준비한다.
	FText QuestKindText = FText::FromString(TEXT("메인"));
	// 반복 퀘스트인 경우에만 종류 문구를 반복으로 교체한다.
	if (QuestType == EMD_QuestTab::Repeat)
	{
		QuestKindText = FText::FromString(TEXT("반복"));
	}
	SetTextPair(Txt_QuestKind, Txt_QuestKind_Shadow, QuestKindText);

	// 공통 퀘스트 제목을 본문과 그림자에 적용한다.
	SetTextPair(Txt_QuestName, Txt_QuestName_Shadow, Definition->QuestName);

	// 현재 페이즈의 목표 설명을 새 카드의 본문과 그림자에 적용한다.
	FText QuestDescriptionText = Definition->GetHUDDescription(PhaseIndex);
	SetTextPair(Txt_QuestDescription, Txt_QuestDescription_Shadow, QuestDescriptionText);

	// 목표 수량을 보정한 뒤 숫자 진행도로 표시한다.
	int32 RequiredCount = FMath::Max(Definition->GetRequiredCount(PhaseIndex), 1);
	SetProgressText(CurrentProgress, RequiredCount);

	// Blueprint가 퀘스트 종류에 맞는 아이콘과 색상을 적용하도록 요청한다.
	SetQuestCardType(QuestType);
}

void UMD_HUDQuestCard::SetTextPair(UTextBlock* TextWidget, UTextBlock* ShadowWidget, FText Text)
{
	// 앞쪽 본문 텍스트가 존재하면 표시 내용을 적용한다.
	if (IsValid(TextWidget) == true)
	{
		TextWidget->SetText(Text);
	}

	// 뒤쪽 그림자 텍스트가 존재하면 본문과 같은 내용을 적용한다.
	if (IsValid(ShadowWidget) == true)
	{
		ShadowWidget->SetText(Text);
	}
}

void UMD_HUDQuestCard::SetProgressText(int32 CurrentProgress, int32 RequiredCount)
{
	// 목표 수량 범위 안에서 표시할 현재 진행도를 보정한다.
	int32 ClampedProgress = FMath::Clamp(CurrentProgress, 0, RequiredCount);

	// 새 카드가 사용하는 현재 수량/목표 수량 형식의 텍스트를 만든다.
	FText ProgressText = FText::Format(NSLOCTEXT("MDHUDQuestCard", "QuestProgress", "{0}/{1}"), ClampedProgress, RequiredCount);

	// 진행도 본문과 그림자에 같은 값을 적용한다.
	SetTextPair(Txt_Progress, Txt_Progress_Shadow, ProgressText);
}
