#include "UI/Game/MD_HUDQuestWidget.h"

#include "Component/Merchant/MD_MerchantComponent.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"
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

bool UMD_HUDQuestWidget::AddMainQuestCard(const FMD_MainQuestState& QuestState)
{
	if (IsValid(SB_QuestList_Main) == false || QuestCardClass.Get() == nullptr)
	{
		return false;
	}

	// 메인 퀘스트 상태 하나를 독립된 카드로 표시하기 위한 위젯 생성
	UMD_HUDQuestCard* QuestCard = CreateWidget<UMD_HUDQuestCard>(this, QuestCardClass);
	if (IsValid(QuestCard) == false)
	{
		return false;
	}

	// 메인 퀘스트 표시 정보를 새 카드에 적용한 결과를 저장한다.
	bool bSetResult = QuestCard->SetMainQuest(QuestState);
	// 원본이 없거나 현재 페이즈가 잘못된 카드는 목록에 추가하지 않는다.
	if (bSetResult == false)
	{
		return false;
	}

	// 메인 퀘스트 전용 목록에 카드를 추가한다.
	SB_QuestList_Main->AddChild(QuestCard);
	// 메인 퀘스트 카드가 목록에 추가되었음을 반환한다.
	return true;
}

void UMD_HUDQuestWidget::AddRepeatQuestCard(const FMD_RepeatQuestState& QuestState)
{
	if (IsValid(SB_QuestList_Sub) == false || QuestCardClass.Get() == nullptr)
	{
		return;
	}

	// 수락한 반복 퀘스트 상태 하나를 독립된 카드로 표시하기 위한 위젯 생성
	UMD_HUDQuestCard* QuestCard = CreateWidget<UMD_HUDQuestCard>(this, QuestCardClass);
	if (IsValid(QuestCard) == false)
	{
		return;
	}

	// 반복 퀘스트 표시 정보를 새 카드에 적용한 결과를 저장한다.
	bool bSetResult = QuestCard->SetRepeatQuest(QuestState);
	// 원본이 없거나 현재 페이즈가 잘못된 카드는 목록에 추가하지 않는다.
	if (bSetResult == false)
	{
		return;
	}

	// 반복 퀘스트 전용 목록에 카드를 추가한다.
	SB_QuestList_Sub->AddChild(QuestCard);
}

void UMD_HUDQuestWidget::UpdateQuestSectionVisibility()
{
	// 메인 퀘스트 카드 존재 여부를 한 번만 확인한다.
	bool bHasMainQuest = SB_QuestList_Main->GetChildrenCount() > 0;
	// 반복 퀘스트 카드 존재 여부를 한 번만 확인한다.
	bool bHasRepeatQuest = SB_QuestList_Sub->GetChildrenCount() > 0;

	// 메인 퀘스트가 없으면 해당 제목과 목록 섹션을 함께 숨긴다.
	if (IsValid(VB_QuestContents_Main) == true)
	{
		VB_QuestContents_Main->SetVisibility(bHasMainQuest == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 반복 퀘스트가 없으면 해당 제목과 목록 섹션을 함께 숨긴다.
	if (IsValid(VB_QuestContents_Sub) == true)
	{
		VB_QuestContents_Sub->SetVisibility(bHasRepeatQuest == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 표시할 퀘스트가 하나도 없으면 HUD 퀘스트 패널 전체를 숨긴다.
	SetVisibility(bHasMainQuest == true || bHasRepeatQuest == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMD_HUDQuestWidget::RefreshQuestDisplay()
{
	if (IsValid(SB_QuestList_Main) == false || IsValid(SB_QuestList_Sub) == false || QuestCardClass.Get() == nullptr)
	{
		return;
	}

	// 상태가 사라진 메인 퀘스트 카드가 남지 않도록 목록을 초기화한다.
	SB_QuestList_Main->ClearChildren();
	// 상태가 사라진 반복 퀘스트 카드가 남지 않도록 목록을 초기화한다.
	SB_QuestList_Sub->ClearChildren();

	UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
	if (IsValid(MerchantComponent) == false)
	{
		// 빈 목록을 기준으로 각 섹션과 전체 패널을 숨긴다.
		UpdateQuestSectionVisibility();
		return;
	}

	// 현재 일차에 해금된 메인 퀘스트 하나를 최상단에 표시
	TArray<FMD_MainQuestState> MainQuests = MerchantComponent->GetMainQuests();
	for (FMD_MainQuestState& MainQuest : MainQuests)
	{
		// 완료 보상은 서버에서 이미 지급됐다. 완료 상태는 원장에 보존하되 HUD에서는 제거한다.
		if (MainQuest.UnlockDay != MerchantComponent->GetCurrentDay()
			|| MainQuest.Status == EMD_MainQuestStatus::Locked
			|| MainQuest.Status == EMD_MainQuestStatus::Completed)
		{
			continue;
		}

		// 원본을 조회할 수 있는 메인 퀘스트만 카드 생성을 시도한다.
		if (MainQuest.HasValidDefinitionId() == true)
		{
			// 유효한 원본과 페이즈로 카드가 추가되었는지 확인한다.
			bool bAddResult = AddMainQuestCard(MainQuest);
			if (bAddResult == true)
			{
				break;
			}
		}
	}

	// 수락 후 진행 중이거나 목표를 달성하고 보상 수령을 기다리는 반복 퀘스트 전체 표시
	TArray<FMD_RepeatQuestState> RepeatQuests = MerchantComponent->GetRepeatQuests();
	for (FMD_RepeatQuestState& RepeatQuest : RepeatQuests)
	{
		if (RepeatQuest.Status == EMD_RepeatQuestStatus::Accepted && RepeatQuest.HasValidDefinitionId() == true)
		{
			AddRepeatQuestCard(RepeatQuest);
		}
	}

	// 새로 구성한 목록을 기준으로 각 섹션과 전체 패널을 갱신한다.
	UpdateQuestSectionVisibility();
}
