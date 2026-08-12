// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Interaction/MD_TravelConsoleWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Player/MD_PlayerController.h"
#include "UI/Interaction/MD_TravelDifficultyWidget.h"
#include "World/Interaction/MD_TravelConsole.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDTravelUI, Log, All);

namespace
{
	constexpr int32 SitePageIndex = 0;
	constexpr int32 DifficultyPageIndex = 1;
}

void UMD_TravelConsoleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BTN_Site_X7)
	{
		BTN_Site_X7->OnClicked.AddDynamic(this, &UMD_TravelConsoleWidget::HandleSiteX7Clicked);
	}
	if (BTN_Site_TR9)
	{
		BTN_Site_TR9->OnClicked.AddDynamic(this, &UMD_TravelConsoleWidget::HandleSiteTR9Clicked);
	}
	if (BTN_Site_Belmont)
	{
		BTN_Site_Belmont->OnClicked.AddDynamic(this, &UMD_TravelConsoleWidget::HandleSiteBelmontClicked);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UMD_TravelConsoleWidget::HandleCloseClicked);
	}

	if (W_DifficultyPage)
	{
		W_DifficultyPage->OnBackRequested.AddDynamic(this, &UMD_TravelConsoleWidget::HandleBackRequested);
		W_DifficultyPage->OnDepartRequested.AddDynamic(this, &UMD_TravelConsoleWidget::HandleDepartRequested);
		W_DifficultyPage->ResetSelection();
	}
	else
	{
		UE_LOG(LogMDTravelUI, Warning,
			TEXT("[%s] W_DifficultyPage 바인딩 실패. WBP에 UMD_TravelDifficultyWidget 자식 위젯을 'W_DifficultyPage' 이름으로 넣어야 출발할 수 있습니다."),
			*GetName());
	}

	bDifficultyNestedInPanel = W_DifficultyPage && BDR_TravelPanel && W_DifficultyPage->IsChildOf(BDR_TravelPanel);
	if (bDifficultyNestedInPanel)
	{
		UE_LOG(LogMDTravelUI, Warning,
			TEXT("[%s] W_DifficultyPage가 BDR_TravelPanel 안에 있습니다. 콘솔 본체를 접으면 난이도 화면도 같이 사라지므로 숨김을 건너뜁니다. 계층에서 W_DifficultyPage를 CVS_Root 직속으로 옮기세요."),
			*GetName());
	}

	InitSiteButtons();
	RefreshHostGate();

	SelectedSiteIndex = INDEX_NONE;
	ShowPage(SitePageIndex);

	// 콘솔 화면이 화면을 덮으므로 HUD를 숨긴다. 복구는 NativeDestruct에서.
	if (AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(GetOwningPlayer()))
	{
		MDPlayerController->SetGameplayHUDVisible(false);
	}
}

void UMD_TravelConsoleWidget::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());

		if (AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(PC))
		{
			MDPlayerController->SetGameplayHUDVisible(true);
		}
	}

	Super::NativeDestruct();
}

bool UMD_TravelConsoleWidget::IsHost() const
{
	const APlayerController* PC = GetOwningPlayer();
	return IsValid(PC) && PC->HasAuthority();
}

void UMD_TravelConsoleWidget::InitSiteButtons()
{
	if (!IsValid(OwningConsole))
	{
		return;
	}

	const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();

	UButton* const SiteButtons[] = { BTN_Site_X7, BTN_Site_TR9, BTN_Site_Belmont };
	UTextBlock* const SiteLabels[] = { TXT_Site_X7, TXT_Site_TR9, TXT_Site_Belmont };

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SiteButtons); ++Index)
	{
		const bool bHasEntry = Sites.IsValidIndex(Index);
		const bool bAvailable = bHasEntry && Sites[Index].bAvailable;

		if (SiteButtons[Index])
		{
			SiteButtons[Index]->SetIsEnabled(bAvailable);
		}

		if (SiteLabels[Index] && bHasEntry)
		{
			SiteLabels[Index]->SetText(
				bAvailable
					? Sites[Index].DisplayName
					: FText::Format(NSLOCTEXT("MDTravel", "SiteLocked", "{0}  (준비 중)"), Sites[Index].DisplayName));
		}
	}
}

void UMD_TravelConsoleWidget::SelectSite(int32 SiteIndex)
{
	if (!IsHost() || !IsValid(OwningConsole))
	{
		return;
	}

	const TArray<FMD_TravelSiteEntry>& Sites = OwningConsole->GetSites();
	if (!Sites.IsValidIndex(SiteIndex) || !Sites[SiteIndex].bAvailable)
	{
		return;
	}

	SelectedSiteIndex = SiteIndex;

	if (W_DifficultyPage)
	{
		W_DifficultyPage->SetSelectedSite(Sites[SiteIndex].DisplayName);
	}

	ShowPage(DifficultyPageIndex);
}

void UMD_TravelConsoleWidget::SetWidgetShown(UWidget* Widget, bool bShown)
{
	if (Widget)
	{
		// SelfHitTestInvisible: 컨테이너 자신은 클릭을 먹지 않고 자식 버튼만 받는다
		Widget->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMD_TravelConsoleWidget::ShowPage(int32 PageIndex)
{
	const bool bOnSitePage = (PageIndex == SitePageIndex);

	if (WS_TravelPages && WS_TravelPages->GetChildrenCount() > PageIndex)
	{
		WS_TravelPages->SetActiveWidgetIndex(PageIndex);
	}

	SetWidgetShown(W_DifficultyPage, !bOnSitePage);

	if (!bDifficultyNestedInPanel)
	{
		SetWidgetShown(BDR_TravelPanel, bOnSitePage);
	}
}

void UMD_TravelConsoleWidget::RefreshHostGate()
{
	const bool bHost = IsHost();

	if (TXT_HostNotice)
	{
		TXT_HostNotice->SetVisibility(bHost ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (!bHost)
	{
		UButton* const Locked[] = { BTN_Site_X7, BTN_Site_TR9, BTN_Site_Belmont };

		for (UButton* Button : Locked)
		{
			if (Button)
			{
				Button->SetIsEnabled(false);
			}
		}
	}
}

void UMD_TravelConsoleWidget::HandleSiteX7Clicked()
{
	SelectSite(0);
}

void UMD_TravelConsoleWidget::HandleSiteTR9Clicked()
{
	SelectSite(1);
}

void UMD_TravelConsoleWidget::HandleSiteBelmontClicked()
{
	SelectSite(2);
}

void UMD_TravelConsoleWidget::HandleBackRequested()
{
	ShowPage(SitePageIndex);
}

void UMD_TravelConsoleWidget::HandleDepartRequested(EMD_Difficulty Difficulty)
{
	if (SelectedSiteIndex == INDEX_NONE)
	{
		return;
	}

	AMD_PlayerController* PC = Cast<AMD_PlayerController>(GetOwningPlayer());

	if (IsValid(PC) && IsValid(OwningConsole))
	{
		PC->ServerRequestTravel(OwningConsole, SelectedSiteIndex, Difficulty);
	}

	CloseWidget();
}

void UMD_TravelConsoleWidget::HandleCloseClicked()
{
	CloseWidget();
}

void UMD_TravelConsoleWidget::CloseWidget()
{
	RemoveFromParent();
}
