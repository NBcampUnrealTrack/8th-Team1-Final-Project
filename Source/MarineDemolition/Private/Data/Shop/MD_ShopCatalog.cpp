// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/Shop/MD_ShopCatalog.h"

bool FMD_ShopItem::IsUnlocked(float Affinity, int32 QuestCompletions, const TArray<FName>& OwnedItemIds) const
{
	if (bRequiresAffinity && Affinity < AffinityRequired)
	{
		return false;
	}

	if (QuestCompletionsRequired > 0 && QuestCompletions < QuestCompletionsRequired)
	{
		return false;
	}

	if (!PrerequisiteItemId.IsNone() && !OwnedItemIds.Contains(PrerequisiteItemId))
	{
		return false;
	}

	return true;
}

FText FMD_ShopItem::GetUnlockConditionText() const
{
	TArray<FString> Parts;

	if (bRequiresAffinity)
	{
		// 기획서 표기를 그대로 따른다: `호감 ≥0`, `호감 ≥+1`
		const FString Sign = AffinityRequired > 0.f ? TEXT("+") : TEXT("");
		Parts.Add(FString::Printf(TEXT("호감 ≥%s%g"), *Sign, AffinityRequired));
	}

	if (QuestCompletionsRequired > 0)
	{
		Parts.Add(FString::Printf(TEXT("퀘스트 %d회"), QuestCompletionsRequired));
	}

	if (!PrerequisiteItemId.IsNone())
	{
		Parts.Add(FString::Printf(TEXT("%s 보유"), *PrerequisiteItemId.ToString()));
	}

	if (Parts.Num() == 0)
	{
		return FText::FromString(TEXT("상점 상시"));
	}

	return FText::FromString(FString::Join(Parts, TEXT(" · ")));
}

FText FMD_ShopItem::GetPriceText() const
{
	if (Price <= 0)
	{
		// `07` §4에서 기본 장비는 "0 (지급)"으로 표기한다.
		return FText::FromString(TEXT("지급"));
	}

	return FText::FromString(bPricePerUnit
		? FString::Printf(TEXT("%d₡/개"), Price)
		: FString::Printf(TEXT("%d₡"), Price));
}

TArray<FMD_ShopItem> UMD_ShopCatalog::GetItemsInCategory(EMD_ShopCategory InCategory) const
{
	TArray<FMD_ShopItem> Result;

	for (const FMD_ShopItem& Item : Items)
	{
		if (Item.Category == InCategory)
		{
			Result.Add(Item);
		}
	}

	return Result;
}

bool UMD_ShopCatalog::FindItem(FName ItemId, FMD_ShopItem& OutItem) const
{
	for (const FMD_ShopItem& Item : Items)
	{
		if (Item.ItemId == ItemId)
		{
			OutItem = Item;
			return true;
		}
	}

	return false;
}
