#include "UI/Merchant/MD_QuestCardWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Data/Merchant/MD_RepeatQuestDefinition.h"

void UMD_QuestCardWidget::SetQuest(int32 InQuestIndex, const FMD_RepeatQuestState& InState)
{
	QuestIndex = InQuestIndex;
	QuestState = InState;
	RefreshDisplay();
}

void UMD_QuestCardWidget::RefreshDisplay()
{
	const UMD_RepeatQuestDefinition* Definition = UMD_RepeatQuestDefinition::LoadDefinition(QuestState.DefinitionId);
	const FMD_RepeatQuestPhaseData* CurrentPhase = IsValid(Definition) == true ? Definition->GetPhase(QuestState.CurrentPhaseIndex) : nullptr;
	const bool bHasDefinition = IsValid(Definition) == true && CurrentPhase != nullptr;
	const bool bObjectiveComplete = IsValid(Definition) == true && Definition->IsObjectiveComplete(QuestState.CurrentPhaseIndex, QuestState.CurrentProgress) == true;

	if (IsValid(TXT_QuestName) == true)
	{
		TXT_QuestName->SetText(bHasDefinition == true ? Definition->QuestName : FText::GetEmpty());
	}
	if (IsValid(TXT_Description) == true)
	{
		// 최종 완료는 현재 페이즈를 사용하고 다음 페이즈 진행 중에는 직전 페이즈를 사용하는 파생 인덱스
		int32 CompletedPhaseIndex = INDEX_NONE;
		if (bObjectiveComplete == true || QuestState.Status == EMD_RepeatQuestStatus::Completed)
		{
			CompletedPhaseIndex = QuestState.CurrentPhaseIndex;
		}
		else if (QuestState.CurrentPhaseIndex > 0)
		{
			CompletedPhaseIndex = QuestState.CurrentPhaseIndex - 1;
		}
		const FMD_RepeatQuestPhaseData* CompletedPhase = IsValid(Definition) == true ? Definition->GetPhase(CompletedPhaseIndex) : nullptr;
		FText DisplayDialogue = IsValid(Definition) == true ? Definition->OfferDialogue : FText::GetEmpty();
		if (CompletedPhase != nullptr)
		{
			DisplayDialogue = CompletedPhase->CompletionDialogue;
		}
		TXT_Description->SetText(bHasDefinition == true ? DisplayDialogue : FText::GetEmpty());
	}
	if (IsValid(TXT_GradeBadge) == true)
	{
		TXT_GradeBadge->SetText(bHasDefinition == true ? BuildGradeBadgeText(CurrentPhase->AcceptedGradeTags) : FText::GetEmpty());
	}
	if (IsValid(TXT_ObjectiveType) == true)
	{
		FText ObjectiveText = bHasDefinition == true ? FText::FromString(CurrentPhase->bAllowDamagedTarget == true ? TEXT("손상 허용 회수") : TEXT("회수")) : FText::GetEmpty();
		TXT_ObjectiveType->SetText(ObjectiveText);
	}
	if (IsValid(TXT_Progress) == true)
	{
		const int32 Required = bHasDefinition == true ? FMath::Max(CurrentPhase->RequiredCount, 1) : 0;
		TXT_Progress->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), QuestState.CurrentProgress, Required)));
	}
	if (IsValid(TXT_RewardCredits) == true)
	{
		TXT_RewardCredits->SetText(FText::FromString(FString::Printf(TEXT("%d₡"), bHasDefinition == true ? Definition->SuccessReward.Credits : 0)));
	}
	if (IsValid(TXT_Status) == true)
	{
		const UEnum* StatusEnum = StaticEnum<EMD_RepeatQuestStatus>();
		// Enum을 늘리지 않고 목표 진행도에서 파생한 보상 수령 대기 상태를 사용자에게 표시하는 처리
		FText StatusText = bObjectiveComplete == true && QuestState.Status == EMD_RepeatQuestStatus::Accepted ? FText::FromString(TEXT("보상 수령 대기")) : StatusEnum->GetDisplayNameTextByValue(static_cast<int64>(QuestState.Status));
		TXT_Status->SetText(StatusText);
	}

	// 서로 분리된 수락·보상 버튼을 동일 서버 판정 경로로 보내기 위한 상태 구분
	const bool bOffered = QuestState.Status == EMD_RepeatQuestStatus::Offered;
	const bool bAccepted = QuestState.Status == EMD_RepeatQuestStatus::Accepted;
	const bool bRewardClaimable = bAccepted == true && bObjectiveComplete == true;

	if (IsValid(BTN_Reward) == true)
	{
		BTN_Reward->SetVisibility(bRewardClaimable == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(BTN_Accept) == true)
	{
		BTN_Accept->SetVisibility(bOffered == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(BTN_Decline) == true)
	{
		BTN_Decline->SetVisibility(bOffered == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(BTN_Abandon) == true)
	{
		BTN_Abandon->SetVisibility(bAccepted == true && bRewardClaimable == false ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

FText UMD_QuestCardWidget::BuildGradeBadgeText(const FGameplayTagContainer& GradeTags) const
{
	TArray<FString> GradeNames;
	for (const FGameplayTag& GradeTag : GradeTags.GetGameplayTagArray())
	{
		FString GradeName = GradeTag.ToString();
		int32 LastDotIndex = INDEX_NONE;
		if (GradeName.FindLastChar(TEXT('.'), LastDotIndex) == true)
		{
			GradeName = GradeName.RightChop(LastDotIndex + 1);
		}
		GradeNames.Add(GradeName);
	}

	FText OutBadgeText = FText::FromString(FString::Join(GradeNames, TEXT("/")));
	return OutBadgeText;
}

void UMD_QuestCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(BTN_Reward) == true)
	{
		BTN_Reward->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleRewardClicked);
	}
	if (IsValid(BTN_Accept) == true)
	{
		BTN_Accept->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleAcceptClicked);
	}
	if (IsValid(BTN_Decline) == true)
	{
		BTN_Decline->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleDeclineClicked);
	}
	if (IsValid(BTN_Abandon) == true)
	{
		BTN_Abandon->OnClicked.AddDynamic(this, &UMD_QuestCardWidget::HandleAbandonClicked);
	}

	RefreshDisplay();
}

void UMD_QuestCardWidget::HandleRewardClicked()
{
	// 목표 달성 상태를 재검증하고 보상을 지급하는 기존 서버 요청 경로의 재사용
	OnAcceptRequested.Broadcast(QuestIndex);
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
