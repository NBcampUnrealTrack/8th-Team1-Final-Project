// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_QuestCardWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

namespace
{
	/** `Resource.Sample.Grade.C` 같은 태그에서 마지막 세그먼트만 배지로 뽑아낸다. */
	FString ExtractGradeBadge(const FGameplayTag& GradeTag)
	{
		if (!GradeTag.IsValid())
		{
			return FString();
		}

		FString TagString = GradeTag.ToString();
		int32 LastDotIndex = INDEX_NONE;
		if (TagString.FindLastChar(TEXT('.'), LastDotIndex))
		{
			return TagString.RightChop(LastDotIndex + 1);
		}

		return TagString;
	}
}

void UMD_QuestCardWidget::SetQuest(int32 InQuestIndex, const FMD_RepeatQuestState& InState)
{
	QuestIndex = InQuestIndex;
	QuestState = InState;
	RefreshDisplay();
}

void UMD_QuestCardWidget::RefreshDisplay()
{
	const FMD_QuestDefinitionData& Definition = QuestState.Definition;
	const bool bHasDefinition = Definition.IsValidDefinition();

	if (IsValid(TXT_QuestName))
	{
		TXT_QuestName->SetText(bHasDefinition ? Definition.QuestName : FText::GetEmpty());
	}
	if (IsValid(TXT_Description))
	{
		TXT_Description->SetText(bHasDefinition ? Definition.Description : FText::GetEmpty());
	}
	if (IsValid(TXT_GradeBadge))
	{
		TXT_GradeBadge->SetText(bHasDefinition
			? FText::FromString(ExtractGradeBadge(Definition.TargetGradeTag))
			: FText::GetEmpty());
	}
	if (IsValid(TXT_ObjectiveType))
	{
		if (bHasDefinition)
		{
			const UEnum* ObjectiveEnum = StaticEnum<EMD_QuestObjectiveType>();
			TXT_ObjectiveType->SetText(ObjectiveEnum->GetDisplayNameTextByValue(static_cast<int64>(Definition.ObjectiveType)));
		}
		else
		{
			TXT_ObjectiveType->SetText(FText::GetEmpty());
		}
	}
	if (IsValid(TXT_Progress))
	{
		const int32 Required = bHasDefinition ? Definition.RequiredCount : 0;
		TXT_Progress->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), QuestState.CurrentProgress, Required)));
	}
	if (IsValid(TXT_RewardCredits))
	{
		TXT_RewardCredits->SetText(FText::FromString(
			FString::Printf(TEXT("%d₡"), bHasDefinition ? Definition.RewardCredits : 0)));
	}
	if (IsValid(TXT_Status))
	{
		const UEnum* StatusEnum = StaticEnum<EMD_RepeatQuestStatus>();
		TXT_Status->SetText(StatusEnum->GetDisplayNameTextByValue(static_cast<int64>(QuestState.Status)));
	}

	// 상태별로 버튼 하나만 노출한다: 제시(Offered) → 수락/거절, 수락(Accepted) → 포기.
	const bool bOffered = QuestState.Status == EMD_RepeatQuestStatus::Offered;
	const bool bAccepted = QuestState.Status == EMD_RepeatQuestStatus::Accepted;

	if (IsValid(BTN_Accept))
	{
		BTN_Accept->SetVisibility(bOffered ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(BTN_Decline))
	{
		BTN_Decline->SetVisibility(bOffered ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(BTN_Abandon))
	{
		BTN_Abandon->SetVisibility(bAccepted ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UMD_QuestCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(BTN_Accept))
	{
		BTN_Accept->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleAcceptClicked);
	}
	if (IsValid(BTN_Decline))
	{
		BTN_Decline->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleDeclineClicked);
	}
	if (IsValid(BTN_Abandon))
	{
		BTN_Abandon->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleAbandonClicked);
	}

	RefreshDisplay();
}

void UMD_QuestCardWidget::HandleAcceptClicked()
{
	OnAcceptRequested.Broadcast(QuestIndex);
}

void UMD_QuestCardWidget::HandleDeclineClicked()
{
	OnDeclineRequested.Broadcast(QuestIndex);
}

void UMD_QuestCardWidget::HandleAbandonClicked()
{
	OnAbandonRequested.Broadcast(QuestIndex);
}
