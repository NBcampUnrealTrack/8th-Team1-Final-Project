// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_QuestWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "GameFramework/Pawn.h"
#include "UI/Merchant/MD_MainQuestRowWidget.h"
#include "UI/Merchant/MD_QuestCardWidget.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"

void UMD_QuestWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
		}
	}

	if (IsValid(BTN_TabMain))
	{
		BTN_TabMain->OnClicked.AddDynamic(this, &UMD_QuestWidget::HandleTabMainClicked);
	}
	if (IsValid(BTN_TabRepeat))
	{
		BTN_TabRepeat->OnClicked.AddDynamic(this, &UMD_QuestWidget::HandleTabRepeatClicked);
	}
	if (IsValid(BTN_TabLore))
	{
		BTN_TabLore->OnClicked.AddDynamic(this, &UMD_QuestWidget::HandleTabLoreClicked);
	}
	if (IsValid(BTN_Back))
	{
		BTN_Back->OnClicked.AddDynamic(this, &UMD_QuestWidget::HandleBackClicked);
	}

	// 퀘스트 화면이 열릴 때 메뉴 패널을 숨긴다 (상점과 동일 원칙).
	if (IsValid(OwningMerchantMenu))
	{
		OwningMerchantMenu->SetMenuPanelVisible(false);
	}

	RefreshDisplay();
}

void UMD_QuestWidget::NativeDestruct()
{
	if (IsValid(OwningMerchantMenu))
	{
		OwningMerchantMenu->SetMenuPanelVisible(true);
	}
	// 상인 메뉴가 남아 있으면 입력·커서 복구는 메뉴 쪽 NativeDestruct가 처리한다.
	// 메뉴 없이 단독으로 띄운 경우에만 여기서 되돌린다 (상점 위젯과 동일 원칙).
	else if (APlayerController* PC = GetOwningPlayer())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->EnableInput(PC);
		}
	}

	Super::NativeDestruct();
}

void UMD_QuestWidget::SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests)
{
	MainQuests = InMainQuests;
	RefreshDisplay();
}

void UMD_QuestWidget::SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests)
{
	RepeatQuests = InRepeatQuests;
	RefreshDisplay();
}

void UMD_QuestWidget::SetLoreProgress(const TArray<FMD_LoreSiteProgress>& InSiteProgressList)
{
	LoreSiteProgressList = InSiteProgressList;
	RefreshLoreDisplay();
}

void UMD_QuestWidget::SelectTab(EMD_QuestTab InTab)
{
	SelectedTab = InTab;
	RefreshDisplay();
}

void UMD_QuestWidget::RefreshDisplay()
{
	if (IsValid(TXT_NoInspectionImpactNotice))
	{
		// 세 탭 다 점검 통과와 무관한 보너스다 (`04` §5.5 공통 주의, `09` §0 원칙1).
		TXT_NoInspectionImpactNotice->SetText(FText::FromString(TEXT("점검엔 영향 없음 — 전부 보너스")));
	}

	RebuildMainQuestList();
	RebuildRepeatQuestList();
	ApplyTabVisibility();
}

void UMD_QuestWidget::ApplyTabVisibility()
{
	if (IsValid(PNL_MainQuestList))
	{
		PNL_MainQuestList->SetVisibility(
			SelectedTab == EMD_QuestTab::Main ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(PNL_RepeatQuestList))
	{
		PNL_RepeatQuestList->SetVisibility(
			SelectedTab == EMD_QuestTab::Repeat ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_RepeatDeliveryNotice))
	{
		TXT_RepeatDeliveryNotice->SetVisibility(
			SelectedTab == EMD_QuestTab::Repeat ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(PNL_LoreTab))
	{
		PNL_LoreTab->SetVisibility(
			SelectedTab == EMD_QuestTab::Lore ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (IsValid(TXT_MainEmptyNotice))
	{
		TXT_MainEmptyNotice->SetVisibility(
			SelectedTab == EMD_QuestTab::Main && MainQuests.Num() == 0
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_RepeatEmptyNotice))
	{
		TXT_RepeatEmptyNotice->SetVisibility(
			SelectedTab == EMD_QuestTab::Repeat && RepeatQuests.Num() == 0
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_LoreEmptyNotice))
	{
		TXT_LoreEmptyNotice->SetVisibility(SelectedTab == EMD_QuestTab::Lore ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		//const bool bShowLoreEmpty = SelectedTab == EMD_QuestTab::Lore;
		//TXT_LoreEmptyNotice->SetVisibility(bShowLoreEmpty ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		//if (bShowLoreEmpty)
		//{
		//	TXT_LoreEmptyNotice->SetText(FText::FromString(TEXT("코덱스 데이터가 아직 연결되지 않았다.")));
		//}
	}

	// 패널 타이틀은 탭 공통으로 고정하지 않는다 — 로어 탭은 "퀘스트"가 아니라
	// "수집물 목록"에 가깝게 표기한다 (`04` §5.5 공통 주의).
	if (IsValid(TXT_PanelTitle))
	{
		switch (SelectedTab)
		{
		case EMD_QuestTab::Main:
			TXT_PanelTitle->SetText(FText::FromString(TEXT("메인 퀘스트")));
			break;
		case EMD_QuestTab::Repeat:
			TXT_PanelTitle->SetText(FText::FromString(TEXT("반복 퀘스트")));
			break;
		case EMD_QuestTab::Lore:
			TXT_PanelTitle->SetText(FText::FromString(TEXT("기록 보관함 (코덱스)")));
			break;
		}
	}
}

void UMD_QuestWidget::RefreshLoreDisplay()
{
	if (IsValid(TXT_LoreEmptyNotice) == false)
	{
		return;
	}

	// 서로 다른 현장의 영구 수집 기록을 코덱스 전체 발견량으로 합산하는 누적값
	int32 DiscoveredLoreCount = 0;
	for (const FMD_LoreSiteProgress& SiteProgress : LoreSiteProgressList)
	{
		DiscoveredLoreCount += SiteProgress.DiscoveredLoreIds.Num();
	}

	// 현재 연결된 단일 텍스트 영역에서 영구 누적 로어 수집량을 즉시 확인하기 위한 표시
	FText LoreProgressText = DiscoveredLoreCount > 0 ? FText::Format(FText::FromString(TEXT("수집한 기록물: {0}개")), FText::AsNumber(DiscoveredLoreCount)) : FText::FromString(TEXT("아직 수집한 기록물이 없다."));
	TXT_LoreEmptyNotice->SetText(LoreProgressText);
}

void UMD_QuestWidget::RebuildMainQuestList()
{
	if (!IsValid(PNL_MainQuestList))
	{
		return;
	}

	PNL_MainQuestList->ClearChildren();

	if (IsValid(TXT_MainEmptyNotice))
	{
		TXT_MainEmptyNotice->SetVisibility(MainQuests.Num() > 0 ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		if (MainQuests.Num() == 0)
		{
			TXT_MainEmptyNotice->SetText(FText::FromString(TEXT("아직 계약이 시작되지 않았다.")));
		}
	}

	if (!MainQuestRowClass)
	{
		return;
	}

	// 배열 순서 = Day 순서 (기획 `09` §1: Day1→2→3). 항상 세로 전체를 나열하고
	// 각 행이 자기 상태(잠김/진행중/완료)에 맞춰 펼침 여부를 스스로 결정한다.
	for (const FMD_MainQuestState& MainQuest : MainQuests)
	{
		UMD_MainQuestRowWidget* Row = CreateWidget<UMD_MainQuestRowWidget>(this, MainQuestRowClass);
		if (!IsValid(Row))
		{
			continue;
		}

		Row->SetMainQuest(MainQuest);
		PNL_MainQuestList->AddChild(Row);
	}
}

void UMD_QuestWidget::RebuildRepeatQuestList()
{
	if (!IsValid(PNL_RepeatQuestList))
	{
		return;
	}

	PNL_RepeatQuestList->ClearChildren();

	if (IsValid(TXT_RepeatEmptyNotice))
	{
		TXT_RepeatEmptyNotice->SetVisibility(RepeatQuests.Num() > 0 ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		if (RepeatQuests.Num() == 0)
		{
			// `09` §4: 현장을 한 번 클리어해야 반복퀘가 열린다.
			TXT_RepeatEmptyNotice->SetText(FText::FromString(TEXT("현장을 한 번 클리어하면 반복 퀘스트가 열린다.")));
		}
	}

	if (!RepeatQuestCardClass)
	{
		return;
	}

	for (int32 Index = 0; Index < RepeatQuests.Num(); ++Index)
	{
		UMD_QuestCardWidget* Card = CreateWidget<UMD_QuestCardWidget>(this, RepeatQuestCardClass);
		if (!IsValid(Card))
		{
			continue;
		}

		Card->SetQuest(Index, RepeatQuests[Index]);
		Card->OnAcceptRequested.AddDynamic(this, &UMD_QuestWidget::HandleCardAcceptRequested);
		Card->OnDeclineRequested.AddDynamic(this, &UMD_QuestWidget::HandleCardDeclineRequested);
		Card->OnAbandonRequested.AddDynamic(this, &UMD_QuestWidget::HandleCardAbandonRequested);

		PNL_RepeatQuestList->AddChild(Card);
	}
}

void UMD_QuestWidget::HandleTabMainClicked()
{
	SelectTab(EMD_QuestTab::Main);
}

void UMD_QuestWidget::HandleTabRepeatClicked()
{
	SelectTab(EMD_QuestTab::Repeat);
}

void UMD_QuestWidget::HandleTabLoreClicked()
{
	SelectTab(EMD_QuestTab::Lore);
}

void UMD_QuestWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

void UMD_QuestWidget::HandleCardAcceptRequested(int32 QuestIndex)
{
	OnRepeatAcceptRequested.Broadcast(QuestIndex);
}

void UMD_QuestWidget::HandleCardDeclineRequested(int32 QuestIndex)
{
	OnRepeatDeclineRequested.Broadcast(QuestIndex);
}

void UMD_QuestWidget::HandleCardAbandonRequested(int32 QuestIndex)
{
	OnRepeatAbandonRequested.Broadcast(QuestIndex);
}
