// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_QuestWidget.h"

#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
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

void UMD_QuestWidget::RefreshDisplay()
{
	// 목록 재구성과 선택 상세 갱신을 동일 데이터 시점에 수행
	RebuildQuestList();
}


void UMD_QuestWidget::SelectQuestCard(UMD_QuestCardWidget* QuestCard)
{
	// 현재 목록에 속하지 않은 카드가 상세 화면을 변경하지 못하도록 검증
	SelectedQuestCard = nullptr;
	if (IsValid(QuestCard) == true)
	{
		bool bContainsQuestCard = QuestCards.Contains(QuestCard);
		if (bContainsQuestCard == true)
		{
			SelectedQuestCard = QuestCard;
		}
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
	// 스크롤과 구분 제목을 유지하면서 메인 목록의 카드만 교체
	if (IsValid(VB_MainQuest) == true)
	{
		// 메인 영역의 목업 또는 이전 카드 제거
		VB_MainQuest->ClearChildren();
	}
	// 반복 목록의 카드만 교체
	if (IsValid(VB_RepeatQuest) == true)
	{
		// 반복 영역의 목업 또는 이전 카드 제거
		VB_RepeatQuest->ClearChildren();
	}
	QuestCards.Empty();
	SelectedQuestCard = nullptr;

	// 카드 클래스가 지정되지 않은 화면에서는 빈 목록과 상세 상태 유지
	if (IsValid(QuestCardClass.Get()) == false)
	{
		RefreshQuestDetail();
		return;
	}

	// 메인과 반복을 기존 배열 순서대로 각 전용 영역에 생성
	for (int32 CardIndex = 0; CardIndex < MainQuests.Num() + RepeatQuests.Num(); ++CardIndex)
	{
		// 공통 생성 경로에서 적용할 퀘스트 종류 구분
		bool bMainQuest = CardIndex < MainQuests.Num();
		// 현재 종류에 대응하는 디자이너 목록 영역 선택
		UVerticalBox* QuestList = bMainQuest == true ? VB_MainQuest.Get() : VB_RepeatQuest.Get();
		// 선택 바인딩이 없는 종류는 카드 생성 생략
		if (IsValid(QuestList) == false)
		{
			// 다음 카드로 이동
			continue;
		}
		// 서버 요청에 사용할 종류별 원본 배열 인덱스 계산
		int32 QuestIndex = bMainQuest == true ? CardIndex : CardIndex - MainQuests.Num();
		// 현재 구성에 사용할 카드 인스턴스 생성
		UMD_QuestCardWidget* QuestCard = CreateWidget<UMD_QuestCardWidget>(this, QuestCardClass);
		// 생성에 실패한 카드는 선택 그룹과 목록에서 제외
		if (IsValid(QuestCard) == false)
		{
			// 다음 카드로 이동
			continue;
		}
		// 선택한 종류에 맞는 원본 상태 적용
		if (bMainQuest == true)
		{
			// 메인 퀘스트 상태와 원본 인덱스 전달
			QuestCard->SetMainQuest(QuestIndex, MainQuests[QuestIndex]);
		}
		else
		{
			// 반복 퀘스트 상태와 원본 인덱스 전달
			QuestCard->SetRepeatQuest(QuestIndex, RepeatQuests[QuestIndex]);
		}
		// 두 영역의 카드를 하나의 선택 그룹으로 관리
		if (IsValid(QuestCardButtonGroup) == true)
		{
			// 새 카드의 단일 선택 등록
			QuestCardButtonGroup->AddWidget(QuestCard);
		}
		// 중간 전달 함수 없이 카드 선택 처리에 클릭 연결
		QuestCard->OnClicked().AddUObject(this, &UMD_QuestWidget::SelectQuestCard, QuestCard);
		// 현재 종류의 목록에 카드를 배치하고 슬롯 확보
		UVerticalBoxSlot* CardSlot = QuestList->AddChildToVerticalBox(QuestCard);
		// 실제 에셋의 카드 간격을 동적 카드에도 적용
		if (IsValid(CardSlot) == true)
		{
			// 목업과 동일한 위아래 여백 적용
			CardSlot->SetPadding(FMargin(0.0f, 4.0f));
		}
		// 선택 복원과 구독 해제에 사용할 카드 보관
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
	// 선택 또는 원본이 없는 경우 사용할 상태 문구 준비
	FText StateText = FText::GetEmpty();
	// 빈 목록의 상세 이름 안내 준비
	FText QuestNameText = NSLOCTEXT("MDQuestTab", "NoSelection", "선택된 퀘스트 없음");
	// 현재 페이즈가 없는 경우 사용할 목표 분류 기본값 준비
	FText ObjectiveText = FText::GetEmpty();
	// 데이터가 없는 진행 수량과 보상에 공통으로 사용할 표시값 준비
	FText EmptyValue = FText::FromString(TEXT("-"));
	// 현재 진행 수량의 기본 표시 준비
	FText CurrentProgressText = EmptyValue;
	// 목표 수량의 기본 표시 준비
	FText RequiredCountText = EmptyValue;
	// 선택 전 상세 설명 안내 준비
	FText DescriptionText = NSLOCTEXT("MDQuestTab", "SelectQuest", "퀘스트를 선택하면 상세 정보가 표시됩니다.");
	// 빈 상세 호감도 보상 표시 준비
	FText AffinityText = EmptyValue;
	// 빈 상세 점수 보상 표시 준비
	FText ScoreText = EmptyValue;
	// 빈 상세 크레딧 보상 표시 준비
	FText CreditText = EmptyValue;
	// 제시 상태의 수락 및 거절 버튼 표시 여부 준비
	bool bShowOfferActions = false;
	// 마지막 목표 달성 후 보상 버튼 표시 여부 준비
	bool bShowReward = false;
	// 목표 달성 전 포기 버튼 표시 여부 준비
	bool bShowGiveup = false;
	// 현재 페이즈의 체크 표시 여부 준비
	bool bPhaseComplete = false;

	// 선택 카드의 원본을 조회하기 위한 준비
	UMD_QuestDefinition* Definition = nullptr;
	// 상세 목표에 사용할 페이즈 기본값 준비
	int32 PhaseIndex = 0;
	// 상세 수량에 사용할 진행도 기본값 준비
	int32 CurrentProgress = 0;
	// 반복 액션을 메인 퀘스트에 노출하지 않기 위한 기본 상태 준비
	EMD_RepeatQuestStatus RepeatStatus = EMD_RepeatQuestStatus::Declined;
	// 유효한 선택 카드에서만 원본과 진행 상태 조회
	if (IsValid(SelectedQuestCard) == true)
	{
		// 카드 종류에 대응하는 원본 데이터 선택
		if (SelectedQuestCard->GetIsMainQuest() == true)
		{
			// 원본과 현재 페이즈를 함께 조회할 메인 상태 복사
			FMD_MainQuestState QuestState = SelectedQuestCard->GetMainQuestState();
			// 메인 퀘스트 원본 로드
			Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
			// 상세 페이즈 선택
			PhaseIndex = QuestState.CurrentPhaseIndex;
			// 상세 진행 수량 선택
			CurrentProgress = QuestState.CurrentProgress;
		}
		else
		{
			// 원본과 액션 상태를 함께 조회할 반복 상태 복사
			FMD_RepeatQuestState QuestState = SelectedQuestCard->GetRepeatQuestState();
			// 반복 퀘스트 원본 로드
			Definition = UMD_QuestDefinition::LoadDefinition(QuestState.DefinitionId);
			// 상세 페이즈 선택
			PhaseIndex = QuestState.CurrentPhaseIndex;
			// 상세 진행 수량 선택
			CurrentProgress = QuestState.CurrentProgress;
			// 반복 액션 노출에 사용할 상태 보관
			RepeatStatus = QuestState.Status;
		}
	}

	// 유효한 원본에서 이름과 상태 및 보상 표시 계산
	if (IsValid(Definition) == true)
	{
		// 선택 카드와 동일한 상태 표시 규칙 적용
		StateText = SelectedQuestCard->MakeQuestStateText(Definition);
		// 현재 페이즈 유무와 관계없이 퀘스트 이름 표시
		QuestNameText = Definition->QuestName;
		// 없는 페이즈의 설명에 선택 안내가 남지 않도록 초기화
		DescriptionText = FText::GetEmpty();
		// 성공 보상 호감도 표시 계산
		AffinityText = FText::FromString(FString::Printf(TEXT("%+.1f"), Definition->SuccessReward.Affinity));
		// 성공 보상 점수 표시 계산
		ScoreText = FText::AsNumber(Definition->SuccessReward.Score);
		// 성공 보상 크레딧 표시 계산
		CreditText = FText::AsNumber(Definition->SuccessReward.Credits);
		// 현재 목표가 존재할 때만 진행도와 액션 노출 계산
		if (Definition->HasPhase(PhaseIndex) == true)
		{
			// 목표 수량 표시와 체크 판정에서 공유할 요구 수량 조회
			int32 RequiredCount = Definition->GetRequiredCount(PhaseIndex);
			// 분리된 현재 수량 텍스트 구성
			CurrentProgressText = FText::AsNumber(CurrentProgress);
			// 분리된 목표 수량 텍스트 구성
			RequiredCountText = FText::AsNumber(RequiredCount);
			// 실제 현재 페이즈의 목표 설명 표시
			DescriptionText = Definition->GetHUDDescription(PhaseIndex);
			// 현재 페이즈를 달성한 경우 체크 표시 활성화
			bPhaseComplete = CurrentProgress >= FMath::Max(RequiredCount, 1);
			// 제시된 반복 퀘스트에서만 수락과 거절 노출
			bShowOfferActions = RepeatStatus == EMD_RepeatQuestStatus::Offered;
			// 수락한 반복 퀘스트에서 목표 달성에 따른 액션 구분
			if (RepeatStatus == EMD_RepeatQuestStatus::Accepted)
			{
				// 마지막 목표 달성 시 보상 수령 노출
				bShowReward = Definition->IsObjectiveComplete(PhaseIndex, CurrentProgress);
				// 보상 대기 상태에서는 포기 액션 제외
				bShowGiveup = bShowReward == false;
			}
			// 기존 수량 합성 텍스트 대신 실제 목표 종류에 맞는 제목 선택
			switch (Definition->GetObjectiveType(PhaseIndex))
			{
			case EMD_QuestObjectiveType::RecoverIntact:
				// 온전한 대상 회수 목표 표시
				ObjectiveText = NSLOCTEXT("MDQuestTab", "RecoverIntact", "회수");
				break;
			case EMD_QuestObjectiveType::RecoverDamaged:
				// 손상 여부를 허용하는 회수 목표 표시
				ObjectiveText = NSLOCTEXT("MDQuestTab", "RecoverDamaged", "손상 허용 회수");
				break;
			case EMD_QuestObjectiveType::Cleanup:
				// 청소 목표 표시
				ObjectiveText = NSLOCTEXT("MDQuestTab", "Cleanup", "청소 계약");
				break;
			case EMD_QuestObjectiveType::Sealing:
				// 마감 목표 표시
				ObjectiveText = NSLOCTEXT("MDQuestTab", "Sealing", "마감 계약");
				break;
			case EMD_QuestObjectiveType::Composite:
				// 복합 목표 표시
				ObjectiveText = NSLOCTEXT("MDQuestTab", "Composite", "복합");
				break;
			case EMD_QuestObjectiveType::Interaction:
				// 인물 상호작용 목표 표시
				ObjectiveText = NSLOCTEXT("MDQuestTab", "Interaction", "NPC 상호작용");
				break;
			}
		}
	}

	// 선택 바인딩이 존재하고 표시값이 달라진 텍스트만 갱신
	auto ApplyText = [](UTextBlock* TextBlock, FText Text)
	{
		// 없는 위젯이나 동일 문구의 반복 갱신 생략
		if (IsValid(TextBlock) == true && TextBlock->GetText().EqualTo(Text) == false)
		{
			// 새 상세 표시값 적용
			TextBlock->SetText(Text);
		}
	};
	// 선택 카드의 상태 문구 적용
	ApplyText(Txt_Detail_State, StateText);
	// 선택 퀘스트 이름 적용
	ApplyText(Txt_Detail_Name, QuestNameText);
	// 목표 종류 제목 적용
	ApplyText(Txt_DetailProgress, ObjectiveText);
	// 분리된 현재 진행 수량 적용
	ApplyText(Txt_DetailProgress_Current, CurrentProgressText);
	// 분리된 목표 수량 적용
	ApplyText(Txt_DetailProgress_Total, RequiredCountText);
	// 현재 목표 설명 적용
	ApplyText(Txt_Detail_Description, DescriptionText);
	// 호감도 보상 적용
	ApplyText(Txt_Reward_Affinity, AffinityText);
	// 점수 보상 적용
	ApplyText(Txt_Reward_Score, ScoreText);
	// 크레딧 보상 적용
	ApplyText(Txt_Reward_Credit, CreditText);
	// 선택 바인딩이 존재하는 체크 이미지 갱신
	if (IsValid(Img_DetailProgress) == true)
	{
		// 미달성 시 슬롯 크기를 유지하면서 체크 이미지만 숨김
		Img_DetailProgress->SetVisibility(bPhaseComplete == true ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	// 실제로 보상을 받을 수 있을 때만 완료 안내 노출
	if (IsValid(Txt_CompletionMessage) == true)
	{
		// 기존 디자이너 안내 문구를 보상 버튼 상태와 동기화
		Txt_CompletionMessage->SetVisibility(bShowReward == true ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	// 제시된 반복 퀘스트의 수락 액션 노출
	if (IsValid(Btn_Accept) == true)
	{
		// 수락 가능 상태에서만 버튼 표시
		Btn_Accept->SetVisibility(bShowOfferActions == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// 제시된 반복 퀘스트의 거절 액션 노출
	if (IsValid(Btn_Reject) == true)
	{
		// 제시 상태에서만 버튼 표시
		Btn_Reject->SetVisibility(bShowOfferActions == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// 마지막 목표를 달성한 반복 퀘스트의 보상 액션 노출
	if (IsValid(Btn_Reward) == true)
	{
		// 보상 대기 상태에서만 버튼 표시
		Btn_Reward->SetVisibility(bShowReward == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// 진행 중인 반복 퀘스트의 포기 액션 노출
	if (IsValid(Btn_Giveup) == true)
	{
		// 보상 대기 전 수행 상태에서만 버튼 표시
		Btn_Giveup->SetVisibility(bShowGiveup == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
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
