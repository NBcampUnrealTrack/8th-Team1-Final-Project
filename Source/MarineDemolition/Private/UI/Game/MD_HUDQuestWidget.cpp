#include "UI/Game/MD_HUDQuestWidget.h"

#include "Component/Merchant/MD_MerchantComponent.h"
#include "Components/ScrollBox.h"
#include "Engine/World.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "UI/Game/MD_HUDQuestCard.h"

void UMD_HUDQuestWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 최초 표시 시 이미 생성된 GameState의 퀘스트 상태를 즉시 반영하기 위한 연결 시도
	TryBindMerchantComponent();

	UWorld* World = GetWorld();
	if (IsValid(World) == true)
	{
		// Seamless Travel 이후 교체된 GameState에 위젯을 다시 연결하기 위한 주기적 확인
		World->GetTimerManager().SetTimer(MerchantBindingTimerHandle, this, &UMD_HUDQuestWidget::TryBindMerchantComponent, 0.25f, true);
	}
}

void UMD_HUDQuestWidget::NativeDestruct()
{
	// 제거된 위젯으로 상태 변경 이벤트가 전달되지 않도록 기존 구독 해제
	UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
	if (IsValid(MerchantComponent) == true)
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &UMD_HUDQuestWidget::RefreshQuestDisplay);
	}
	BoundMerchantComponent.Reset();

	UWorld* World = GetWorld();
	if (IsValid(World) == true)
	{
		// 더 이상 필요하지 않은 GameState 재연결 확인 중단
		World->GetTimerManager().ClearTimer(MerchantBindingTimerHandle);
	}

	Super::NativeDestruct();
}

void UMD_HUDQuestWidget::TryBindMerchantComponent()
{
	UWorld* World = GetWorld();
	AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	UMD_MerchantComponent* MerchantComponent = IsValid(GameState) == true ? GameState->GetMerchantComponent() : nullptr;

	// 같은 컴포넌트에 중복 구독하지 않기 위한 현재 연결 비교
	if (BoundMerchantComponent.Get() == MerchantComponent)
	{
		return;
	}

	// 맵 전환 전 GameState에 남아 있는 이벤트 구독 정리
	UMD_MerchantComponent* PreviousMerchantComponent = BoundMerchantComponent.Get();
	if (IsValid(PreviousMerchantComponent) == true)
	{
		PreviousMerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &UMD_HUDQuestWidget::RefreshQuestDisplay);
	}

	BoundMerchantComponent = MerchantComponent;
	if (IsValid(MerchantComponent) == true)
	{
		// 서버 미러가 변경될 때 퀘스트 진행도와 수락 상태를 즉시 표시하기 위한 구독
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &UMD_HUDQuestWidget::RefreshQuestDisplay);
	}

	RefreshQuestDisplay();
}

void UMD_HUDQuestWidget::AddMainQuestCard(const FMD_MainQuestState& QuestState)
{
	if (IsValid(SB_QuestList) == false || QuestCardClass.Get() == nullptr)
	{
		return;
	}

	// 메인 퀘스트 상태 하나를 독립된 카드로 표시하기 위한 위젯 생성
	UMD_HUDQuestCard* QuestCard = CreateWidget<UMD_HUDQuestCard>(this, QuestCardClass);
	if (IsValid(QuestCard) == false)
	{
		return;
	}

	QuestCard->SetMainQuest(QuestState);
	SB_QuestList->AddChild(QuestCard);
}

void UMD_HUDQuestWidget::AddRepeatQuestCard(const FMD_RepeatQuestState& QuestState)
{
	if (IsValid(SB_QuestList) == false || QuestCardClass.Get() == nullptr)
	{
		return;
	}

	// 수락한 반복 퀘스트 상태 하나를 독립된 카드로 표시하기 위한 위젯 생성
	UMD_HUDQuestCard* QuestCard = CreateWidget<UMD_HUDQuestCard>(this, QuestCardClass);
	if (IsValid(QuestCard) == false)
	{
		return;
	}

	QuestCard->SetRepeatQuest(QuestState);
	SB_QuestList->AddChild(QuestCard);
}

void UMD_HUDQuestWidget::RefreshQuestDisplay()
{
	if (IsValid(SB_QuestList) == false || QuestCardClass.Get() == nullptr)
	{
		return;
	}

	// 상태가 사라진 퀘스트 카드가 남지 않도록 현재 복제 상태 기준으로 목록 초기화
	SB_QuestList->ClearChildren();

	UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
	if (IsValid(MerchantComponent) == false)
	{
		return;
	}

	// 현재 일차에 해금된 메인 퀘스트 하나를 최상단에 표시
	TArray<FMD_MainQuestState> MainQuests = MerchantComponent->GetMainQuests();
	for (FMD_MainQuestState& MainQuest : MainQuests)
	{
		if (MainQuest.UnlockDay == MerchantComponent->GetCurrentDay() && MainQuest.Status != EMD_MainQuestStatus::Locked && MainQuest.Definition.IsValidDefinition() == true)
		{
			AddMainQuestCard(MainQuest);
			break;
		}
	}

	// 사용자가 명시적으로 수락했고 아직 진행 중인 반복 퀘스트 전체 표시
	TArray<FMD_RepeatQuestState> RepeatQuests = MerchantComponent->GetRepeatQuests();
	for (FMD_RepeatQuestState& RepeatQuest : RepeatQuests)
	{
		if (RepeatQuest.Status == EMD_RepeatQuestStatus::Accepted && RepeatQuest.Definition.IsValidDefinition() == true)
		{
			AddRepeatQuestCard(RepeatQuest);
		}
	}

	int32 CurrentQuestCount = SB_QuestList->GetChildrenCount();
	SetVisibility(CurrentQuestCount <= 0 ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}
