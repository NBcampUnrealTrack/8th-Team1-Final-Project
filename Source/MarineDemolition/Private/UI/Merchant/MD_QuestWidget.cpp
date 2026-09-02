// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_QuestWidget.h"

#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Groups/CommonButtonGroupBase.h"
#include "UI/MD_CommonButtonBase.h"
#include "UI/Merchant/MD_QuestCardWidget.h"

void UMD_QuestWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 상세 패널 액션 버튼을 퀘스트 탭의 단일 처리 함수에 연결
	if (IsValid(Btn_Accept) == true)
	{
		Btn_Accept->OnClicked().AddUObject(this, &UMD_QuestWidget::HandleAcceptClicked);
	}
	if (IsValid(Btn_Reject) == true)
	{
		Btn_Reject->OnClicked().AddUObject(this, &UMD_QuestWidget::HandleRejectClicked);
	}
	if (IsValid(Btn_Reward) == true)
	{
		Btn_Reward->OnClicked().AddUObject(this, &UMD_QuestWidget::HandleRewardClicked);
	}
	if (IsValid(Btn_Giveup) == true)
	{
		Btn_Giveup->OnClicked().AddUObject(this, &UMD_QuestWidget::HandleGiveupClicked);
	}

	// 에디터 기본값 또는 화면 생성 직후 전달된 데이터로 최초 목록 구성
	RefreshDisplay();
}

void UMD_QuestWidget::NativeDestruct()
{
	// 풀링 또는 재생성 시 동일 클릭 함수가 중복 연결되지 않도록 해제
	if (IsValid(Btn_Accept) == true)
	{
		Btn_Accept->OnClicked().RemoveAll(this);
	}
	if (IsValid(Btn_Reject) == true)
	{
		Btn_Reject->OnClicked().RemoveAll(this);
	}
	if (IsValid(Btn_Reward) == true)
	{
		Btn_Reward->OnClicked().RemoveAll(this);
	}
	if (IsValid(Btn_Giveup) == true)
	{
		Btn_Giveup->OnClicked().RemoveAll(this);
	}

	// 생성 카드가 보관한 탭 참조를 위젯 수명 종료 전에 해제
	for (UMD_QuestCardWidget* QuestCard : QuestCards)
	{
		if (IsValid(QuestCard) == true)
		{
			QuestCard->OnClicked().RemoveAll(this);
		}
	}

	// 위젯 수명 종료 후 생성 카드가 선택 그룹에 남지 않도록 참조 해제
	if (IsValid(QuestCardButtonGroup) == true)
	{
		QuestCardButtonGroup->RemoveAll();
	}

	Super::NativeDestruct();
}

void UMD_QuestWidget::SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests)
{
	// 복제된 메인 퀘스트 상태를 화면 데이터로 교체
	MainQuests = InMainQuests;
	RefreshDisplay();
}

void UMD_QuestWidget::SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests)
{
	// 복제된 반복 퀘스트 상태를 화면 데이터로 교체
	RepeatQuests = InRepeatQuests;
	RefreshDisplay();
}

void UMD_QuestWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds)
{
	// 현재 탐사 현장의 정적 정의와 플레이어별 수집 원장을 함께 보관하는 처리
	LoreCatalog = InLoreCatalog;
	DiscoveredLoreAssetIds = InDiscoveredLoreAssetIds;
	// 이미 열린 UI에서 수집 변경을 즉시 반영하기 위한 전용 탭 갱신
	EnsureLoreTabWidget();
	RefreshLoreDisplay();
}

	// 선택 필수 그룹에서 대상 카드만 선택하여 나머지 카드를 자동 해제
	if (IsValid(QuestCardButtonGroup) == true)
	{
		if (IsValid(SelectedQuestCard) == true)
		{
			int32 QuestCardIndex = QuestCardButtonGroup->FindButtonIndex(SelectedQuestCard);
			if (QuestCardIndex != INDEX_NONE)
			{
				QuestCardButtonGroup->SelectButtonAtIndex(QuestCardIndex, false);
			}
		}
	}
	else
	{
		// 그룹 생성 실패 시에도 기존 단일 선택 동작을 유지하는 예비 처리
		for (UMD_QuestCardWidget* CurrentQuestCard : QuestCards)
		{
			if (IsValid(CurrentQuestCard) == true)
			{
				bool bSelected = CurrentQuestCard == SelectedQuestCard;
				CurrentQuestCard->SetIsSelected(bSelected, false);
			}
		}
	}

	RefreshQuestDetail();
}

void UMD_QuestWidget::RebuildQuestList()
{
	// 디자이너 필수 스크롤 목록이 유효하지 않으면 생성 작업 중단
	if (IsValid(SB_QuestList) == false)
	{
		return;
	}

	// 데이터 갱신 후에도 같은 종류와 인덱스의 선택을 복원하기 위한 값 보관
	bool bHadSelection = IsValid(SelectedQuestCard) == true;
	bool bSelectedMainQuest = bHadSelection == true ? SelectedQuestCard->GetIsMainQuest() : true;
	int32 SelectedQuestIndex = bHadSelection == true ? SelectedQuestCard->GetQuestIndex() : INDEX_NONE;

	// 모든 퀘스트 카드를 동일한 선택 필수 그룹에 등록하기 위한 그룹 준비
	if (IsValid(QuestCardButtonGroup) == false)
	{
		QuestCardButtonGroup = NewObject<UCommonButtonGroupBase>(this);
	}
	if (IsValid(QuestCardButtonGroup) == true)
	{
		QuestCardButtonGroup->RemoveAll();
		QuestCardButtonGroup->SetSelectionRequired(true);
	}

	// 기존 카드의 클릭 연결을 해제하고 디자이너 목업을 실제 데이터 카드로 교체
	for (UMD_QuestCardWidget* QuestCard : QuestCards)
	{
		if (IsValid(QuestCard) == true)
		{
			QuestCard->OnClicked().RemoveAll(this);
		}
	}
	SB_QuestList->ClearChildren();
	QuestCards.Empty();
	SelectedQuestCard = nullptr;

	// 카드 클래스가 지정되지 않은 화면에서는 빈 목록과 상세 상태 유지
	if (QuestCardClass == nullptr)
	{
		RefreshQuestDetail();
		return;
	}

	// 메인 퀘스트를 원본 배열 순서대로 목록 앞부분에 생성
	for (int32 QuestIndex = 0; QuestIndex < MainQuests.Num(); ++QuestIndex)
	{
		UMD_QuestCardWidget* QuestCard = CreateWidget<UMD_QuestCardWidget>(this, QuestCardClass);
		if (IsValid(QuestCard) == false)
		{
			continue;
		}

		QuestCard->SetMainQuest(QuestIndex, MainQuests[QuestIndex]);
		// 메인 퀘스트 카드를 단일 선택 그룹에 등록
		if (IsValid(QuestCardButtonGroup) == true)
		{
			QuestCardButtonGroup->AddWidget(QuestCard);
		}
		QuestCard->OnClicked().AddUObject(this, &UMD_QuestWidget::HandleQuestCardClicked, QuestCard);
		SB_QuestList->AddChild(QuestCard);
		QuestCards.Add(QuestCard);
	}

	// 반복 퀘스트를 원본 배열 순서대로 메인 퀘스트 뒤에 생성
	for (int32 QuestIndex = 0; QuestIndex < RepeatQuests.Num(); ++QuestIndex)
	{
		UMD_QuestCardWidget* QuestCard = CreateWidget<UMD_QuestCardWidget>(this, QuestCardClass);
		if (IsValid(QuestCard) == false)
		{
			continue;
		}

		QuestCard->SetRepeatQuest(QuestIndex, RepeatQuests[QuestIndex]);
		// 반복 퀘스트 카드를 메인 퀘스트 카드와 같은 단일 선택 그룹에 등록
		if (IsValid(QuestCardButtonGroup) == true)
		{
			QuestCardButtonGroup->AddWidget(QuestCard);
		}
		QuestCard->OnClicked().AddUObject(this, &UMD_QuestWidget::HandleQuestCardClicked, QuestCard);
		SB_QuestList->AddChild(QuestCard);
		QuestCards.Add(QuestCard);
	}

	// 이전 선택과 같은 카드가 남아 있으면 우선 복원
	UMD_QuestCardWidget* QuestCardToSelect = nullptr;
	for (UMD_QuestCardWidget* QuestCard : QuestCards)
	{
		if (IsValid(QuestCard) == false)
		{
			continue;
		}
		bool bMainQuestCard = QuestCard->GetIsMainQuest();
		if (bMainQuestCard == bSelectedMainQuest)
		{
			int32 QuestIndex = QuestCard->GetQuestIndex();
			if (QuestIndex == SelectedQuestIndex)
			{
				QuestCardToSelect = QuestCard;
				break;
			}
		}
	}

	// 복원할 선택이 없으면 첫 번째 퀘스트를 기본 상세 대상으로 사용
	if (IsValid(QuestCardToSelect) == false)
	{
		int32 QuestCardCount = QuestCards.Num();
		if (QuestCardCount > 0)
		{
			QuestCardToSelect = QuestCards[0];
		}
	}
	SelectQuestCard(QuestCardToSelect);
}

void UMD_QuestWidget::RefreshQuestDetail()
{
	// 선택된 카드가 없을 때 사용할 빈 상세 표시 값
	FText QuestTypeText = FText::GetEmpty();
	FText QuestNameText = FText::FromString(TEXT("선택된 퀘스트 없음"));
	FText QuestProgressText = FText::GetEmpty();
	FText QuestDescriptionText = FText::FromString(TEXT("퀘스트를 선택하면 상세 정보가 표시됩니다."));
	FText AffinityRewardText = FText::FromString(TEXT("-"));
	FText ScoreRewardText = FText::FromString(TEXT("-"));
	FText CreditRewardText = FText::FromString(TEXT("-"));
	bool bShowAccept = false;
	bool bShowReject = false;
	bool bShowReward = false;
	bool bShowGiveup = false;

	// 선택 카드의 종류에 맞는 상태와 원본 퀘스트 정의 조회
	UMD_QuestDefinition* Definition = nullptr;
	int32 PhaseIndex = 0;
	int32 CurrentProgress = 0;
	bool bMainQuest = false;
	if (IsValid(SelectedQuestCard) == true)
	{
		// 카탈로그가 자신의 항목만 골라 표시하도록 전역 발견 원장을 그대로 전달한다.
		LoreQuestTabWidget->SetLoreData(LoreCatalog, DiscoveredLoreAssetIds);
		return;
	}

	// 유효한 현재 페이즈에서 상세 본문과 성공 보상 계산
	bool bHasDefinition = IsValid(Definition);
	if (bHasDefinition == true)
	{
		bHasDefinition = Definition->HasPhase(PhaseIndex);
	}

	// 전역 로어 원장의 영구 누적 수집량을 안내에 사용한다.
	int32 DiscoveredLoreCount = DiscoveredLoreAssetIds.Num();

	// 계산된 상세 텍스트를 실제 디자이너 위젯에 반영
	if (IsValid(Txt_DetailType) == true)
	{
		Txt_DetailType->SetText(QuestTypeText);
	}
	if (IsValid(Txt_DetailName) == true)
	{
		Txt_DetailName->SetText(QuestNameText);
	}
	if (IsValid(Txt_DetailProgress) == true)
	{
		Txt_DetailProgress->SetText(QuestProgressText);
	}
	if (IsValid(Txt_DetailDescription) == true)
	{
		Txt_DetailDescription->SetText(QuestDescriptionText);
	}
	if (IsValid(Txt_AffinityReward) == true)
	{
		Txt_AffinityReward->SetText(AffinityRewardText);
	}
	if (IsValid(Txt_ScoreReward) == true)
	{
		Txt_ScoreReward->SetText(ScoreRewardText);
	}
	if (IsValid(Txt_CreditReward) == true)
	{
		Txt_CreditReward->SetText(CreditRewardText);
	}

	// 디자이너가 종류별 아이콘을 지정한 경우에만 상세 아이콘 교체
	if (IsValid(Img_DetailType) == true)
	{
		if (IsValid(SelectedQuestCard) == true)
		{
			FSlateBrush QuestIcon = bMainQuest == true ? MainQuestIcon : RepeatQuestIcon;
			UObject* QuestIconResource = QuestIcon.GetResourceObject();
			if (IsValid(QuestIconResource) == true)
			{
				Img_DetailType->SetBrush(QuestIcon);
			}
		}
	}

	// 선택된 반복 퀘스트 상태에 맞는 액션만 노출
	if (IsValid(Btn_Accept) == true)
	{
		Btn_Accept->SetVisibility(bShowAccept == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Btn_Reject) == true)
	{
		Btn_Reject->SetVisibility(bShowReject == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Btn_Reward) == true)
	{
		Btn_Reward->SetVisibility(bShowReward == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(Btn_Giveup) == true)
	{
		Btn_Giveup->SetVisibility(bShowGiveup == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UMD_QuestWidget::HandleQuestCardClicked(UMD_QuestCardWidget* QuestCard)
{
	SelectQuestCard(QuestCard);
}

void UMD_QuestWidget::HandleAcceptClicked()
{
	// 메인 퀘스트 또는 제시 상태가 아닌 반복 퀘스트의 수락 요청 차단
	if (IsValid(SelectedQuestCard) == false)
	{
		return;
	}
	bool bMainQuest = SelectedQuestCard->GetIsMainQuest();
	if (bMainQuest == true)
	{
		return;
	}
	FMD_RepeatQuestState QuestState = SelectedQuestCard->GetRepeatQuestState();
	if (QuestState.Status != EMD_RepeatQuestStatus::Offered)
	{
		return;
	}
	OnRepeatAcceptRequested.Broadcast(SelectedQuestCard->GetQuestIndex());
}

void UMD_QuestWidget::HandleRejectClicked()
{
	// 제시된 반복 퀘스트에서만 거절 요청 전달
	if (IsValid(SelectedQuestCard) == false)
	{
		return;
	}
	bool bMainQuest = SelectedQuestCard->GetIsMainQuest();
	if (bMainQuest == true)
	{
		return;
	}
	FMD_RepeatQuestState QuestState = SelectedQuestCard->GetRepeatQuestState();
	if (QuestState.Status != EMD_RepeatQuestStatus::Offered)
	{
		return;
	}
	OnRepeatDeclineRequested.Broadcast(SelectedQuestCard->GetQuestIndex());
}

void UMD_QuestWidget::HandleRewardClicked()
{
	// 수락 상태의 반복 퀘스트만 기존 서버 정산 요청 경로로 전달
	if (IsValid(SelectedQuestCard) == false)
	{
		return;
	}
	bool bMainQuest = SelectedQuestCard->GetIsMainQuest();
	if (bMainQuest == true)
	{
		return;
	}
	FMD_RepeatQuestState QuestState = SelectedQuestCard->GetRepeatQuestState();
	if (QuestState.Status != EMD_RepeatQuestStatus::Accepted)
	{
		return;
	}
	OnRepeatAcceptRequested.Broadcast(SelectedQuestCard->GetQuestIndex());
}

void UMD_QuestWidget::HandleGiveupClicked()
{
	// 진행 중인 반복 퀘스트에서만 포기 요청 전달
	if (IsValid(SelectedQuestCard) == false)
	{
		return;
	}
	bool bMainQuest = SelectedQuestCard->GetIsMainQuest();
	if (bMainQuest == true)
	{
		return;
	}
	FMD_RepeatQuestState QuestState = SelectedQuestCard->GetRepeatQuestState();
	if (QuestState.Status != EMD_RepeatQuestStatus::Accepted)
	{
		return;
	}
	OnRepeatAbandonRequested.Broadcast(SelectedQuestCard->GetQuestIndex());
}
