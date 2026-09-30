// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopItemCardWidget.h"

#include "Components/TextBlock.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "Engine/Texture2D.h"

void UMD_ShopItemCardWidget::SetItem(const FMD_ShopItem& InItem)
{
	// 전달받은 품목을 카드 데이터로 저장
	Item = InItem;

	// 변경된 품목을 카드 UI에 반영
	RefreshDisplay();
}

void UMD_ShopItemCardWidget::RefreshDisplay()
{
	// 이름과 아이콘을 같은 공통 정의에서 조회
	UMD_ItemDefinition* Definition = Item.GetDefinition();
	// 로드되지 않은 품목은 이전 카드 이름을 비운 상태로 표시
	SetContentText(IsValid(Definition) == true ? Definition->ItemName : FText::GetEmpty());

	// 카드 하단에 품목 가격 적용
	if (IsValid(Txt_CostCredit) == true)
	{
		// 별도 크레딧 기호와 중복되지 않도록 가격 숫자만 표시
		Txt_CostCredit->SetText(FText::AsNumber(Item.Price));
	}

	// 가격 숫자 옆 크레딧 단위 영역 유효성 확인
	if (IsValid(Txt_Credit_Suffix) == true)
	{
		// 일반 품목과 개당 품목의 거래 단위를 분리해서 표시
		Txt_Credit_Suffix->SetText(Item.bPricePerUnit == true
			? NSLOCTEXT("MDShop", "CreditPerUnit", "₡/개") : NSLOCTEXT("MDShop", "CreditSuffix", "₡"));
	}

	// 재사용된 카드에 이전 품목의 아이콘이 남지 않도록 기본 Brush 보관
	FSlateBrush ItemIconBrush = GetContentImage();
	// 로드된 정의의 아이콘 또는 빈 이미지 적용
	ItemIconBrush.SetResourceObject(IsValid(Definition) == true ? Definition->ItemIcon.Get() : nullptr);
	// 지정된 아이콘 유무에 따른 이미지 표시 여부 결정
	ItemIconBrush.DrawAs = IsValid(Definition) == true && IsValid(Definition->ItemIcon) == true ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	// 카드의 기존 크기와 색상을 유지하면서 공통 아이콘 갱신
	SetContentImage(ItemIconBrush);
}
