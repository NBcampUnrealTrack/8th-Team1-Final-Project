// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UI/MD_CommonButtonBase.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"
#include "UI/Merchant/MD_ShopItemCardWidget.h"

void UMD_ShopWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 구매 버튼의 현재 활성화 주기 클릭 처리를 등록한다.
	if (IsValid(Btn_Buy) == true)
	{
		auto& clickedEvent = Btn_Buy->OnClicked();
		clickedEvent.RemoveAll(this);
		clickedEvent.AddUObject(this, &UMD_ShopWidget::HandleBuyClicked);
	}

	// 아이템 카테고리 열거형을 기준으로 카테고리 버튼 자동 생성
	BuildCategoryButtons();

	// 상점 활성화 중 기존 상인 메뉴 패널 숨김
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(false);
	}

	// 현재 상점 데이터를 화면에 반영
	RefreshDisplay();
}

void UMD_ShopWidget::NativeOnDeactivated()
{
	// 현재 상점 인스턴스가 등록한 구매 버튼 연결을 제거한다.
	if (IsValid(Btn_Buy) == true)
	{
		Btn_Buy->OnClicked().RemoveAll(this);
	}

	// 상점 종료 후 기존 상인 메뉴 패널 복원
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(true);
	}

	Super::NativeOnDeactivated();
}

void UMD_ShopWidget::SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget)
{
	// 화면을 연 상인 메뉴 저장
	OwningMerchantMenu = InMenuWidget;
}

void UMD_ShopWidget::SetCatalog(UMD_ShopCatalog* InCatalog)
{
	// 표시할 카탈로그 저장
	Catalog = InCatalog;

	// 카탈로그 변경 사항 반영
	RefreshDisplay();
}

void UMD_ShopWidget::SetShopInfo(const FMD_MerchantMenuInfo& InInfo)
{
	// 전달받은 상점 정보 저장
	ShopInfo = InInfo;

	// 크레딧과 구매 상태 갱신
	RefreshDisplay();
}

void UMD_ShopWidget::SetProgress(int32 InQuestCompletions, const TArray<FName>& InOwnedItemIds)
{
	// 해금 판정용 진행 상태 저장
	QuestCompletions = InQuestCompletions;
	OwnedItemIds = InOwnedItemIds;

	// 진행 상태에 맞게 화면 갱신
	RefreshDisplay();
}

void UMD_ShopWidget::SetFullyStockedItems(const TArray<FName>& InItemIds)
{
	// 구매 제한 품목 목록 저장
	FullyStockedItemIds = InItemIds;

	// 선택 품목의 구매 가능 상태 갱신
	RefreshSelectedItem();
}

void UMD_ShopWidget::SelectCategory(EMD_ShopCategory InCategory)
{
	// 선택한 아이템 카테고리 저장
	SelectedCategory = InCategory;
	bShowAllCategories = false;

	// 새 분류의 첫 품목이 선택되도록 기존 선택 초기화
	SelectedItemId = NAME_None;

	// 선택 분류에 맞는 목록 재구성
	RebuildItemList();
}

void UMD_ShopWidget::RefreshDisplay()
{
	// 보유 크레딧을 헤더에 표시
	if (IsValid(Txt_Credits) == true)
	{
		FText creditsText = FText::AsNumber(ShopInfo.Credits);
		Txt_Credits->SetText(creditsText);
	}

	// 품목 목록과 상세 패널 갱신
	RebuildItemList();
}

void UMD_ShopWidget::RebuildItemList()
{
	// 카드 컨테이너가 없으면 목록 생성 중단
	if (IsValid(WB_ItemEntries) == false)
	{
		return;
	}

	// 기존 동적 카드 제거
	WB_ItemEntries->ClearChildren();

	// 카탈로그 또는 카드 클래스가 없으면 상세 패널만 초기화
	if (IsValid(Catalog) == false)
	{
		RefreshSelectedItem();
		return;
	}

	if (ItemCardClass == nullptr)
	{
		RefreshSelectedItem();
		return;
	}

	// 카탈로그 순서대로 현재 분류 품목 카드 생성
	for (FMD_ShopItem item : Catalog->Items)
	{
		if (bShowAllCategories == false)
		{
			if (item.Category != SelectedCategory)
			{
				continue;
			}
		}

		UMD_ShopItemCardWidget* itemCard = CreateWidget<UMD_ShopItemCardWidget>(this, ItemCardClass);
		if (IsValid(itemCard) == false)
		{
			continue;
		}

		// 카드에 전달할 지급 완료 상태 확인
		bool ownedItem = OwnedItemIds.Contains(item.ItemId);
		if (item.Price <= 0)
		{
			ownedItem = true;
		}

		// 카드 데이터 설정 후 기본 버튼 클릭에 품목 선택 처리 연결
		itemCard->SetItem(item, ownedItem);
		itemCard->OnClicked().AddUObject(this, &UMD_ShopWidget::HandleItemSelected, item.ItemId);
		WB_ItemEntries->AddChild(itemCard);

		// 분류 진입 시 첫 품목을 기본 선택
		if (SelectedItemId.IsNone() == true)
		{
			SelectedItemId = item.ItemId;
		}
	}

	// 선택된 품목 상세 정보 갱신
	RefreshSelectedItem();
}

void UMD_ShopWidget::RefreshSelectedItem()
{
	// 선택 품목을 카탈로그에서 조회
	FMD_ShopItem selectedItem;
	bool foundItem = false;
	if (IsValid(Catalog) == true)
	{
		foundItem = Catalog->FindItem(SelectedItemId, selectedItem);
	}

	// 선택 품목이 없으면 상세 패널 내용 초기화
	if (foundItem == false)
	{
		if (IsValid(Txt_DetailName) == true)
		{
			Txt_DetailName->SetText(FText::GetEmpty());
		}

		if (IsValid(Txt_DetailDescription) == true)
		{
			Txt_DetailDescription->SetText(FText::GetEmpty());
		}

		if (IsValid(Txt_DetailPrice) == true)
		{
			Txt_DetailPrice->SetText(FText::GetEmpty());
		}

		if (IsValid(Txt_Owned) == true)
		{
			Txt_Owned->SetText(FText::GetEmpty());
		}

		if (IsValid(Img_DetailIcon) == true)
		{
			Img_DetailIcon->SetBrushFromTexture(nullptr);
		}

		if (IsValid(Btn_Buy) == true)
		{
			Btn_Buy->SetIsEnabled(false);
		}

		return;
	}

	// 이미 지급된 일회성 품목 여부 확인
	bool ownedItem = OwnedItemIds.Contains(selectedItem.ItemId);
	if (selectedItem.Price <= 0)
	{
		ownedItem = true;
	}

	// 선택 품목의 텍스트와 아이콘 표시
	if (IsValid(Txt_DetailName) == true)
	{
		Txt_DetailName->SetText(selectedItem.DisplayName);
	}

	if (IsValid(Txt_DetailDescription) == true)
	{
		Txt_DetailDescription->SetText(selectedItem.Description);
	}

	if (IsValid(Txt_DetailPrice) == true)
	{
		FText priceText = ownedItem == true ? FText::FromString(TEXT("-")) : selectedItem.GetPriceText();
		Txt_DetailPrice->SetText(priceText);
	}

	if (IsValid(Txt_Owned) == true)
	{
		Txt_Owned->SetText(ownedItem == true ? FText::FromString(TEXT("보유 1개")) : FText::FromString(TEXT("보유 0개")));
	}

	if (IsValid(Img_DetailIcon) == true)
	{
		UTexture2D* iconTexture = selectedItem.Icon.LoadSynchronous();
		Img_DetailIcon->SetBrushFromTexture(iconTexture);
	}

	// 해금, 크레딧과 가격을 기준으로 구매 버튼 상태 결정
	if (IsValid(Btn_Buy) == true)
	{
		bool unlockedItem = selectedItem.IsUnlocked(ShopInfo.Affinity, QuestCompletions, OwnedItemIds);
		bool affordableItem = ShopInfo.Credits >= selectedItem.Price;
		bool paidItem = selectedItem.Price > 0;
		bool canPurchase = unlockedItem == true;
		if (affordableItem == false)
		{
			canPurchase = false;
		}

		if (paidItem == false)
		{
			canPurchase = false;
		}

		// 이미 지급된 품목의 추가 구매 차단
		if (ownedItem == true)
		{
			canPurchase = false;
		}

		Btn_Buy->SetIsEnabled(canPurchase);
	}
}

void UMD_ShopWidget::BuildCategoryButtons()
{
	// 카테고리 컨테이너가 없으면 자동 생성 중단
	if (IsValid(SB_Categories) == false)
	{
		return;
	}

	// 에셋에 배치된 첫 버튼의 클래스를 동적 버튼 템플릿으로 재사용
	if (CategoryButtonClass == nullptr)
	{
		if (SB_Categories->GetChildrenCount() > 0)
		{
			UWidget* firstCategoryWidget = SB_Categories->GetChildAt(0);
			UMD_CommonButtonBase* firstCategoryButton = Cast<UMD_CommonButtonBase>(firstCategoryWidget);
			if (IsValid(firstCategoryButton) == true)
			{
				CategoryButtonClass = firstCategoryButton->GetClass();
			}
		}
	}

	// 사용할 버튼 클래스가 없으면 기존 디자이너 구성을 유지
	if (CategoryButtonClass == nullptr)
	{
		return;
	}

	// 기존 디자이너 배치 버튼을 제거하고 자동 생성 목록 준비
	SB_Categories->ClearChildren();

	// 전체 품목을 표시하는 선두 버튼 생성
	UMD_CommonButtonBase* allButton = CreateWidget<UMD_CommonButtonBase>(this, CategoryButtonClass);
	if (IsValid(allButton) == true)
	{
		allButton->SetContentText(FText::FromString(TEXT("전체")));
		allButton->OnClicked().AddUObject(this, &UMD_ShopWidget::HandleCategoryAllClicked);
		SB_Categories->AddChild(allButton);
	}

	// 아이템 카테고리 열거형의 유효 항목마다 버튼 생성
	UEnum* categoryEnum = StaticEnum<EMD_ShopCategory>();
	if (IsValid(categoryEnum) == false)
	{
		return;
	}

	int32 categoryCount = categoryEnum->NumEnums() - 1;
	for (int32 categoryIndex = 0; categoryIndex < categoryCount; ++categoryIndex)
	{
		EMD_ShopCategory category = static_cast<EMD_ShopCategory>(categoryEnum->GetValueByIndex(categoryIndex));
		UMD_CommonButtonBase* categoryButton = CreateWidget<UMD_CommonButtonBase>(this, CategoryButtonClass);
		if (IsValid(categoryButton) == false)
		{
			continue;
		}

		FText categoryName = categoryEnum->GetDisplayNameTextByIndex(categoryIndex);
		categoryButton->SetContentText(categoryName);
		categoryButton->OnClicked().AddUObject(this, &UMD_ShopWidget::HandleCategoryClicked, category);
		SB_Categories->AddChild(categoryButton);
	}
}

void UMD_ShopWidget::HandleCategoryAllClicked()
{
	// 모든 아이템 카테고리를 함께 표시하도록 상태 변경
	bShowAllCategories = true;
	SelectedItemId = NAME_None;
	RebuildItemList();
}

void UMD_ShopWidget::HandleCategoryClicked(EMD_ShopCategory Category)
{
	// 생성한 버튼에 연결된 아이템 카테고리 선택
	SelectCategory(Category);
}

void UMD_ShopWidget::HandleItemSelected(FName ItemId)
{
	// 선택한 카드의 품목을 상세 패널 대상으로 저장
	SelectedItemId = ItemId;

	// 선택 품목 상세 정보 갱신
	RefreshSelectedItem();
}

void UMD_ShopWidget::HandleBuyClicked()
{
	// 선택 품목이 없으면 구매 요청 중단
	if (SelectedItemId.IsNone() == true)
	{
		return;
	}

	// 가득 찬 품목은 별도 거부 이벤트로 전달
	if (FullyStockedItemIds.Contains(SelectedItemId) == true)
	{
		OnPurchaseRejected.Broadcast(SelectedItemId);
		return;
	}

	// 실제 구매 시스템으로 선택 품목 요청 전달
	OnItemPurchaseRequested.Broadcast(SelectedItemId);
}
