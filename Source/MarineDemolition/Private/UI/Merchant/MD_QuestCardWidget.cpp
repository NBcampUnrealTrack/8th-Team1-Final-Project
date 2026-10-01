#include "UI/Merchant/MD_QuestCardWidget.h"

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
	// 현재 카드에 해당하는 퀘스트 원본을 한 번 조회하여 표시 계산에 공유
	UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(bMainQuest == true ? MainQuestState.DefinitionId : RepeatQuestState.DefinitionId);
	// 원본 누락 시 이전 이름을 지우기 위한 기본 표시값 준비
	FText QuestNameText = FText::GetEmpty();
	// 페이즈 누락 시 이전 진행 수량을 지우기 위한 기본 표시값 준비
	FString ProgressString;
	// 표시 대상 상태가 없는 경우 이전 상태 문구를 지우기 위한 기본값 준비
	FText StateText = FText::GetEmpty();
	// 유효한 원본에서만 카드 표시 내용 계산
	if (IsValid(Definition) == true)
	{
		// 공통 버튼의 이름으로 사용할 원본 텍스트 선택
		QuestNameText = Definition->QuestName;
		// 진행 수량 표시와 보상 가능 여부 판정에 사용할 현재 페이즈 선택
		int32 PhaseIndex = bMainQuest == true ? MainQuestState.CurrentPhaseIndex : RepeatQuestState.CurrentPhaseIndex;
		// 진행 수량 표시와 보상 가능 여부 판정에 사용할 누적 제출 수량 선택
		int32 CurrentProgress = bMainQuest == true ? MainQuestState.CurrentProgress : RepeatQuestState.CurrentProgress;
		// 진행 위젯과 현재 페이즈가 모두 있는 경우에만 수량 문자열 구성
		if (IsValid(Txt_Progress) == true && Definition->HasPhase(PhaseIndex) == true)
		{
			// 변경되지 않은 값의 FText 재생성을 피하기 위한 비교용 문자열 구성
			ProgressString = FString::Printf(TEXT("%d / %d"), CurrentProgress, Definition->GetRequiredCount(PhaseIndex));
		}
		// 상태 위젯이 있는 구성에서만 문구와 목표 달성 여부 계산
		if (IsValid(Txt_State) == true)
		{
			// 카드와 상세 패널이 공유하는 상태 문구 계산
			StateText = MakeQuestStateText(Definition);
		}
	}

	// 저장된 이름이나 실제 표시가 달라졌을 때만 부모 공통 버튼 갱신
	if (GetContentText().IdenticalTo(QuestNameText) == false
		|| (IsValid(Txt_ButtonText) == true && Txt_ButtonText->GetText().IdenticalTo(QuestNameText) == false))
	{
		// 재구성에 사용할 이름과 현재 TextBlock 표시 동기화
		SetContentText(QuestNameText);
	}
	// 실제 진행 수량이 달라졌을 때만 텍스트 생성과 위젯 갱신 수행
	if (IsValid(Txt_Progress) == true && Txt_Progress->GetText().ToString() != ProgressString)
	{
		// 계산된 진행 수량 또는 빈 값 적용
		Txt_Progress->SetText(FText::FromString(ProgressString));
	}
	// 상태 문구의 지역화 식별자가 달라졌을 때만 위젯 갱신 수행
	if (IsValid(Txt_State) == true && Txt_State->GetText().IdenticalTo(StateText) == false)
	{
		// 계산된 상태 문구 또는 빈 값 적용
		Txt_State->SetText(StateText);
	}

	// 네이티브 표시 갱신 후 기존 Blueprint의 종류별 연출 실행
	SetQuestType(bMainQuest);
}

FText UMD_QuestCardWidget::MakeQuestStateText(UMD_QuestDefinition* Definition)
{
	// 원본이 없는 카드의 상태 문구 생략
	if (IsValid(Definition) == false)
	{
		// 이전 퀘스트 상태가 남지 않도록 빈 문구 반환
		return FText::GetEmpty();
	}
	// 메인과 반복의 완료 상태를 동일한 문구로 표시
	if (bMainQuest == true ? MainQuestState.Status == EMD_MainQuestStatus::Completed : RepeatQuestState.Status == EMD_RepeatQuestStatus::Completed)
	{
		// 보상 정산이 끝난 상태 문구 반환
		return NSLOCTEXT("MDQuestCard", "Completed", "완료됨");
	}
	// 메인과 반복의 실패 상태를 동일한 문구로 표시
	if (bMainQuest == true ? MainQuestState.Status == EMD_MainQuestStatus::Failed : RepeatQuestState.Status == EMD_RepeatQuestStatus::Failed)
	{
		// 목표를 달성하지 못하고 종료된 상태 문구 반환
		return NSLOCTEXT("MDQuestCard", "Failed", "실패");
	}
	// 수행 중인 상태에서만 마지막 목표 달성 여부 판정
	if (bMainQuest == true ? MainQuestState.Status == EMD_MainQuestStatus::Active : RepeatQuestState.Status == EMD_RepeatQuestStatus::Accepted)
	{
		// 원본을 다시 로드하지 않고 현재 카드의 진행 데이터로 보상 가능 여부 확인
		if (Definition->IsObjectiveComplete(bMainQuest == true ? MainQuestState.CurrentPhaseIndex : RepeatQuestState.CurrentPhaseIndex,
			bMainQuest == true ? MainQuestState.CurrentProgress : RepeatQuestState.CurrentProgress) == true)
		{
			// 마지막 목표 달성 후 보상 대기 상태 문구 반환
			return NSLOCTEXT("MDQuestCard", "RewardAvailable", "보상 가능");
		}
		// 아직 마지막 목표를 달성하지 않은 수행 상태 문구 반환
		return NSLOCTEXT("MDQuestCard", "InProgress", "진행 중");
	}
	// 수락과 거절을 선택할 수 있는 반복 퀘스트 제안 구분
	if (bMainQuest == false && RepeatQuestState.Status == EMD_RepeatQuestStatus::Offered)
	{
		// 제안 상태의 수락 가능 문구 반환
		return NSLOCTEXT("MDQuestCard", "Available", "수락 가능");
	}
	// 잠김과 거절 또는 알 수 없는 상태의 문구 생략
	return FText::GetEmpty();
}
