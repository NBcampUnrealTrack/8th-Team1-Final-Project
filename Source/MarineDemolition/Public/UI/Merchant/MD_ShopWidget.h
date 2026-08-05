// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

// 둘 다 값으로 쓰는 타입이라 전방 선언이 안 된다 (EMD_ShopCategory, FMD_ShopItem,
// FMD_MerchantMenuInfo). 대신 위젯 헤더가 아닌 타입 전용 헤더만 끌어온다.
#include "Data/Shop/MD_ShopCatalog.h"
#include "UI/Merchant/MD_MerchantTypes.h"

#include "MD_ShopWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UMD_ShopItemCardWidget;
class UMD_MerchantMenuWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_ShopBackRequest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_ShopPurchaseRequest, FName, ItemId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_ShopPurchaseRejected, FName, ItemId);

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
class MARINEDEMOLITION_API UMD_ShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget) { OwningMerchantMenu = InMenuWidget; }

	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetCatalog(UMD_ShopCatalog* InCatalog);

	/** 크레딧·호감도 등 헤더 정보. 메뉴 위젯과 같은 구조체를 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetShopInfo(const FMD_MerchantMenuInfo& InInfo);

	/** 해금 판정에 필요한 진행 상태. 아직 저장 시스템이 없어 외부에서 밀어 넣는다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetProgress(int32 InQuestCompletions, const TArray<FName>& InOwnedItemIds);

	/**
	 * 더 살 필요가 없는 품목(호환 장비가 이미 가득 찬 소모품 등)을 지정한다.
	 *
	 * "가득 찼는지"를 판단하는 규칙은 UI가 알 바가 아니다 — 장비·인벤 시스템이
	 * 계산해서 결과만 넘긴다. 그래야 폼건 카트리지든 열선 배터리든 규칙이 바뀌어도
	 * 이 위젯은 그대로 쓸 수 있다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetFullyStockedItems(const TArray<FName>& InItemIds);

	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SelectCategory(EMD_ShopCategory InCategory);

	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	EMD_ShopCategory GetSelectedCategory() const { return SelectedCategory; }

	/** 헤더 정보 + 아이템 목록을 모두 다시 그린다. */
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void RefreshDisplay();

	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_ShopBackRequest OnBackRequested;

	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_ShopPurchaseRequest OnItemPurchaseRequested;

	/** 이미 가득 차서 구매를 막았을 때. 상인이 받아 대사로 알린다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_ShopPurchaseRejected OnPurchaseRejected;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	void RebuildItemList();

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleCategoryToolClicked();

	UFUNCTION()
	void HandleCategorySafetyClicked();

	UFUNCTION()
	void HandleCategoryUtilityRepairClicked();

	UFUNCTION()
	void HandleCategorySurvivalClicked();

	UFUNCTION()
	void HandleCategoryRecoveryClicked();

	UFUNCTION()
	void HandleCardBuyRequested(FName ItemId);

	UPROPERTY()
	TObjectPtr<UMD_MerchantMenuWidget> OwningMerchantMenu;

	/** 카탈로그 에셋. WBP 기본값으로 지정해도 되고 런타임에 SetCatalog로 넣어도 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Shop")
	TObjectPtr<UMD_ShopCatalog> Catalog;

	/** 아이템 카드로 쓸 WBP */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Shop")
	TSubclassOf<UMD_ShopItemCardWidget> ItemCardClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	FMD_MerchantMenuInfo ShopInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop", meta = (ClampMin = "0"))
	int32 QuestCompletions = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	TArray<FName> OwnedItemIds;

	/** 이미 가득 차서 구매를 막을 품목. SetFullyStockedItems()로 채운다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	TArray<FName> FullyStockedItemIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	EMD_ShopCategory SelectedCategory = EMD_ShopCategory::Tool;

	// ───────────────── 카테고리 탭 5종 ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_CatTool;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_CatSafety;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_CatUtilityRepair;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_CatSurvival;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_CatRecovery;

	/** 선택된 카테고리 이름 (enum DisplayName을 그대로 쓴다) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_CategoryTitle;

	// ───────────────── 아이템 목록 ─────────────────

	/**
	 * 카드가 들어갈 컨테이너. ScrollBox·WrapBox·VerticalBox 아무거나 가능하도록
	 * UPanelWidget으로 받는다 — 레이아웃 선택을 디자이너에게 남긴다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PNL_ItemList;

	/** 해당 카테고리에 품목이 없을 때 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_EmptyCategory;

	// ───────────────── 헤더 정보 (`07` §8) ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Credits;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_DailyAdvance;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Affinity;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_AffinityToNextUnlock;

	/** 활성 퀘스트 보수 미리보기 (`07` §8) */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ActiveQuestReward;

	// ───────────────── 안내 문구 ─────────────────

	/** `04` §3: 쓰레기통은 상점 비매품 — 폐기물 대기장 무한 보급 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_TrashBinNotice;

	/** `07` §8: 소모품 권장 일일 구매량 힌트 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ConsumableHint;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Back;
};
