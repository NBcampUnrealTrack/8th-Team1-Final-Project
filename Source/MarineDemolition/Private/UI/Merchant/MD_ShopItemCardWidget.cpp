// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopItemCardWidget.h"

#include "Components/TextBlock.h"
#include "Data/Equipment/MD_ToolDefinition.h"
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

	// 장비 정의에 저장된 아이콘을 상점 카드의 단일 이미지 원본으로 조회
	UTexture2D* iconTexture = nullptr;
	if (IsValid(Item.EquipmentDefinition) == true)
	{
		iconTexture = Item.EquipmentDefinition->ToolIcon;
	}

	// 유효한 장비 아이콘을 공통 버튼 이미지에 적용
	if (IsValid(iconTexture) == true)
	{
		// 카드에 설정된 크기와 색상을 유지할 아이콘 Brush 복사
		FSlateBrush itemIconBrush = GetContentImage();

		// 아이템 아이콘을 카드 Brush의 이미지 리소스로 지정
		itemIconBrush.SetResourceObject(iconTexture);
		itemIconBrush.DrawAs = ESlateBrushDrawType::Image;

		// 갱신한 Brush를 Brush_ButtonImage와 바인딩된 Image에 함께 적용
		SetContentImage(itemIconBrush);
	}
}
