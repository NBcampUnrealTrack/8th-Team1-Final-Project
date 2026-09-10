// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopWidget.h"

#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/Texture2D.h"
#include "Groups/CommonButtonGroupBase.h"
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

	// BP 기본 카탈로그도 활성화 시 비동기 로드를 시작하여 화면에 반영
	SetCatalog(Catalog);
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

void UMD_ShopWidget::NativeDestruct()
{
	// 닫힌 화면으로 지연된 완료 콜백이 전달되지 않도록 요청 취소
	if (DefinitionLoadHandle.IsValid() == true)
	{
		// 이 화면의 요청과 에셋 보관만 해제
		DefinitionLoadHandle->CancelHandle();
	}
	// 위젯 수명이 끝난 뒤 핸들 참조 정리
	DefinitionLoadHandle.Reset();
	// 부모 위젯의 해제 절차 수행
	Super::NativeDestruct();
}

void UMD_ShopWidget::SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget)
{
	// 화면을 연 상인 메뉴 저장
	OwningMerchantMenu = InMenuWidget;
}

void UMD_ShopWidget::SetCatalog(UMD_ShopCatalog* InCatalog)
{
	// 같은 카탈로그를 갱신할 때 진행 중 로드와 보관 핸들 재사용
	if (Catalog == InCatalog && DefinitionLoadHandle.IsValid() == true)
	{
		// 현재 로드 상태에 맞게 표시 내용 갱신
		RefreshDisplay();
		// 중복 요청 생성 방지
		return;
	}
	// 이전 카탈로그의 지연 콜백과 에셋 보관 해제
	if (DefinitionLoadHandle.IsValid() == true)
	{
		// 이전 화면 데이터에 해당하는 요청만 취소
		DefinitionLoadHandle->CancelHandle();
	}
	// 새 카탈로그 요청 전 핸들 초기화
	DefinitionLoadHandle.Reset();
	// 표시할 카탈로그 저장
	Catalog = InCatalog;
	// 새 목록에서 이전 품목이 선택된 상태 방지
	SelectedItemAssetId = FPrimaryAssetId();
	// 유효한 카탈로그에 한해 정의를 비동기로 요청
	if (IsValid(Catalog) == true)
	{
		// 로드된 정의는 핸들로 유지하고 완료 후 전체 UI 갱신
		DefinitionLoadHandle = Catalog->LoadItemDefinitions(FStreamableDelegate::CreateUObject(this, &UMD_ShopWidget::RefreshDisplay));
	}
	// 로드 중에는 기존 목록과 구매 버튼 초기화
	RefreshDisplay();
}

void UMD_ShopWidget::SetShopInfo(const FMD_MerchantMenuInfo& InInfo)
{
	// 전달받은 상점 정보 저장
	ShopInfo = InInfo;

	// 크레딧과 구매 상태 갱신
	RefreshDisplay();
}

void UMD_ShopWidget::SetProgress(int32 InQuestCompletions, const TArray<FPrimaryAssetId>& InOwnedItemIds)
{
	// 해금 판정용 진행 상태 저장
	QuestCompletions = InQuestCompletions;
	OwnedItemIds = InOwnedItemIds;

	// 진행 상태에 맞게 화면 갱신
	RefreshDisplay();
}

void UMD_ShopWidget::SetFullyStockedItems(const TArray<FPrimaryAssetId>& InItemIds)
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
	SelectedItemAssetId = FPrimaryAssetId();

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

	// 모든 상점 카드를 동일한 선택 필수 그룹에 등록하기 위한 그룹 준비
	if (IsValid(ItemCardButtonGroup) == false)
	{
		ItemCardButtonGroup = NewObject<UCommonButtonGroupBase>(this);
	}
	if (IsValid(ItemCardButtonGroup) == true)
	{
		ItemCardButtonGroup->RemoveAll();
		ItemCardButtonGroup->SetSelectionRequired(true);
	}

	// 기존 동적 카드 제거
	WB_ItemEntries->ClearChildren();

	// 카탈로그가 없거나 정의가 준비되지 않으면 목록 표시 보류
	if (IsValid(Catalog) == false || Catalog->IsItemDefinitionsLoaded() == false)
	{
		RefreshSelectedItem();
		return;
	}

	if (ItemCardClass == nullptr)
	{
		RefreshSelectedItem();
		return;
	}

	// 기존 상세 품목과 일치하는 카드의 선택 그룹 인덱스 저장
	int32 selectedItemCardIndex = INDEX_NONE;

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
		// 정의의 전체 AssetId로 현재 플레이어의 구매 이력 확인
		bool ownedItem = OwnedItemIds.Contains(item.GetItemAssetId());
		if (item.Price <= 0)
		{
			ownedItem = true;
		}

		// 카드 데이터 설정 후 기본 버튼 클릭에 품목 선택 처리 연결
		itemCard->SetItem(item, ownedItem);
		itemCard->SetIsSelectable(true);
		if (IsValid(ItemCardButtonGroup) == true)
		{
			ItemCardButtonGroup->AddWidget(itemCard);
		}
		// 카드 클릭 시 타입과 이름을 보존한 선택 이벤트 연결
		itemCard->OnClicked().AddUObject(this, &UMD_ShopWidget::HandleItemSelected, item.GetItemAssetId());
		WB_ItemEntries->AddChild(itemCard);

		// 분류 진입 시 첫 품목을 기본 선택
		if (SelectedItemAssetId.IsValid() == false)
		{
			// 첫 판매 품목의 전체 식별자로 기본 선택 설정
			SelectedItemAssetId = item.GetItemAssetId();
		}

		// 전체 식별자가 일치하는 카드의 선택 인덱스 복원
		if (item.GetItemAssetId() == SelectedItemAssetId && IsValid(ItemCardButtonGroup) == true)
		{
			// 전체 식별자가 일치하는 카드의 그룹 선택 위치 저장
			selectedItemCardIndex = ItemCardButtonGroup->FindButtonIndex(itemCard);
		}
	}

	// 목록 재생성 전 상세 패널에 표시하던 품목 카드 선택 복원
	if (IsValid(ItemCardButtonGroup) == true)
	{
		if (selectedItemCardIndex != INDEX_NONE)
		{
			ItemCardButtonGroup->SelectButtonAtIndex(selectedItemCardIndex, false);
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
	// 로딩 전 또는 선택 전에는 미등록 품목 조회 생략
	if (IsValid(Catalog) == true && Catalog->IsItemDefinitionsLoaded() == true && SelectedItemAssetId.IsValid() == true)
	{
		// 이름만 같은 다른 타입을 제외한 정확한 판매 항목 조회
		foundItem = Catalog->FindItemByAssetId(SelectedItemAssetId, selectedItem);
	}

	// 여러 표시 요소가 함께 사용할 공통 정의 조회
	UMD_ItemDefinition* Definition = foundItem == true ? selectedItem.GetDefinition() : nullptr;
	// 선택 품목 또는 정의가 없으면 상세 패널 내용 초기화
	if (foundItem == false || IsValid(Definition) == false)
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
	// 정의의 전체 AssetId로 현재 플레이어의 구매 이력 확인
	bool ownedItem = OwnedItemIds.Contains(selectedItem.GetItemAssetId());
	if (selectedItem.Price <= 0)
	{
		ownedItem = true;
	}

	// 선택 품목의 텍스트와 아이콘 표시
	if (IsValid(Txt_DetailName) == true)
	{
		// 정의에서 관리하는 공통 이름 표시
		Txt_DetailName->SetText(Definition->ItemName);
	}

	if (IsValid(Txt_DetailDescription) == true)
	{
		// 정의에서 관리하는 공통 설명 표시
		Txt_DetailDescription->SetText(Definition->Description);
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
		// 공통 정의의 아이콘을 표시하며 미지정 상태에서는 이전 이미지 제거
		Img_DetailIcon->SetBrushFromTexture(Definition->ItemIcon);
	}

	// 해금, 크레딧과 가격을 기준으로 구매 버튼 상태 결정
	if (IsValid(Btn_Buy) == true)
	{
		// 서버와 같은 전체 AssetId 보유 이력으로 선행 조건 판정
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
	SelectedItemAssetId = FPrimaryAssetId();
	RebuildItemList();
}

void UMD_ShopWidget::HandleCategoryClicked(EMD_ShopCategory Category)
{
	// 생성한 버튼에 연결된 아이템 카테고리 선택
	SelectCategory(Category);
}

void UMD_ShopWidget::HandleItemSelected(FPrimaryAssetId ItemAssetId)
{
	// 선택한 카드의 품목을 상세 패널 대상으로 저장
	SelectedItemAssetId = ItemAssetId;

	// 선택 품목 상세 정보 갱신
	RefreshSelectedItem();
}

void UMD_ShopWidget::HandleBuyClicked()
{
	// 선택 또는 판매 정의의 준비가 끝나지 않은 구매 요청 차단
	if (SelectedItemAssetId.IsValid() == false || IsValid(Catalog) == false || Catalog->IsItemDefinitionsLoaded() == false)
	{
		return;
	}

	// 선택된 판매 항목에서 구매 상태 비교용 전체 식별자 조회
	FMD_ShopItem SelectedItem;
	// 카탈로그에 없는 선택값의 구매 요청 차단
	if (Catalog->FindItemByAssetId(SelectedItemAssetId, SelectedItem) == false)
	{
		// 유효한 판매 항목이 없는 요청 종료
		return;
	}

	// 전체 식별자가 일치하는 구매 제한 품목의 별도 거부 이벤트 전달
	if (FullyStockedItemIds.Contains(SelectedItem.GetItemAssetId()) == true)
	{
		// 사용하지 않는 품목 식별자 없이 구매 제한 알림
		OnPurchaseRejected.Broadcast();
		return;
	}

	// 실제 구매 시스템으로 선택 품목 요청 전달
	OnItemPurchaseRequested.Broadcast(SelectedItemAssetId);
}
