#include "UI/Merchant/MD_QuestScreenWidget.h"

#include "Data/Lore/MD_LoreCatalog.h"
#include "UI/MD_CommonButtonBase.h"
#include "UI/Merchant/MD_LoreQuestTabWidget.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"

void UMD_QuestScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 상단 탭 버튼의 기본 클릭 이벤트를 화면 전환 함수에 연결
	if (IsValid(Btn_QuestTab) == true)
	{
		Btn_QuestTab->OnClicked().AddUObject(this, &UMD_QuestScreenWidget::ShowQuestTab);
	}
	if (IsValid(Btn_LoreTab) == true)
	{
		Btn_LoreTab->OnClicked().AddUObject(this, &UMD_QuestScreenWidget::ShowLoreTab);
	}

	// 하위 퀘스트 탭의 서버 액션 요청을 화면 외부 이벤트에 연결
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->OnRepeatAcceptRequested.AddUniqueDynamic(this, &UMD_QuestScreenWidget::HandleRepeatAcceptRequested);
		WBP_MD_QuestTab->OnRepeatDeclineRequested.AddUniqueDynamic(this, &UMD_QuestScreenWidget::HandleRepeatDeclineRequested);
		WBP_MD_QuestTab->OnRepeatAbandonRequested.AddUniqueDynamic(this, &UMD_QuestScreenWidget::HandleRepeatAbandonRequested);
	}

	RefreshDisplay();
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::NativeDestruct()
{
	// 재생성 시 상단 탭 클릭 연결이 중복되지 않도록 해제
	if (IsValid(Btn_QuestTab) == true)
	{
		Btn_QuestTab->OnClicked().RemoveAll(this);
	}
	if (IsValid(Btn_LoreTab) == true)
	{
		Btn_LoreTab->OnClicked().RemoveAll(this);
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

void UMD_QuestScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 퀘스트 화면이 상인 메뉴 위에 표시되는 동안 기존 메뉴 패널 숨김
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(false);
	}
	RefreshDisplay();
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::NativeOnDeactivated()
{
	// 퀘스트 화면 종료 시 뒤에 남은 상인 메뉴 패널 복구
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(true);
	}

	// Common UI Stack이 비활성화 델리게이트를 전달하도록 공통 처리를 실행한다.
	Super::NativeOnDeactivated();
}

void UMD_QuestScreenWidget::SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget)
{
	// 화면 활성화와 비활성화에서 동일 메뉴 패널을 제어하기 위한 참조 저장
	OwningMerchantMenu = InMenuWidget;
}

void UMD_QuestScreenWidget::SetMainQuests(const TArray<FMD_MainQuestState>& InMainQuests)
{
	// 하위 위젯 생성 시점과 관계없이 사용할 메인 퀘스트 상태 저장
	MainQuests = InMainQuests;
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->SetMainQuests(MainQuests);
	}
}

void UMD_QuestScreenWidget::SetRepeatQuests(const TArray<FMD_RepeatQuestState>& InRepeatQuests)
{
	// 하위 위젯 생성 시점과 관계없이 사용할 반복 퀘스트 상태 저장
	RepeatQuests = InRepeatQuests;
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->SetRepeatQuests(RepeatQuests);
	}
}

void UMD_QuestScreenWidget::SetLoreData(UMD_LoreCatalog* InLoreCatalog, const TArray<FPrimaryAssetId>& InDiscoveredLoreAssetIds)
{
	// 현재 현장의 정적 로어 정의와 플레이어별 전역 수집 원장 저장
	LoreCatalog = InLoreCatalog;
	DiscoveredLoreAssetIds = InDiscoveredLoreAssetIds;

	// 디자이너 로어 탭이 준비된 경우 현재 카탈로그와 전역 원장을 즉시 반영
	if (IsValid(WBP_MD_LoreTab) == true)
	{
		WBP_MD_LoreTab->SetLoreData(LoreCatalog, DiscoveredLoreAssetIds);
	}
}

void UMD_QuestScreenWidget::RefreshDisplay()
{
	// 캐시된 메인 및 반복 퀘스트를 하위 퀘스트 탭에 함께 적용
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->SetMainQuests(MainQuests);
		WBP_MD_QuestTab->SetRepeatQuests(RepeatQuests);
	}
	// 현재 로어 카탈로그와 수집 원장을 하위 로어 탭에 다시 적용
	SetLoreData(LoreCatalog, DiscoveredLoreAssetIds);
}

void UMD_QuestScreenWidget::ShowQuestTab()
{
	// 상단 퀘스트 버튼 선택에 맞춰 퀘스트 하위 화면 활성화
	bShowingLoreTab = false;
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::ShowLoreTab()
{
	// 상단 로어 버튼 선택에 맞춰 로어 하위 화면 활성화
	bShowingLoreTab = true;
	ApplySelectedTab();
}

void UMD_QuestScreenWidget::ApplySelectedTab()
{
	// 선택한 탭만 표시하고 다른 하위 화면은 레이아웃에서 제외
	if (IsValid(WBP_MD_QuestTab) == true)
	{
		WBP_MD_QuestTab->SetVisibility(bShowingLoreTab == false ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (IsValid(WBP_MD_LoreTab) == true)
	{
		WBP_MD_LoreTab->SetVisibility(bShowingLoreTab == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 상단 버튼의 Common UI 선택 스타일을 현재 탭과 동기화
	if (IsValid(Btn_QuestTab) == true)
	{
		Btn_QuestTab->SetIsSelectable(true);
		Btn_QuestTab->SetIsSelected(bShowingLoreTab == false, false);
	}
	if (IsValid(Btn_LoreTab) == true)
	{
		Btn_LoreTab->SetIsSelectable(true);
		Btn_LoreTab->SetIsSelected(bShowingLoreTab, false);
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
