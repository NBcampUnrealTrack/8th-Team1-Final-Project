// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_MainQuestRowWidget.h"

#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Data/Merchant/MD_QuestDefinition.h"

void UMD_MainQuestRowWidget::SetMainQuest(const FMD_MainQuestState& InState)
{
	QuestState = InState;
	RefreshDisplay();
}

void UMD_MainQuestRowWidget::RefreshDisplay()
{
	const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
	const bool bHasDefinition = IsValid(Definition) == true && Definition->IsValidDefinition() == true && Definition->HasPhase(QuestState.CurrentPhaseIndex) == true;

	if (IsValid(TXT_DayLabel))
	{
		TXT_DayLabel->SetText(FText::FromString(FString::Printf(TEXT("Day %d"), QuestState.UnlockDay)));
	}
	if (IsValid(TXT_QuestName))
	{
		TXT_QuestName->SetText(bHasDefinition ? Definition->QuestName : FText::GetEmpty());
	}
	if (IsValid(TXT_Status))
	{
		const UEnum* StatusEnum = StaticEnum<EMD_MainQuestStatus>();
		FText StatusText = StatusEnum->GetDisplayNameTextByValue(static_cast<int64>(QuestState.Status));

		if (QuestState.Status == EMD_MainQuestStatus::Failed
			&& QuestState.FailureReason != EMD_QuestFailureReason::None)
		{
			const UEnum* ReasonEnum = StaticEnum<EMD_QuestFailureReason>();
			const FText ReasonText = ReasonEnum->GetDisplayNameTextByValue(
				static_cast<int64>(QuestState.FailureReason));
			StatusText = FText::Format(
				FText::FromString(TEXT("{0} ({1})")),
				StatusText,
				ReasonText);
		}

		TXT_Status->SetText(StatusText);
	}

	const bool bExpanded = QuestState.Status == EMD_MainQuestStatus::Active
		|| QuestState.Status == EMD_MainQuestStatus::Completed;

	if (IsValid(BDR_ExpandedDetail))
	{
		BDR_ExpandedDetail->SetVisibility(bExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_Description))
	{
		TXT_Description->SetText(bHasDefinition ? Definition->GetHUDDescription(QuestState.CurrentPhaseIndex) : FText::GetEmpty());
	}
	if (IsValid(TXT_Progress))
	{
		const int32 Required = bHasDefinition ? Definition->GetRequiredCount(QuestState.CurrentPhaseIndex) : 0;
		TXT_Progress->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), QuestState.CurrentProgress, Required)));
	}
	if (IsValid(TXT_IncompleteBadge))
	{
		TXT_IncompleteBadge->SetVisibility(
			QuestState.Status == EMD_MainQuestStatus::Active ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UMD_MainQuestRowWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshDisplay();
}
