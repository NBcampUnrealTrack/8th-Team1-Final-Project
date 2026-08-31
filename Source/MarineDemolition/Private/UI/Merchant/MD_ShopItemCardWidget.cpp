// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopItemCardWidget.h"

#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UMD_ShopItemCardWidget::SetItem(const FMD_ShopItem& InItem, bool bInOwned)
{
	// 전달받은 품목을 카드 데이터로 저장
	Item = InItem;

	// 품목의 지급 완료 상태 저장
	bOwned = bInOwned;

	// 변경된 품목을 카드 UI에 반영
	RefreshDisplay();
}

void UMD_ShopItemCardWidget::RefreshDisplay()
{
	// 공통 버튼의 기본 텍스트에 품목 이름 적용
	SetContentText(Item.DisplayName);

	// 카드 하단에 품목 가격 적용
	if (IsValid(Txt_CostCredit) == true)
	{
		FText priceText = bOwned == true ? FText::FromString(TEXT("-")) : Item.GetPriceText();
		Txt_CostCredit->SetText(priceText);
	}

	// 품목 아이콘을 동기 로드해 공통 버튼 이미지에 적용
	UTexture2D* iconTexture = Item.Icon.LoadSynchronous();
	if (IsValid(iconTexture) == true)
	{
		SetContentImageResourceObject(iconTexture);
	}
}
