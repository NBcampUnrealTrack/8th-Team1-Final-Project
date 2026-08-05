// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "MD_ShopItemCardWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_ShopItemBuyRequest, FName, ItemId);

/**
 * 상점 아이템 카드 1장.
 *
 * `04` §3 "아이템 카드에 반드시 넣을 것" 5개 항목이 그대로 위젯으로 노출돼 있다:
 * 이름 · 가격(₡) · 한 줄 기능 설명 · 해금 상태 · 잠김 조건.
 * 전부 BindWidgetOptional이라 WBP에서 필요한 것만 배치해도 컴파일이 깨지지 않는다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_ShopItemCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 카드에 표시할 품목과 판정 결과를 넣는다.
	 * @param bInUnlocked      해금 여부. 판정은 FMD_ShopItem::IsUnlocked()가 담당한다.
	 * @param bInAffordable    보유 크레딧으로 살 수 있는지. 해금돼 있어도 돈이 모자라면 구매 불가.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void SetItem(const FMD_ShopItem& InItem, bool bInUnlocked, bool bInAffordable);

	UFUNCTION(BlueprintPure, Category = "MD|Shop")
	const FMD_ShopItem& GetItem() const { return Item; }

	UFUNCTION(BlueprintCallable, Category = "MD|Shop")
	void RefreshDisplay();

	UPROPERTY(BlueprintAssignable, Category = "MD|Shop")
	FMD_ShopItemBuyRequest OnBuyRequested;

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void HandleBuyClicked();

	/**
	 * 가격 버튼에 마우스를 올리면 가격 대신 `구매`를 보여준다.
	 *
	 * 가격 표시와 구매 버튼이 같은 자리를 쓰는 구도라, 평소에는 정보(가격)를 보여주고
	 * 누를 의도가 생긴 순간에만 행동(구매)으로 바뀐다. 두 라벨의 텍스트를 갈아끼우지 않고
	 * 가시성만 토글하는 이유: 가격 문자열을 잃지 않고, 디자이너가 각각 다르게 꾸밀 수 있다.
	 */
	UFUNCTION()
	void HandleBuyHovered();

	UFUNCTION()
	void HandleBuyUnhovered();

	/** 디자이너가 에디터 미리보기에서 값을 넣어볼 수 있도록 노출한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	FMD_ShopItem Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	bool bUnlocked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Shop")
	bool bAffordable = true;

	// ───────────────── `04` §3 아이템 카드 필수 5종 ─────────────────

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ItemName;

	/** `450₡` / `35₡/개` / `지급` */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Price;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_Description;

	/** `해금` / `잠김` */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_UnlockState;

	/** `호감 ≥0 · 퀘스트 1회` — 필드에서 생성된 문구 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_UnlockCondition;

	// ───────────────── 부가 표시 ─────────────────

	/** `07` §4의 ID. 디버깅·기획서 대조용. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_ItemId;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IMG_Icon;

	/** 가격 자리를 겸하는 구매 버튼. 안에 TXT_Price와 TXT_BuyLabel을 함께 둔다. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BTN_Buy;

	/** 호버 시 가격 대신 나오는 라벨 (`구매`). 평소에는 숨는다. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TXT_BuyLabel;

	/** 잠긴 카드 위에 덮는 연출용. 카드 자체를 숨기지는 않는다 (`04` §3 UX 포인트). */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Shop", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BDR_LockOverlay;
};
