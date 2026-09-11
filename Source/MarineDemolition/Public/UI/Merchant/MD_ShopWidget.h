// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

// 둘 다 값으로 쓰는 타입이라 전방 선언이 안 된다 (EMD_ShopCategory, FMD_ShopItem,
// FMD_MerchantMenuInfo). 대신 위젯 헤더가 아닌 타입 전용 헤더만 끌어온다.
#include "Data/Shop/MD_ShopCatalog.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "UI/Merchant/MD_MerchantUITypes.h"
#include "MD_ShopWidget.generated.h"

class UImage;
class UCommonButtonGroupBase;
class UPanelWidget;
class UTextBlock;
class UMD_CommonButtonBase;
class UMD_MerchantMenuWidget;
class UMD_ShopItemCardWidget;
class UMD_EquipHotbarComponent;

// 선택한 전체 AssetId를 구매 요청 수신자에 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_ShopPurchaseRequest, FPrimaryAssetId, ItemAssetId);
// 품목 식별자를 사용하지 않는 구매 제한 알림
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_ShopPurchaseRejected);

/**
 * 상점 UI. 5탭(`04` §3 = `08` §1) + 아이템 카드 목록.
 *
 * 카탈로그(가격·해금)는 UMD_ShopCatalog 에셋에서 읽는다 — `07` §4가 밸런스 문서라
 * 자주 바뀌므로 C++에 박지 않는다.
 *
 * 구매 처리는 여기서 하지 않는다. 경제·인벤 런타임 시스템이 아직 없어서
 * OnItemPurchaseRequested만 브로드캐스트하고, 시스템이 붙으면 거기서 받는다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_ShopWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget);

	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetCatalog(UMD_ShopCatalog* InCatalog);

	/** 크레딧·호감도 등 헤더 정보. 메뉴 위젯과 같은 구조체를 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetShopInfo(const FMD_MerchantMenuInfo& InInfo);

	// 진행 조건과 구매 이력 및 등록된 구매 금지를 상점 화면에 전달
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetProgress(int32 InQuestCompletions, const TArray<FPrimaryAssetId>& InOwnedItemIds, TArray<FPrimaryAssetId> InBlockedItemIds);

	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SelectCategory(EMD_ShopCategory InCategory);

	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void RefreshDisplay();

	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_ShopPurchaseRequest OnItemPurchaseRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_ShopPurchaseRejected OnPurchaseRejected;

protected:
	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;

	virtual void NativeDestruct() override;

	void RebuildItemList();

	void BuildCategoryButtons();

	void RefreshSelectedItem();

	// 판매 정보 복사 없이 실제 소유 Pawn과 구매 이력으로 지급 조건 검사
	bool IsItemGrantable(FMD_ShopItem& Item);

	// 선행 도구와 잔여 수량 변경을 화면에 반영할 Hotbar 구독 관리
	void ObserveHotbar(UMD_EquipHotbarComponent* InHotbar);

	void HandleCategoryAllClicked();

	void HandleCategoryClicked(EMD_ShopCategory Category);

	// 선택된 카드의 전체 식별자로 상세 품목 갱신
	void HandleItemSelected(FPrimaryAssetId ItemAssetId);

	void HandleBuyClicked();

	// 상점 화면을 연 상인 메뉴
	UPROPERTY()
	TObjectPtr<UMD_MerchantMenuWidget> OwningMerchantMenu;

	/** 카탈로그 에셋. WBP 기본값으로 지정해도 되고 런타임에 SetCatalog로 넣어도 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Shop")
	TObjectPtr<UMD_ShopCatalog> Catalog;

	// 이 화면이 사용하는 판매 정의의 비동기 로드 및 보관 핸들
	TSharedPtr<FStreamableHandle> DefinitionLoadHandle;

	/** 아이템 카드로 쓸 WBP */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Shop")
	TSubclassOf<UMD_ShopItemCardWidget> ItemCardClass;

	// 동적으로 생성할 카테고리 버튼 클래스
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Shop")
	TSubclassOf<UMD_CommonButtonBase> CategoryButtonClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	FMD_MerchantMenuInfo ShopInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop", meta = (ClampMin = "0"))
	int32 QuestCompletions = 0;

	// 현재 플레이어의 전체 AssetId 구매 이력
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	TArray<FPrimaryAssetId> OwnedItemIds;

	// 소유 플레이어에게 복제된 업그레이드 소스 구매 금지 목록
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop")
	TArray<FPrimaryAssetId> BlockedItemIds;

	// 화면에 연결된 보관 상태의 변경 알림 해제에 사용할 약한 참조
	TWeakObjectPtr<UMD_EquipHotbarComponent> ObservedHotbar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	EMD_ShopCategory SelectedCategory = EMD_ShopCategory::Tool;

	// 모든 아이템 카테고리를 함께 표시하는 상태
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	bool bShowAllCategories = true;

	// 현재 상세 패널에 표시 중인 품목의 전체 AssetId
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop")
	FPrimaryAssetId SelectedItemAssetId;

	// 동적으로 생성한 상점 카드 중 항상 하나만 선택되도록 관리하는 그룹
	UPROPERTY(Transient)
	TObjectPtr<UCommonButtonGroupBase> ItemCardButtonGroup;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Buy;

	/*
	 * 카드가 들어갈 컨테이너. ScrollBox·WrapBox·VerticalBox 아무거나 가능하도록
	 * UPanelWidget으로 받는다 — 레이아웃 선택을 디자이너에게 남긴다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SB_Categories;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> WB_ItemEntries;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credits;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailName;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailDescription;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_DetailPrice;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Owned;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_DetailIcon;
};
