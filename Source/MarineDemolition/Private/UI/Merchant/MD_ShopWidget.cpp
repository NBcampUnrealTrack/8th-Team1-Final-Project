// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopWidget.h"

#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/TextBlock.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/Texture2D.h"
#include "Groups/CommonButtonGroupBase.h"
#include "UI/MD_CommonButtonBase.h"
#include "UI/Merchant/MD_ShopItemCardWidget.h"

void UMD_ShopWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 같은 위젯 인스턴스의 카테고리 그룹을 최초 활성화에서만 구성
	if (IsValid(CategoryButtonGroup) == false)
	{
		// 실제 배치된 버튼의 단일 선택 정책 초기화
		InitializeCategoryButtons();
	}
	else
	{
		// 재활성화 시 기존 그룹 연결을 유지하며 분류 표시 복원
		RefreshCategorySelection();
	}
	// BP 기본 카탈로그도 활성화 시 비동기 로드를 시작하여 화면에 반영
	SetCatalog(Catalog);
}

void UMD_ShopWidget::NativeOnDeactivated()
{
	// 닫힌 화면의 불필요한 인벤토리 갱신 중단
	ObserveHotbar(nullptr);

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
	Super::NativeDestruct();
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

	// 크레딧 변경에 필요한 헤더 및 거래 조건만 갱신
	RefreshCredits();
	// 표시 품목과 카드 가격에 영향 없는 정보의 목록 재순회 방지
	RefreshSelectedItem();
}

void UMD_ShopWidget::SetProgress(const TArray<FPrimaryAssetId>& InOwnedItemIds, TArray<FPrimaryAssetId> InBlockedItemIds)
{
	// 동일한 복제 스냅샷의 중복 표시 갱신 방지
	if (OwnedItemIds == InOwnedItemIds && BlockedItemIds == InBlockedItemIds)
	{
		// 현재 카드와 상세 패널 유지
		return;
	}
	// 무료 재구매 가격을 바꾸는 구매 이력 변경 여부
	bool bPricesChanged = OwnedItemIds != InOwnedItemIds;
	// 도구의 무료 재구매와 정의 조건에 사용할 이력 저장
	OwnedItemIds = InOwnedItemIds;
	// 소스 도구를 다시 구매하지 못하도록 등록된 식별자 저장
	BlockedItemIds = MoveTemp(InBlockedItemIds);
	// 카드 가격이 바뀌는 경우에만 목록 갱신
	if (bPricesChanged == true)
	{
		// 구매 이력이 반영된 카드 가격과 상세 상태 적용
		RebuildItemList();
	}
	else
	{
		// 구매 금지 변경을 선택 품목의 거래 조건에 반영
		RefreshSelectedItem();
	}
}

void UMD_ShopWidget::SelectCategory(EMD_ShopCategory InCategory)
{
	// 같은 분류 재클릭 시 현재 카드 선택과 목록 유지
	if (bShowAllCategories == false && SelectedCategory == InCategory)
	{
		// 변경 없는 필터의 재적용 생략
		return;
	}
	// 선택한 아이템 카테고리 저장
	SelectedCategory = InCategory;
	// 특정 분류만 표시하는 목록으로 전환
	bShowAllCategories = false;

	// 프로그램에서 분류를 변경한 경우에도 선택 스타일 동기화
	RefreshCategorySelection();

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

	// 현재 크레딧 헤더 갱신
	RefreshCredits();
	// 품목 목록과 상세 패널 갱신
	RebuildItemList();
}

void UMD_ShopWidget::RefreshCredits()
{
	// 현재 레이아웃에 배치된 크레딧 문구 확인
	if (IsValid(Txt_Credits) == true)
	{
		// 숫자 형식의 현재 크레딧 반영
		Txt_Credits->SetText(FText::AsNumber(ShopInfo.Credits));
	}
}

void UMD_ShopWidget::HandleHotbarChanged()
{
	// 구독 해제 중 전달된 비활성 화면 갱신 방지
	if (IsActivated() == false)
	{
		// 다음 활성화의 전체 갱신까지 처리 보류
		return;
	}
	// 소지품 구성에 따라 판매 목록이 달라지는 경우 확인
	if (bSellingMode == true)
	{
		// 소진되거나 새로 소지한 판매 품목 반영
		RebuildItemList();
	}
	else
	{
		// 구매 목록을 유지하면서 지급 가능 상태만 갱신
		RefreshSelectedItem();
	}
}

void UMD_ShopWidget::RebuildItemList()
{
	// 카드 컨테이너가 없으면 목록 생성 중단
	if (IsValid(WB_ItemEntries) == false)
	{
		// 표시할 컨테이너가 없는 갱신 종료
		return;
	}
	// 현재 로드 상태와 카테고리를 반영할 표시용 품목 목록
	TArray<FMD_ShopItem> DisplayItems;
	// 카탈로그와 카드 클래스가 준비된 경우에만 정상 품목 수집
	if (IsValid(Catalog) == true && ItemCardClass != nullptr)
	{
		// 품목마다 전체 카탈로그를 재검색하지 않는 일괄 준비 검사
		DisplayItems = bSellingMode == true ? Catalog->Items : Catalog->GetAvailableItems();
		// 판매 목록의 중복 품목을 반복 검색 없이 제외할 횟수 집계
		TMap<FPrimaryAssetId, int32> SaleItemCounts;
		// 구매 목록은 카탈로그의 일괄 검사 결과 재사용
		if (bSellingMode == true)
		{
			// 전체 품목에 맞춰 중복 검사 저장 공간 확보
			SaleItemCounts.Reserve(DisplayItems.Num());
			// 필터 적용 전 다른 분류의 중복 행까지 포함한 집계
			for (FMD_ShopItem& Item : DisplayItems)
			{
				// 같은 전체 식별자의 등록 횟수 누적
				++SaleItemCounts.FindOrAdd(Item.GetItemAssetId());
			}
		}
		// 정상 품목 중 현재 분류 밖의 항목만 제외
		DisplayItems.RemoveAll([this, &SaleItemCounts](FMD_ShopItem& Item)
		{
			// 구매와 판매 목록에 동일한 선택 카테고리 적용
			if (bShowAllCategories == false && Item.Category != SelectedCategory)
			{
				// 선택 분류에 속하지 않는 품목 제외
				return true;
			}
			// 판매 목록에서는 구매 해금 대신 실제 소지 수량과 판매 단가 검사
			if (bSellingMode == true)
			{
				// 유일하게 등록되고 실제 소지한 판매 가능 품목만 표시
				return Item.GetItemAssetId().IsValid() == false || SaleItemCounts.FindChecked(Item.GetItemAssetId()) != 1
					|| Item.GetSalePrice() < 0 || FindSaleSlot(Item.GetItemAssetId()) == INDEX_NONE;
			}
			// 선택 분류와 표시 조건을 만족한 품목 유지
			return false;
		});
	}
	// 그룹 또는 표시 개수가 달라진 경우에만 카드 재구성 필요
	bool bRebuildCards = IsValid(ItemCardButtonGroup) == false || WB_ItemEntries->GetChildrenCount() != DisplayItems.Num();
	// 같은 개수에서도 순서나 카드 종류가 달라진 경우 감지
	for (int32 Index = 0; bRebuildCards == false && Index < DisplayItems.Num(); ++Index)
	{
		// 기존 위치의 실제 표시 카드 조회
		UMD_ShopItemCardWidget* ItemCard = Cast<UMD_ShopItemCardWidget>(WB_ItemEntries->GetChildAt(Index));
		// 같은 전체 AssetId와 클래스의 카드만 재사용 허용
		bRebuildCards = IsValid(ItemCard) == false || ItemCard->GetClass() != ItemCardClass.Get()
			|| ItemCard->GetItem().GetItemAssetId() != DisplayItems[Index].GetItemAssetId();
	}
	// 표시 구성이 바뀐 경우에만 기존 카드와 선택 연결 재구성
	if (bRebuildCards == true)
	{
		// 새 화면에서 사용할 선택 필수 그룹 준비
		if (IsValid(ItemCardButtonGroup) == false)
		{
			// 카드 간 단일 선택을 관리할 그룹 생성
			ItemCardButtonGroup = NewObject<UCommonButtonGroupBase>(this);
		}
		// 제거할 카드의 선택 이벤트와 필수 선택 상태 정리
		ItemCardButtonGroup->RemoveAll();
		// 표시 품목이 있으면 한 카드의 선택 유지
		ItemCardButtonGroup->SetSelectionRequired(true);
		// 변경된 목록에 속하지 않는 이전 카드 제거
		WB_ItemEntries->ClearChildren();
	}
	// 이전 상세 품목의 카드 선택 위치
	int32 SelectedItemCardIndex = INDEX_NONE;
	// 기존 선택이 사라진 경우의 첫 정상 품목
	FPrimaryAssetId FirstDisplayedItemAssetId;
	// 표시 순서에 따라 새 카드 생성 또는 기존 카드 갱신
	for (int32 Index = 0; Index < DisplayItems.Num(); ++Index)
	{
		// 재사용할 카드는 앞선 구성 검사에서 유효성 확인 완료
		UMD_ShopItemCardWidget* ItemCard = bRebuildCards == true
			? CreateWidget<UMD_ShopItemCardWidget>(this, ItemCardClass) : Cast<UMD_ShopItemCardWidget>(WB_ItemEntries->GetChildAt(Index));
		// 생성에 실패한 개별 카드만 건너뛰기
		if (IsValid(ItemCard) == false)
		{
			// 나머지 정상 품목의 표시 계속 진행
			continue;
		}
		// 카탈로그 원본 대신 표시용 복사본의 가격 변경
		FMD_ShopItem& Item = DisplayItems[Index];
		// 현재 거래 방향에 따라 무료 재구매 가격 또는 개별 판매 단가 반영
		Item.Price = bSellingMode == true ? Item.GetSalePrice() : Item.GetPurchasePrice(OwnedItemIds);
		// 판매 단가는 묶음과 무관한 개당 금액으로 표시
		if (bSellingMode == true)
		{
			// 카드의 가격 표시를 개별 수량 단위로 전환
			Item.bPricePerUnit = true;
		}
		// 같은 식별자의 이름과 아이콘 변경도 현재 정의에서 갱신
		ItemCard->SetItem(Item);
		// 신규 카드에만 선택 그룹과 클릭 이벤트 연결
		if (bRebuildCards == true)
		{
			// 카드의 선택 상태 사용 허용
			ItemCard->SetIsSelectable(true);
			// 새 카드의 단일 선택 그룹 등록
			ItemCardButtonGroup->AddWidget(ItemCard);
			// 카드의 실제 품목을 사용하는 클릭 이벤트 연결
			ItemCard->OnClicked().AddUObject(this, &UMD_ShopWidget::HandleItemSelected, Item.GetItemAssetId());
			// 생성 실패로 인한 빈 칸을 방지할 실제 카드 순번
			int32 CardIndex = WB_ItemEntries->GetChildrenCount();
			// 카드 자체의 크기로 가로 배치할 줄바꿈 슬롯 생성
			UWrapBoxSlot* ItemSlot = WB_ItemEntries->AddChildToWrapBox(ItemCard);
			// 생성된 슬롯에서 카드 자체의 크기 제한 존중
			if (IsValid(ItemSlot) == true)
			{
				// 가로 공간이 넓어도 네 카드마다 다음 줄로 이동
				ItemSlot->SetNewLine(CardIndex > 0 && CardIndex % 4 == 0);
				// CommonButtonBase의 Min/Max 폭을 셀 너비로 늘리지 않는 좌측 정렬
				ItemSlot->SetHorizontalAlignment(HAlign_Left);
				// CommonButtonBase의 Min/Max 높이를 행 높이로 늘리지 않는 상단 정렬
				ItemSlot->SetVerticalAlignment(VAlign_Top);
			}
		}
		// 실제 생성된 첫 카드만 대체 선택 후보로 보관
		if (FirstDisplayedItemAssetId.IsValid() == false)
		{
			// 로드 또는 카드 생성 실패 품목의 기본 선택 방지
			FirstDisplayedItemAssetId = Item.GetItemAssetId();
		}
		// 이전에 선택한 품목이 계속 표시되는 경우 선택 위치 복원
		if (Item.GetItemAssetId() == SelectedItemAssetId)
		{
			// 카드 생성 실패를 고려한 실제 그룹 위치 조회
			SelectedItemCardIndex = ItemCardButtonGroup->FindButtonIndex(ItemCard);
		}
	}
	// 선택했던 품목이 사라진 경우 첫 정상 품목으로 이동
	if (SelectedItemCardIndex == INDEX_NONE)
	{
		// 빈 목록에서는 이전 상세 품목의 식별자 제거
		SelectedItemAssetId = FirstDisplayedItemAssetId;
		// 품목이 있는 경우에만 첫 카드 선택
		SelectedItemCardIndex = FirstDisplayedItemAssetId.IsValid() == true ? 0 : INDEX_NONE;
	}
	// 유지되거나 대체된 정상 품목의 선택 표시 적용
	if (SelectedItemCardIndex != INDEX_NONE)
	{
		// 클릭 이벤트를 재발생시키지 않고 단일 선택 복원
		ItemCardButtonGroup->SelectButtonAtIndex(SelectedItemCardIndex, false);
	}
	// 최신 가격과 소지 상태 및 구매 제한을 상세 패널에 반영
	RefreshSelectedItem();
}

void UMD_ShopWidget::RefreshSelectedItem()
{
	// 현재 목록의 거래 방향을 실제 실행 버튼에 표시
	if (IsValid(Txt_Buy) == true)
	{
		// 하단 실행 버튼에 구매와 판매 동작 표시
		Txt_Buy->SetText(bSellingMode == true ? NSLOCTEXT("MDShop", "SellButton", "판매") : NSLOCTEXT("MDShop", "BuyButton", "구매"));
	}
	// 선택 품목을 카탈로그에서 조회
	FMD_ShopItem selectedItem;
	// 한 번 조회한 판매 항목으로 정의 준비 상태까지 확인
	bool foundItem = IsValid(Catalog) == true && SelectedItemAssetId.IsValid() == true
		&& Catalog->FindItemByAssetId(SelectedItemAssetId, selectedItem) == true
		&& (bSellingMode == true ? selectedItem.GetSalePrice() >= 0 : selectedItem.IsDefinitionLoaded() == true);

	// 여러 표시 요소가 함께 사용할 공통 정의 조회
	UMD_ItemDefinition* Definition = foundItem == true ? selectedItem.GetDefinition() : nullptr;
	// 선택 품목 또는 정의가 없으면 상세 패널 내용 초기화
	if (foundItem == false || IsValid(Definition) == false)
	{
		if (IsValid(Txt_DetailName) == true)
		{
			// 선택 품목이 없는 상태의 상세 이름을 대시로 표시
			Txt_DetailName->SetText(FText::FromString(TEXT("-")));
		}

		if (IsValid(Txt_DetailDescription) == true)
		{
			Txt_DetailDescription->SetText(FText::GetEmpty());
		}

		// 빈 상세 화면에 남은 카테고리 표시 영역 확인
		if (IsValid(Txt_DetailCategory) == true)
		{
			// 선택 품목이 없는 상태의 카테고리를 대시로 표시
			Txt_DetailCategory->SetText(FText::FromString(TEXT("-")));
		}

		if (IsValid(Txt_Owned) == true)
		{
			Txt_Owned->SetText(FText::GetEmpty());
		}

		if (IsValid(Img_DetailIcon) == true)
		{
			Img_DetailIcon->SetBrushFromTexture(nullptr);
			// 선택 품목이 없는 상세 이미지를 완전히 투명하게 표시
			Img_DetailIcon->SetRenderOpacity(0.0f);
		}

		// 선택 품목이 없는 상태에서 거래 실행 버튼 확인
		if (IsValid(Btn_Buy) == true)
		{
			// 목록 전환 탭은 유지하면서 거래 실행만 차단
			Btn_Buy->SetIsEnabled(false);
		}

		return;
	}

	// 카드와 서버가 동일한 규칙을 사용하도록 현재 거래 단가 확정
	selectedItem.Price = bSellingMode == true ? selectedItem.GetSalePrice() : selectedItem.GetPurchasePrice(OwnedItemIds);
	// 판매 상세 가격을 개당 단위로 표시
	if (bSellingMode == true)
	{
		// 묶음 구매가와 개별 판매가의 단위 혼동 방지
		selectedItem.bPricePerUnit = true;
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

	// 선택 품목의 카테고리를 표시할 배지 유효성 확인
	if (IsValid(Txt_DetailCategory) == true)
	{
		// 전체 보기에서도 선택 품목의 실제 카테고리 이름 표시
		Txt_DetailCategory->SetText(StaticEnum<EMD_ShopCategory>()->GetDisplayNameTextByValue(static_cast<int64>(selectedItem.Category)));
	}

	if (IsValid(Txt_Owned) == true)
	{
		// 판매 화면에는 상점 재고 대신 현재 소유 수량 표시
		if (bSellingMode == true)
		{
			// 판매 가능한 현재 수량을 상세 영역에 표시
			Txt_Owned->SetText(FText::Format(NSLOCTEXT("MDShop", "SaleOwnedCount", "보유 수량: {0}개"),
				FText::AsNumber(ObservedHotbar.IsValid() == true ? ObservedHotbar->GetItemCountAtSlot(FindSaleSlot(SelectedItemAssetId)) : 0)));
		}
		// 개당 구매 품목은 구매 이력과 무관하게 무제한 재고 표시
		else if (selectedItem.bPricePerUnit == true)
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
		// 품목 선택 시 투명 상태를 해제하여 상세 아이콘 표시 복원
		Img_DetailIcon->SetRenderOpacity(1.0f);
	}

	// 상점 해금 및 결제 조건과 유형별 정의의 지급 조건을 함께 적용
	if (IsValid(Btn_Buy) == true)
	{
		// 판매는 실제 소지 수량과 적립 한도이며 구매는 기존 해금 및 지급 조건 적용
		Btn_Buy->SetIsEnabled(bSellingMode == true
			? FindSaleSlot(SelectedItemAssetId) != INDEX_NONE && selectedItem.Price >= 0 && ShopInfo.Credits <= MAX_int32 - selectedItem.Price
			: selectedItem.IsUnlocked(OwnedItemIds) == true
				&& selectedItem.Price >= 0 && ShopInfo.Credits >= selectedItem.Price && IsItemGrantable(selectedItem) == true);
	}
}

bool UMD_ShopWidget::IsItemGrantable(FMD_ShopItem& Item)
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
		ObservedHotbar->OnHotbarChanged.RemoveDynamic(this, &UMD_ShopWidget::HandleHotbarChanged);
	}
	// 화면과 수명을 공유하지 않는 약한 보관 참조 갱신
	ObservedHotbar = InHotbar;
	// 새 보관 컴포넌트가 있는 경우에만 변경 알림 연결
	if (IsValid(InHotbar) == true)
	{
		// 서버 지급과 클라이언트 복제로 선행 도구 조건이 바뀌면 즉시 재검사
		InHotbar->OnHotbarChanged.AddUniqueDynamic(this, &UMD_ShopWidget::HandleHotbarChanged);
	}
}

void UMD_ShopWidget::InitializeCategoryButtons()
{
	// 배치된 버튼의 단일 선택을 관리할 그룹 준비
	if (IsValid(CategoryButtonGroup) == false)
	{
		// 화면 수명 동안 사용할 선택 그룹 생성
		CategoryButtonGroup = NewObject<UCommonButtonGroupBase>(this);
	}
	// 재활성화 시 같은 버튼의 그룹 이벤트 중복 등록 방지
	CategoryButtonGroup->RemoveAll();
	// 현재 필터를 표시할 카테고리 하나의 선택 유지
	CategoryButtonGroup->SetSelectionRequired(true);
	// 이벤트 그래프의 클릭 연결을 유지할 디자이너 버튼 목록
	TArray<UMD_CommonButtonBase*> CategoryButtons = {
		Btn_Category_All, Btn_Category_Tool, Btn_Category_Safety, Btn_Category_Repair, Btn_Category_Survive, Btn_Category_Recovery
	};
	// 새 버튼 생성 없이 기존 버튼의 선택 정책만 적용
	for (UMD_CommonButtonBase* CategoryButton : CategoryButtons)
	{
		// 현재 레이아웃에 배치된 유효한 버튼만 등록
		if (IsValid(CategoryButton) == true)
		{
			// 분류 선택에 대응하는 버튼 스타일 사용 허용
			CategoryButton->SetIsSelectable(true);
			// 현재 분류 재클릭에 따른 선택 해제 방지
			CategoryButton->SetIsToggleable(false);
			// 포커스 이동과 실제 분류 선택의 표시 구분 유지
			CategoryButton->SetShouldSelectUponReceivingFocus(false);
			// 클릭의 필터 처리는 Blueprint에 두고 단일 선택 그룹만 연결
			CategoryButtonGroup->AddWidget(CategoryButton);
		}
	}
	// 이전 활성화에서 선택한 필터의 표시 복원
	RefreshCategorySelection();
}

void UMD_ShopWidget::RefreshCategorySelection()
{
	// 화면 배치 순서와 독립적으로 Enum에 대응할 버튼 목록
	TArray<UMD_CommonButtonBase*> CategoryButtons = {
		Btn_Category_All, Btn_Category_Tool, Btn_Category_Safety, Btn_Category_Repair, Btn_Category_Survive, Btn_Category_Recovery
	};
	// 전체 보기 또는 지정한 Enum 값에 대응하는 버튼 위치 산출
	int32 CategoryIndex = bShowAllCategories == true ? 0 : StaticEnum<EMD_ShopCategory>()->GetIndexByValue(static_cast<int64>(SelectedCategory)) + 1;
	// 일부 버튼이 없는 레이아웃에서도 현재 버튼의 실제 그룹 위치 확인
	if (IsValid(CategoryButtonGroup) == true && CategoryButtons.IsValidIndex(CategoryIndex) == true && IsValid(CategoryButtons[CategoryIndex]) == true)
	{
		// 버튼 배치 순서 대신 실제 버튼 참조를 기준으로 현재 필터 선택
		CategoryButtonGroup->SelectButtonAtIndex(CategoryButtonGroup->FindButtonIndex(CategoryButtons[CategoryIndex]), false);
	}
}

void UMD_ShopWidget::SelectAllCategories()
{
	// 전체 보기 재클릭 시 현재 품목 선택 유지
	if (bShowAllCategories == true)
	{
		// 변경 없는 목록 재구성 생략
		return;
	}
	// 모든 아이템 카테고리를 함께 표시하도록 상태 변경
	bShowAllCategories = true;
	// 전체 보기로 전환된 분류 선택 표시 반영
	RefreshCategorySelection();
	// 전체 목록의 첫 품목으로 선택 초기화
	SelectedItemAssetId = FPrimaryAssetId();
	// 해제된 필터를 목록과 상세 패널에 반영
	RebuildItemList();
}

void UMD_ShopWidget::HandleItemSelected(FPrimaryAssetId ItemAssetId)
{
	// 이미 선택한 품목의 반복 상세 조회 방지
	if (SelectedItemAssetId == ItemAssetId)
	{
		// 현재 상세 표시 유지
		return;
	}
	// 선택한 카드의 품목을 상세 패널 대상으로 저장
	SelectedItemAssetId = ItemAssetId;

	// 선택 품목 상세 정보 갱신
	RefreshSelectedItem();
}

void UMD_ShopWidget::HandleBuyClicked()
{
	// 이벤트 그래프의 직접 호출도 활성 화면에서만 허용
	if (IsActivated() == false)
	{
		// 숨겨진 화면의 거래 요청 차단
		return;
	}
	// 판매 목록의 실행 버튼은 현재 보유 품목 한 개 회수 요청
	if (bSellingMode == true)
	{
		// 실제 소지 상태를 재검사하는 공통 BP 판매 진입점 호출
		RequestSelectedItemSale();
		// 같은 클릭으로 구매까지 진행되는 상황 방지
		return;
	}
	// 한 번 조회한 판매 정보를 클릭 직전 검증에도 재사용
	FMD_ShopItem SelectedItem;
	// 유효한 선택과 유일한 판매 항목 및 준비된 정의를 함께 검사
	if (SelectedItemAssetId.IsValid() == false || IsValid(Catalog) == false
		|| Catalog->FindItemByAssetId(SelectedItemAssetId, SelectedItem) == false || SelectedItem.IsDefinitionLoaded() == false)
	{
		// 구매할 수 없는 개별 품목의 요청 종료
		return;
	}

	// 버튼을 거치지 않는 BP 호출에서도 현재 구매 단가 적용
	SelectedItem.Price = SelectedItem.GetPurchasePrice(OwnedItemIds);
	// 비활성 구매 버튼과 같은 가격 및 해금 조건 검사
	if (SelectedItem.Price < 0 || ShopInfo.Credits < SelectedItem.Price
		|| SelectedItem.IsUnlocked(OwnedItemIds) == false)
	{
		// 현재 결제 조건을 충족하지 않은 요청 차단
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

void UMD_ShopWidget::SetSellingMode(bool bInSellingMode)
{
	// 같은 모드 재클릭과 BP 중복 호출의 목록 및 애니메이션 갱신 방지
	if (bSellingMode == bInSellingMode)
	{
		// 현재 품목 선택 유지
		return;
	}
	// 현재 목록의 거래 방향 저장
	bSellingMode = bInSellingMode;
	// 새 목록에서 첫 보유 품목을 선택하도록 이전 선택 해제
	SelectedItemAssetId = FPrimaryAssetId();
	// 소지 상태와 카드 및 상세 화면을 함께 갱신
	RefreshDisplay();
	// 입력 방식과 관계없이 실제 상태 변경에 맞춘 BP 연출 호출
	BP_OnSellingModeChanged(bSellingMode);
}

int32 UMD_ShopWidget::FindSaleSlot(FPrimaryAssetId ItemAssetId)
{
	// 소유 Pawn의 보관 상태를 구독하는 활성 화면만 판매 허용
	if (ObservedHotbar.IsValid() == false || ItemAssetId.IsValid() == false)
	{
		// 유효한 판매 위치 부재 전달
		return INDEX_NONE;
	}
	// 동일 AssetId가 하나의 슬롯에만 존재하는 공통 인벤토리 탐색
	for (int32 SlotIndex = 0; SlotIndex < ObservedHotbar->GetSlotCount(); ++SlotIndex)
	{
		// 현재 위치의 실제 아이템 조회
		UMD_ItemDefinition* Item = ObservedHotbar->GetItemAtSlot(SlotIndex);
		// 남은 수량과 전체 식별자가 일치하는 소지 슬롯 선택
		if (IsValid(Item) == true && Item->GetPrimaryAssetId() == ItemAssetId && ObservedHotbar->GetItemCountAtSlot(SlotIndex) > 0)
		{
			// 서버에서 다시 검증할 예상 위치 전달
			return SlotIndex;
		}
	}
	// 판매 가능한 소지품 부재 전달
	return INDEX_NONE;
}

void UMD_ShopWidget::RequestSelectedItemSale(int32 Quantity)
{
	// 숨겨진 화면과 잘못된 수량 요청 차단
	if (IsActivated() == false || bSellingMode == false || Quantity <= 0)
	{
		// BP 알림을 포함한 잘못된 요청 결과 전달
		ReceiveSaleResult(EMD_ShopSaleResult::InvalidRequest);
		// 판매 요청 진행 종료
		return;
	}
	// 클릭 직전의 단가를 검증할 원본 카탈로그 항목
	FMD_ShopItem Item;
	// 무효 품목과 판매 불가 유형 검사
	if (IsValid(Catalog) == false || Catalog->FindItemByAssetId(SelectedItemAssetId, Item) == false || Item.GetSalePrice() < 0)
	{
		// 현재 품목의 거래 불가 알림
		ReceiveSaleResult(EMD_ShopSaleResult::NotSellable);
		// 소지 수량을 변경하지 않은 요청 종료
		return;
	}
	// 드래그 교환이나 소비 이후의 최신 보관 위치 조회
	int32 SlotIndex = FindSaleSlot(SelectedItemAssetId);
	// 예상 수량보다 적은 소지품의 요청 차단
	if (SlotIndex == INDEX_NONE || ObservedHotbar->GetItemCountAtSlot(SlotIndex) < Quantity)
	{
		// 복제된 실제 수량 기준 판매 불가 알림
		ReceiveSaleResult(EMD_ShopSaleResult::NotOwned);
		// 잘못된 판매 요청 종료
		return;
	}
	// 상인에게 단가 없이 전체 식별자와 위치 및 수량만 전달
	OnItemSaleRequested.Broadcast(SelectedItemAssetId, SlotIndex, Quantity);
}

void UMD_ShopWidget::ReceiveSaleResult(EMD_ShopSaleResult Result)
{
	// 최신 복제 상태로 소진된 품목과 남은 수량 반영
	RefreshDisplay();
	// 판매 성공 및 실패 이유를 BP 사운드와 알림에 전달
	OnSaleCompleted.Broadcast(Result);
}
