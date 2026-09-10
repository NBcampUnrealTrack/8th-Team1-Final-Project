// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopWidget.h"

#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Pawn.h"
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
	// 닫힌 화면의 불필요한 인벤토리 갱신 중단
	ObserveHotbar(nullptr);
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
	// 비활성화 없이 제거되는 화면의 보관 상태 구독 정리
	ObserveHotbar(nullptr);
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

void UMD_ShopWidget::SetProgress(int32 InQuestCompletions, const TArray<FPrimaryAssetId>& InOwnedItemIds, TArray<FPrimaryAssetId> InBlockedItemIds)
{
	// 해금 판정용 진행 상태 저장
	QuestCompletions = InQuestCompletions;
	// 도구의 무료 재구매와 정의 조건에 사용할 이력 저장
	OwnedItemIds = InOwnedItemIds;
	// 소스 도구를 다시 구매하지 못하도록 등록된 식별자 저장
	BlockedItemIds = MoveTemp(InBlockedItemIds);

	// 진행 상태에 맞게 화면 갱신
	RefreshDisplay();
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
	// 현재 화면 소유자의 보관 상태 변경과 클라이언트 복제 알림 수신
	APawn* OwningPawn = GetOwningPlayerPawn();
	// 소유 Pawn 변경 시 이전 구독을 해제하고 새 Hotbar에 연결
	ObserveHotbar(IsActivated() == true && IsValid(OwningPawn) == true ? OwningPawn->FindComponentByClass<UMD_EquipHotbarComponent>() : nullptr);

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

	// 목록을 구성할 카탈로그 또는 카드 클래스가 없는 경우만 전체 표시 중단
	if (IsValid(Catalog) == false || ItemCardClass == nullptr)
	{
		// 표시할 목록이 없는 상태에서 이전 품목의 선택 제거
		SelectedItemAssetId = FPrimaryAssetId();
		// 이전 상세 정보와 구매 가능 상태 초기화
		RefreshSelectedItem();
		// 품목별 카드 생성 종료
		return;
	}

	// 기존 상세 품목과 일치하는 카드의 선택 그룹 인덱스 저장
	int32 selectedItemCardIndex = INDEX_NONE;
	// 이전 선택 품목이 제외되었을 때 선택할 첫 번째 표시 품목 보관
	FPrimaryAssetId FirstDisplayedItemAssetId;

	// 카탈로그 순서대로 현재 분류 품목 카드 생성
	for (FMD_ShopItem item : Catalog->Items)
	{
		// 다른 분류 또는 해당 정의가 준비되지 않은 품목만 표시에서 제외
		if ((bShowAllCategories == false && item.Category != SelectedCategory) || Catalog->IsItemDefinitionLoaded(item.GetItemAssetId()) == false)
		{
			// 정상 품목의 카드 생성 계속 진행
			continue;
		}

		UMD_ShopItemCardWidget* itemCard = CreateWidget<UMD_ShopItemCardWidget>(this, ItemCardClass);
		if (IsValid(itemCard) == false)
		{
			continue;
		}

		// 카탈로그 원본을 유지하고 카드용 복사본에 이번 구매 가격 반영
		item.Price = item.GetPurchasePrice(OwnedItemIds);
		// 무료 도구 재구매도 숫자 영으로 표시할 품목 전달
		itemCard->SetItem(item);
		itemCard->SetIsSelectable(true);
		if (IsValid(ItemCardButtonGroup) == true)
		{
			ItemCardButtonGroup->AddWidget(itemCard);
		}
		// 카드 클릭 시 타입과 이름을 보존한 선택 이벤트 연결
		itemCard->OnClicked().AddUObject(this, &UMD_ShopWidget::HandleItemSelected, item.GetItemAssetId());
		WB_ItemEntries->AddChild(itemCard);

		// 실제로 카드가 생성된 첫 품목을 대체 선택 대상으로 기록
		if (FirstDisplayedItemAssetId.IsValid() == false)
		{
			// 로드 실패로 제외된 품목이 기본 선택에 남는 상황 방지
			FirstDisplayedItemAssetId = item.GetItemAssetId();
		}

		// 전체 식별자가 일치하는 카드의 선택 인덱스 복원
		if (item.GetItemAssetId() == SelectedItemAssetId && IsValid(ItemCardButtonGroup) == true)
		{
			// 전체 식별자가 일치하는 카드의 그룹 선택 위치 저장
			selectedItemCardIndex = ItemCardButtonGroup->FindButtonIndex(itemCard);
		}
	}

	// 이전 선택이 표시 목록에서 제외되면 첫 정상 품목으로 선택 이동
	if (selectedItemCardIndex == INDEX_NONE)
	{
		// 표시 품목이 전혀 없으면 빈 식별자로 선택 해제
		SelectedItemAssetId = FirstDisplayedItemAssetId;
		// 표시 품목이 있는 경우에만 첫 카드의 선택 위치 지정
		selectedItemCardIndex = FirstDisplayedItemAssetId.IsValid() == true ? 0 : INDEX_NONE;
	}
	// 유지되거나 대체된 정상 품목의 카드 선택 복원
	if (IsValid(ItemCardButtonGroup) == true && selectedItemCardIndex != INDEX_NONE)
	{
		// 품목 클릭 이벤트를 재발생시키지 않고 선택 상태 갱신
		ItemCardButtonGroup->SelectButtonAtIndex(selectedItemCardIndex, false);
	}

	// 선택된 품목 상세 정보 갱신
	RefreshSelectedItem();
}

void UMD_ShopWidget::RefreshSelectedItem()
{
	// 선택 품목을 카탈로그에서 조회
	FMD_ShopItem selectedItem;
	bool foundItem = false;
	// 다른 품목의 로드 실패와 독립적으로 선택 품목의 준비 여부 확인
	if (IsValid(Catalog) == true && SelectedItemAssetId.IsValid() == true && Catalog->IsItemDefinitionLoaded(SelectedItemAssetId) == true)
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

	// 카드 및 서버와 같은 규칙으로 상세 표시와 잔액 검사 금액 확정
	selectedItem.Price = selectedItem.GetPurchasePrice(OwnedItemIds);

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
		// 도구 재구매의 무료 가격을 포함한 실제 결제 금액 표시
		Txt_DetailPrice->SetText(selectedItem.GetPriceText());
	}

	if (IsValid(Txt_Owned) == true)
	{
		// 개당 판매 품목은 구매 이력과 무관하게 무제한 재고 표시
		if (selectedItem.bPricePerUnit == true)
		{
			// 반복 구매 가능한 품목의 상점 잔여 수량 표시
			Txt_Owned->SetText(NSLOCTEXT("MDShop", "StockUnlimited", "잔여 수량: 무제한"));
		}
		else
		{
			// 개당 판매가 아닌 품목은 구매 전 한 개와 구매 후 영 개로 표시
			Txt_Owned->SetText(OwnedItemIds.Contains(selectedItem.GetItemAssetId()) == true
				? NSLOCTEXT("MDShop", "StockEmpty", "잔여 수량: 0개") : NSLOCTEXT("MDShop", "StockAvailable", "잔여 수량: 1개"));
		}
	}

	if (IsValid(Img_DetailIcon) == true)
	{
		// 공통 정의의 아이콘을 표시하며 미지정 상태에서는 이전 이미지 제거
		Img_DetailIcon->SetBrushFromTexture(Definition->ItemIcon);
	}

	// 상점 해금 및 결제 조건과 유형별 정의의 지급 조건을 함께 적용
	if (IsValid(Btn_Buy) == true)
	{
		// 무료 재구매 가격과 등록된 금지 및 실제 지급 조건으로 구매 판정
		Btn_Buy->SetIsEnabled(selectedItem.IsUnlocked(ShopInfo.Affinity, QuestCompletions, OwnedItemIds) == true
			&& selectedItem.Price >= 0 && ShopInfo.Credits >= selectedItem.Price && IsItemGrantable(selectedItem) == true);
	}
}

bool UMD_ShopWidget::IsItemGrantable(FMD_ShopItem Item)
{
	// 보유 업그레이드를 탐색하지 않고 등록된 구매 금지 식별자 검사
	if (BlockedItemIds.Contains(Item.GetItemAssetId()) == true)
	{
		// 버튼 활성화 및 클릭 요청에서 동일한 구매 금지 적용
		return false;
	}
	// 상태를 변경하지 않는 유형별 지급 검사에 사용할 정의 조회
	UMD_ItemDefinition* Definition = Item.GetDefinition();
	// 서버와 같은 구매자와 구매 이력을 전달할 문맥 구성
	FMD_ItemGrantContext Context;
	// 위젯을 소유한 플레이어의 현재 Pawn을 지급 대상으로 지정
	Context.Recipient = GetOwningPlayerPawn();
	// 일회성 효과 등 정의가 명시한 조건에만 구매 이력 제공
	Context.PurchasedItemIds = OwnedItemIds;
	// 구매자와 정의가 준비된 경우에만 BP 확장을 포함한 지급 조건 확인
	return IsValid(Context.Recipient) == true && IsValid(Definition) == true && Definition->CanGrant(Context) == true;
}

void UMD_ShopWidget::ObserveHotbar(UMD_EquipHotbarComponent* InHotbar)
{
	// 동일한 보관 상태를 구독 중인 경우 중복 연결 방지
	if (ObservedHotbar.Get() == InHotbar)
	{
		// 기존 알림 연결 유지
		return;
	}
	// 이전 Pawn의 유효한 보관 컴포넌트 구독 해제
	if (ObservedHotbar.IsValid() == true)
	{
		// 이전 보관 변경으로 현재 화면이 갱신되는 상황 방지
		ObservedHotbar->OnHotbarChanged.RemoveDynamic(this, &UMD_ShopWidget::RefreshDisplay);
	}
	// 화면과 수명을 공유하지 않는 약한 보관 참조 갱신
	ObservedHotbar = InHotbar;
	// 새 보관 컴포넌트가 있는 경우에만 변경 알림 연결
	if (IsValid(InHotbar) == true)
	{
		// 서버 지급과 클라이언트 복제로 선행 도구 조건이 바뀌면 즉시 재검사
		InHotbar->OnHotbarChanged.AddUniqueDynamic(this, &UMD_ShopWidget::RefreshDisplay);
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
	// 선택한 품목이 표시 및 구매 가능한 상태인지 개별 재검증
	if (SelectedItemAssetId.IsValid() == false || IsValid(Catalog) == false || Catalog->IsItemDefinitionLoaded(SelectedItemAssetId) == false)
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

	// 클릭 직전의 선행 도구와 보관 및 효과 조건을 정의를 통해 재검증
	if (IsItemGrantable(SelectedItem) == false)
	{
		// 사용하지 않는 품목 식별자 없이 구매 제한 알림
		OnPurchaseRejected.Broadcast();
		return;
	}

	// 실제 구매 시스템으로 선택 품목 요청 전달
	OnItemPurchaseRequested.Broadcast(SelectedItemAssetId);
}
