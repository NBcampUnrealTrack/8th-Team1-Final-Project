#include "UI/Merchant/MD_QuestScreenWidget.h"

#include "UI/MD_CommonButtonBase.h"
#include "Components/WidgetSwitcher.h"
#include "UI/Merchant/MD_LoreQuestTabWidget.h"

void UMD_QuestScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 상단 탭 버튼의 기본 클릭 이벤트를 화면 전환 함수에 연결
	if (IsValid(Btn_QuestTab_1) == true)
	{
		// 퀘스트 탭 클릭을 중복 없이 화면 전환 함수에 연결
		Btn_QuestTab_1->OnClicked().AddUObject(this, &UMD_QuestScreenWidget::ShowQuestTab);
	}
	// 기록 탭 버튼의 선택 입력 연결 여부 확인
	if (IsValid(Btn_QuestTab_2) == true)
	{
		// 로어 탭 클릭을 중복 없이 화면 전환 함수에 연결
		Btn_QuestTab_2->OnClicked().AddUObject(this, &UMD_QuestScreenWidget::ShowLoreTab);
	}

	// 하위 퀘스트 탭의 서버 액션 요청을 화면 외부 이벤트에 연결
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->OnRepeatAcceptRequested.AddUniqueDynamic(this, &UMD_QuestScreenWidget::HandleRepeatAcceptRequested);
		WBP_MD_QuestTab->OnRepeatDeclineRequested.AddUniqueDynamic(this, &UMD_QuestScreenWidget::HandleRepeatDeclineRequested);
		WBP_MD_QuestTab->OnRepeatAbandonRequested.AddUniqueDynamic(this, &UMD_QuestScreenWidget::HandleRepeatAbandonRequested);
	}

	// 화면 재구성 이후에도 기존 탭 선택 표시 복원
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::NativeDestruct()
{
	// 재생성 시 상단 탭 클릭 연결이 중복되지 않도록 해제
	if (IsValid(Btn_QuestTab_1) == true)
	{
		// 화면 소멸 시 퀘스트 탭의 화면 전환 연결 해제
		Btn_QuestTab_1->OnClicked().RemoveAll(this);
	}
	// 화면 소멸 시 기록 탭 버튼의 연결 대상 확인
	if (IsValid(Btn_QuestTab_2) == true)
	{
		// 화면 소멸 시 로어 탭의 화면 전환 연결 해제
		Btn_QuestTab_2->OnClicked().RemoveAll(this);
	}

	// 하위 퀘스트 탭이 화면 수명 이후 외부 이벤트를 호출하지 않도록 해제
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->OnRepeatAcceptRequested.RemoveDynamic(this, &UMD_QuestScreenWidget::HandleRepeatAcceptRequested);
		WBP_MD_QuestTab->OnRepeatDeclineRequested.RemoveDynamic(this, &UMD_QuestScreenWidget::HandleRepeatDeclineRequested);
		WBP_MD_QuestTab->OnRepeatAbandonRequested.RemoveDynamic(this, &UMD_QuestScreenWidget::HandleRepeatAbandonRequested);
	}

	Super::NativeDestruct();
}

void UMD_QuestScreenWidget::SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests)
{
	// 메인 퀘스트 상태를 보관하고 표시하는 하위 탭 확인
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		// 상위 화면의 중복 배열 보관 없이 하위 탭에 직접 전달
		WBP_MD_QuestTab->SetMainQuests(InMainQuests);
	}
}

void UMD_QuestScreenWidget::SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests)
{
	// 반복 퀘스트 상태를 보관하고 표시하는 하위 탭 확인
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		// 상위 화면의 중복 배열 보관 없이 하위 탭에 직접 전달
		WBP_MD_QuestTab->SetRepeatQuests(InRepeatQuests);
	}
}

void UMD_QuestScreenWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds, FText InSiteName)
{
	// 현장과 수집 원장을 보관하고 표시하는 하위 탭 확인
	if (IsValid(WBP_MD_LoreTab) == true)
	{
		// 하위 탭의 변경 감지와 카드 재사용 경로에 직접 전달
		WBP_MD_LoreTab->SetLoreData(InLoreCatalog, InDiscoveredLoreAssetIds, InSiteName);
	}
}

void UMD_QuestScreenWidget::ShowQuestTab()
{
	// 상단 퀘스트 버튼 선택에 맞춰 퀘스트 하위 화면 활성화
	bShowingLoreTab = false;
	// 퀘스트 페이지와 버튼 선택 표시 반영
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::ShowLoreTab()
{
	// 상단 로어 버튼 선택에 맞춰 로어 하위 화면 활성화
	bShowingLoreTab = true;
	// 기록 페이지와 버튼 선택 표시 반영
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::ApplySelectedTab()
{
	// 탭 전환 시 해제할 이전 버튼 조회
	UMD_CommonButtonBase* OtherButton = bShowingLoreTab == true ? Btn_QuestTab_1.Get() : Btn_QuestTab_2.Get();
	// 실제 선택 상태인 이전 버튼만 갱신
	if (IsValid(OtherButton) == true && OtherButton->GetSelected() == true)
	{
		// 토글 불가 버튼도 기존 선택을 해제하여 단일 선택 유지
		OtherButton->ClearSelection();
	}

	// 표시할 페이지에 대응하는 선택 버튼 조회
	UMD_CommonButtonBase* SelectedButton = bShowingLoreTab == true ? Btn_QuestTab_2.Get() : Btn_QuestTab_1.Get();
	// 이미 선택된 버튼의 스타일 중복 갱신 방지
	if (IsValid(SelectedButton) == true && SelectedButton->GetSelected() == false)
	{
		// 버튼 자체의 선택 스타일과 블루프린트 연출 적용
		SelectedButton->SetIsSelected(true, false);
	}

	// 페이지 전환을 담당하는 디자이너 스위처 확인
	if (IsValid(WS_Contents) == true)
	{
		// 디자이너 배치 순서와 무관하게 선택한 하위 탭 조회
		UWidget* SelectedTab = bShowingLoreTab == true ? static_cast<UWidget*>(WBP_MD_LoreTab.Get()) : WBP_MD_QuestTab.Get();
		// 유효한 페이지가 바뀐 경우에만 스위처 갱신
		if (IsValid(SelectedTab) == true && WS_Contents->GetActiveWidget() != SelectedTab)
		{
			// 스위처가 선택한 페이지만 표시하도록 전환
			WS_Contents->SetActiveWidget(SelectedTab);
		}
	}
	else
	{
		// 스위처가 생략된 파생 화면의 퀘스트 탭 표시 지원
		if (IsValid(WBP_MD_QuestTab) == true)
		{
			// 선택 상태에 따른 퀘스트 탭 표시 여부 반영
			WBP_MD_QuestTab->SetVisibility(bShowingLoreTab == false ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		// 스위처가 생략된 파생 화면의 기록 탭 표시 지원
		if (IsValid(WBP_MD_LoreTab) == true)
		{
			// 선택 상태에 따른 기록 탭 표시 여부 반영
			WBP_MD_LoreTab->SetVisibility(bShowingLoreTab == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}

void UMD_QuestScreenWidget::HandleRepeatAcceptRequested(int32 QuestIndex)
{
	OnRepeatAcceptRequested.Broadcast(QuestIndex);
}

void UMD_QuestScreenWidget::HandleRepeatDeclineRequested(int32 QuestIndex)
{
	OnRepeatDeclineRequested.Broadcast(QuestIndex);
}

void UMD_QuestScreenWidget::HandleRepeatAbandonRequested(int32 QuestIndex)
{
	OnRepeatAbandonRequested.Broadcast(QuestIndex);
}
