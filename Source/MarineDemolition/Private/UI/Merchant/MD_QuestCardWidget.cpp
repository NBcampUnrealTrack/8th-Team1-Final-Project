#include "UI/Merchant/MD_QuestCardWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/Merchant/MD_QuestDefinition.h"

void UMD_QuestCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 공통 버튼의 선택 상태를 퀘스트 목록에서 사용할 수 있도록 활성화
	SetIsSelectable(true);
	// 에디터 기본값 또는 외부에서 먼저 적용한 데이터를 실제 자식 위젯에 반영
	RefreshDisplay();
}

void UMD_QuestCardWidget::SetMainQuest(int32 InQuestIndex, const FMD_MainQuestState& InQuestState)
{
	// 카드 선택을 메인 퀘스트 원본 배열과 연결하기 위한 데이터 저장
	QuestIndex = InQuestIndex;
	bMainQuest = true;
	MainQuestState = InQuestState;
	// 이전 반복 퀘스트 데이터가 잘못 조회되지 않도록 초기화
	RepeatQuestState = FMD_RepeatQuestState();
	RefreshDisplay();
}

void UMD_QuestCardWidget::SetRepeatQuest(int32 InQuestIndex, const FMD_RepeatQuestState& InQuestState)
{
	// 카드 선택을 반복 퀘스트 원본 배열과 연결하기 위한 데이터 저장
	QuestIndex = InQuestIndex;
	bMainQuest = false;
	RepeatQuestState = InQuestState;
	// 이전 메인 퀘스트 데이터가 잘못 조회되지 않도록 초기화
	MainQuestState = FMD_MainQuestState();
	RefreshDisplay();
}

void UMD_QuestCardWidget::RefreshDisplay()
{
	// 카드 종류에 따라 원본 정의와 현재 진행 정보를 선택
	FPrimaryAssetId DefinitionId = bMainQuest == true ? MainQuestState.DefinitionId : RepeatQuestState.DefinitionId;
	int32 PhaseIndex = bMainQuest == true ? MainQuestState.CurrentPhaseIndex : RepeatQuestState.CurrentPhaseIndex;
	int32 CurrentProgress = bMainQuest == true ? MainQuestState.CurrentProgress : RepeatQuestState.CurrentProgress;
	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(DefinitionId);
	bool bHasDefinition = IsValid(Definition);
	if (bHasDefinition == true)
	{
		bHasDefinition = Definition->HasPhase(PhaseIndex);
	}

	// 카드 상단에 메인과 반복 종류를 구분하여 표시
	if (IsValid(Txt_QuestType) == true)
	{
		FText QuestTypeText = bMainQuest == true ? FText::FromString(TEXT("메인")) : FText::FromString(TEXT("반복"));
		Txt_QuestType->SetText(QuestTypeText);
	}

	// 유효한 정의에서 카드 제목을 가져오고 누락된 정의는 빈 값으로 처리
	if (IsValid(Txt_QuestName) == true)
	{
		FText QuestName = bHasDefinition == true ? Definition->QuestName : FText::GetEmpty();
		Txt_QuestName->SetText(QuestName);
	}

	// 현재 페이즈 목표 내용을 카드 본문에 적용
	if (IsValid(Txt_QuestDescription) == true)
	{
		FText QuestDescription = bHasDefinition == true ? Definition->GetHUDDescription(PhaseIndex) : FText::GetEmpty();
		Txt_QuestDescription->SetText(QuestDescription);
	}

	// 제출 수량과 요구 수량을 동일한 형식으로 표시
	if (IsValid(Txt_QuestProgress) == true)
	{
		int32 RequiredCount = bHasDefinition == true ? Definition->GetRequiredCount(PhaseIndex) : 0;
		FText QuestProgress = FText::FromString(FString::Printf(TEXT("%d / %d"), CurrentProgress, RequiredCount));
		Txt_QuestProgress->SetText(QuestProgress);
	}

	// 디자이너가 유효한 종류별 아이콘을 지정한 경우에만 기존 이미지 교체
	if (IsValid(Img_QuestType) == true)
	{
		FSlateBrush QuestIcon = bMainQuest == true ? MainQuestIcon : RepeatQuestIcon;
		UObject* QuestIconResource = QuestIcon.GetResourceObject();
		if (IsValid(QuestIconResource) == true)
		{
			Img_QuestType->SetBrush(QuestIcon);
		}
	}
}
