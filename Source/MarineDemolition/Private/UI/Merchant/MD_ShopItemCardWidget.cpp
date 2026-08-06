// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopItemCardWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UMD_ShopItemCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(BTN_Buy))
	{
		BTN_Buy->OnClicked.AddDynamic(this, &UMD_ShopItemCardWidget::HandleBuyClicked);
		BTN_Buy->OnHovered.AddDynamic(this, &UMD_ShopItemCardWidget::HandleBuyHovered);
		BTN_Buy->OnUnhovered.AddDynamic(this, &UMD_ShopItemCardWidget::HandleBuyUnhovered);
	}

	RefreshDisplay();
}

void UMD_ShopItemCardWidget::SetItem(const FMD_ShopItem& InItem, bool bInUnlocked, bool bInAffordable)
{
	Item = InItem;
	bUnlocked = bInUnlocked;
	bAffordable = bInAffordable;

	RefreshDisplay();
}

void UMD_ShopItemCardWidget::RefreshDisplay()
{
	if (IsValid(TXT_ItemName))
	{
		TXT_ItemName->SetText(Item.DisplayName);
	}
	if (IsValid(TXT_ItemId))
	{
		TXT_ItemId->SetText(FText::FromName(Item.ItemId));
	}
	if (IsValid(TXT_Price))
	{
		TXT_Price->SetText(Item.GetPriceText());
	}
	if (IsValid(TXT_Description))
	{
		TXT_Description->SetText(Item.Description);
	}
	if (IsValid(TXT_UnlockState))
	{
		TXT_UnlockState->SetText(FText::FromString(bUnlocked ? TEXT("해금") : TEXT("잠김")));
	}
	if (IsValid(TXT_UnlockCondition))
	{
		// 해금 조건은 잠긴 상태에서도 계속 보여준다 — `04` §3 UX 포인트가
		// "잠긴 아이템도 다음에 뭐가 열리는지 보이게"를 요구한다.
		TXT_UnlockCondition->SetText(Item.GetUnlockConditionText());
	}

	if (IsValid(IMG_Icon))
	{
		if (UTexture2D* IconTexture = Item.Icon.LoadSynchronous())
		{
			IMG_Icon->SetBrushFromTexture(IconTexture);
		}
	}

	if (IsValid(BDR_LockOverlay))
	{
		BDR_LockOverlay->SetVisibility(bUnlocked ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (IsValid(BTN_Buy))
	{
		// 지급 품목(가격 0)은 구매 대상이 아니다.
		const bool bPurchasable = bUnlocked && bAffordable && Item.Price > 0;
		BTN_Buy->SetIsEnabled(bPurchasable);
	}

	// 호버 라벨은 기본 숨김. 마우스를 올릴 때만 가격과 자리를 바꾼다.
	if (IsValid(TXT_BuyLabel))
	{
		TXT_BuyLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_Price))
	{
		TXT_Price->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UMD_ShopItemCardWidget::HandleBuyClicked()
{
	OnBuyRequested.Broadcast(Item.ItemId);
}

void UMD_ShopItemCardWidget::HandleBuyHovered()
{
	// 살 수 없는 품목은 호버해도 `구매`로 바뀌지 않는다 — 누를 수 있다는
	// 잘못된 신호를 주지 않기 위해서다. 잠김 사유는 카드에 이미 적혀 있다.
	if (!IsValid(BTN_Buy) || !BTN_Buy->GetIsEnabled())
	{
		return;
	}

	if (IsValid(TXT_Price))
	{
		TXT_Price->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_BuyLabel))
	{
		TXT_BuyLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UMD_ShopItemCardWidget::HandleBuyUnhovered()
{
	if (IsValid(TXT_BuyLabel))
	{
		TXT_BuyLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(TXT_Price))
	{
		TXT_Price->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}
