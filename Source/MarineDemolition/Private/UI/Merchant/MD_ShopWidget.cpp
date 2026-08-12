// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_ShopWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "UI/Merchant/MD_ShopItemCardWidget.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"

namespace
{
	FText FormatCredits(int32 Amount)
	{
		return FText::FromString(FString::Printf(TEXT("%d₡"), Amount));
	}

	FText GetCategoryDisplayName(EMD_ShopCategory Category)
	{
		// enum의 UMETA DisplayName을 그대로 가져온다 — 표기가 두 군데로 갈리지 않게.
		if (const UEnum* EnumPtr = StaticEnum<EMD_ShopCategory>())
		{
			return EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(Category));
		}

		return FText::GetEmpty();
	}
}

void UMD_ShopWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 상점 화면 열릴 때 입력 모드를 UI로 유지
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
		// 게임 입력을 비활성화하여 캐릭터 이동 차단
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
		}
	}

	if (IsValid(BTN_CatTool))
	{
		BTN_CatTool->OnClicked.AddDynamic(this, &UMD_ShopWidget::HandleCategoryToolClicked);
	}
	if (IsValid(BTN_CatSafety))
	{
		BTN_CatSafety->OnClicked.AddDynamic(this, &UMD_ShopWidget::HandleCategorySafetyClicked);
	}
	if (IsValid(BTN_CatUtilityRepair))
	{
		BTN_CatUtilityRepair->OnClicked.AddDynamic(this, &UMD_ShopWidget::HandleCategoryUtilityRepairClicked);
	}
	if (IsValid(BTN_CatSurvival))
	{
		BTN_CatSurvival->OnClicked.AddDynamic(this, &UMD_ShopWidget::HandleCategorySurvivalClicked);
	}
	if (IsValid(BTN_CatRecovery))
	{
		BTN_CatRecovery->OnClicked.AddDynamic(this, &UMD_ShopWidget::HandleCategoryRecoveryClicked);
	}
	if (IsValid(BTN_Back))
	{
		BTN_Back->OnClicked.AddDynamic(this, &UMD_ShopWidget::HandleBackClicked);
	}

	// 상점이 열릴 때 메뉴 패널을 숨긴다
	if (IsValid(OwningMerchantMenu))
	{
		OwningMerchantMenu->SetMenuPanelVisible(false);
	}

	RefreshDisplay();
}

void UMD_ShopWidget::NativeDestruct()
{
	// 상점이 닫힐 때 메뉴 패널을 다시 표시한다
	if (IsValid(OwningMerchantMenu))
	{
		OwningMerchantMenu->SetMenuPanelVisible(true);
	}

	// 상인 메뉴가 남아 있으면 입력 복구는 메뉴 쪽에서 처리한다.
	if (!IsValid(OwningMerchantMenu))
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->EnableInput(PC);
			}
		}
	}

	Super::NativeDestruct();
}

void UMD_ShopWidget::SetCatalog(UMD_ShopCatalog* InCatalog)
{
	Catalog = InCatalog;
	RefreshDisplay();
}

void UMD_ShopWidget::SetShopInfo(const FMD_MerchantMenuInfo& InInfo)
{
	ShopInfo = InInfo;
	RefreshDisplay();
}

void UMD_ShopWidget::SetProgress(int32 InQuestCompletions, const TArray<FName>& InOwnedItemIds)
{
	QuestCompletions = InQuestCompletions;
	OwnedItemIds = InOwnedItemIds;
	RefreshDisplay();
}

void UMD_ShopWidget::SelectCategory(EMD_ShopCategory InCategory)
{
	SelectedCategory = InCategory;
	RefreshDisplay();
}

void UMD_ShopWidget::RefreshDisplay()
{
	if (IsValid(TXT_CategoryTitle))
	{
		TXT_CategoryTitle->SetText(GetCategoryDisplayName(SelectedCategory));
	}

	// 헤더 — `07` §8 "상점 UI에 보여줄 정보"
	if (IsValid(TXT_Credits))
	{
		TXT_Credits->SetText(FormatCredits(ShopInfo.Credits));
	}
	if (IsValid(TXT_DailyAdvance))
	{
		TXT_DailyAdvance->SetText(FormatCredits(ShopInfo.DailyAdvance));
	}
	// `04` §3은 "호감도 옆에 `다음 해금까지 0.6` 표시"를 요구한다.
	// 숫자만 두면 무엇의 0.6인지 읽는 사람이 알 수 없어서 둘 다 무슨 값인지 이름을 붙인다.
	if (IsValid(TXT_Affinity))
	{
		const FString Sign = ShopInfo.Affinity >= 0.f ? TEXT("+") : TEXT("-");
		TXT_Affinity->SetText(FText::FromString(
			FString::Printf(TEXT("호감도 %s%.1f"), *Sign, FMath::Abs(ShopInfo.Affinity))));
	}
	if (IsValid(TXT_AffinityToNextUnlock))
	{
		TXT_AffinityToNextUnlock->SetText(FText::FromString(
			FString::Printf(TEXT("다음 해금까지 호감도 %.1f"), ShopInfo.AffinityToNextUnlock)));
	}
	if (IsValid(TXT_ActiveQuestReward))
	{
		TXT_ActiveQuestReward->SetText(FormatCredits(ShopInfo.ActiveQuestReward));
	}

	// 안내 문구
	if (IsValid(TXT_TrashBinNotice))
	{
		TXT_TrashBinNotice->SetText(FText::FromString(
			TEXT("쓰레기통은 판매하지 않는다. 기지 폐기물 대기장에서 무한히 꺼내 쓴다.")));
	}

	RebuildItemList();
}

void UMD_ShopWidget::RebuildItemList()
{
	if (!IsValid(PNL_ItemList))
	{
		return;
	}

	PNL_ItemList->ClearChildren();

	if (!IsValid(Catalog) || !ItemCardClass)
	{
		if (IsValid(TXT_EmptyCategory))
		{
			TXT_EmptyCategory->SetVisibility(ESlateVisibility::Visible);
			TXT_EmptyCategory->SetText(FText::FromString(TEXT("카탈로그가 지정되지 않았다.")));
		}
		return;
	}

	const TArray<FMD_ShopItem> CategoryItems = Catalog->GetItemsInCategory(SelectedCategory);

	if (IsValid(TXT_EmptyCategory))
	{
		TXT_EmptyCategory->SetVisibility(CategoryItems.Num() > 0 ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		if (CategoryItems.Num() == 0)
		{
			TXT_EmptyCategory->SetText(FText::FromString(TEXT("취급 품목이 없다.")));
		}
	}

	for (const FMD_ShopItem& Item : CategoryItems)
	{
		UMD_ShopItemCardWidget* Card = CreateWidget<UMD_ShopItemCardWidget>(this, ItemCardClass);
		if (!IsValid(Card))
		{
			continue;
		}

		// 잠긴 품목도 목록에서 빼지 않는다 — `04` §3 UX 포인트가
		// "잠긴 아이템도 다음에 뭐가 열리는지 보이게"를 요구한다.
		const bool bUnlocked = Item.IsUnlocked(ShopInfo.Affinity, QuestCompletions, OwnedItemIds);
		const bool bAffordable = ShopInfo.Credits >= Item.Price;

		Card->SetItem(Item, bUnlocked, bAffordable);
		Card->OnBuyRequested.AddDynamic(this, &UMD_ShopWidget::HandleCardBuyRequested);

		PNL_ItemList->AddChild(Card);
	}
}

void UMD_ShopWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

void UMD_ShopWidget::HandleCategoryToolClicked()
{
	SelectCategory(EMD_ShopCategory::Tool);
}

void UMD_ShopWidget::HandleCategorySafetyClicked()
{
	SelectCategory(EMD_ShopCategory::Safety);
}

void UMD_ShopWidget::HandleCategoryUtilityRepairClicked()
{
	SelectCategory(EMD_ShopCategory::UtilityRepair);
}

void UMD_ShopWidget::HandleCategorySurvivalClicked()
{
	SelectCategory(EMD_ShopCategory::Survival);
}

void UMD_ShopWidget::HandleCategoryRecoveryClicked()
{
	SelectCategory(EMD_ShopCategory::Recovery);
}

void UMD_ShopWidget::SetFullyStockedItems(const TArray<FName>& InItemIds)
{
	FullyStockedItemIds = InItemIds;
	RefreshDisplay();
}

void UMD_ShopWidget::HandleCardBuyRequested(FName ItemId)
{
	// 가득 찬 품목은 버튼을 비활성화하지 않고 눌리게 두되 여기서 막는다.
	// 눌러야 상인이 이유를 말해줄 수 있어서다 — 회색으로 죽어 있으면
	// 플레이어는 왜 못 사는지 모른 채 넘어간다.
	if (FullyStockedItemIds.Contains(ItemId))
	{
		OnPurchaseRejected.Broadcast(ItemId);
		return;
	}

	// 실제 결제·인벤 반영은 경제 시스템이 붙은 뒤에 처리한다.
	OnItemPurchaseRequested.Broadcast(ItemId);
}
