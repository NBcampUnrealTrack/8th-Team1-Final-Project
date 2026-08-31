// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_MerchantMenuWidget.h"

#include "Components/TextBlock.h"
#include "Player/MD_PlayerController.h"

void UMD_MerchantMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 공통 GameMenu가 입력 모드를 관리하므로 상인 메뉴는 게임 HUD만 숨긴다.
	if (APlayerController* PC = GetOwningPlayer())
	{
		// 메뉴가 화면을 덮는 동안 플레이 HUD를 숨긴다.
		if (AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(PC))
		{
			MDPlayerController->SetGameplayHUDVisible(false);
		}
	}

	RefreshDisplay();
}

void UMD_MerchantMenuWidget::SetMenuInfo(const FMD_MerchantMenuInfo& InInfo)
{
	MenuInfo = InInfo;

	// 대사 목록이 갈렸을 수 있으니 재생 위치를 처음으로 되돌린다.
	TalkLineIndex = INDEX_NONE;

	RefreshDisplay();
}

void UMD_MerchantMenuWidget::RefreshDisplay()
{
	// 상인 신원
	if (IsValid(Txt_Name) == true)
	{
		Txt_Name->SetText(MenuInfo.DisplayName);
	}

	// 현재 호감도를 양수와 음수가 구분되도록 부호와 소수점 한 자리로 표시한다.
	if (IsValid(Txt_Affinity) == true)
	{
		FString AffinitySign = MenuInfo.Affinity >= 0.f ? TEXT("+") : TEXT("-");
		FString AffinityValue = FString::Printf(TEXT("호감도 %s%.1f"), *AffinitySign, FMath::Abs(MenuInfo.Affinity));
		Txt_Affinity->SetText(FText::FromString(AffinityValue));
	}

	// 대사창은 인사말로 시작한다. '대화'를 누르면 TalkLines로 넘어간다.
	ShowDialogueLine(MenuInfo.Greeting);
}

void UMD_MerchantMenuWidget::CloseMenu()
{
	// 상인 메뉴를 실제로 닫을 때 게임 HUD를 복구한다.
	APlayerController* PC = GetOwningPlayer();
	if (IsValid(PC) == true)
	{
		AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(PC);
		if (IsValid(MDPlayerController) == true)
		{
			MDPlayerController->SetGameplayHUDVisible(true);
		}
	}

	// Common UI Stack의 표준 비활성화 경로로 상인 메뉴를 닫는다.
	DeactivateWidget();
}

void UMD_MerchantMenuWidget::ShowDialogueLine(const FText& Line)
{
	if (IsValid(Txt_Dialogue) == true)
	{
		// `노라 브릭스: 목록이나 봐.` 형식. 이름이 비어 있으면 대사만 띄운다 —
		// 화자 표기가 있어야 대사창이 UI 안내문이 아니라 대화로 읽힌다.
		Txt_Dialogue->SetText(MenuInfo.DisplayName.IsEmpty()
			? Line
			: FText::Format(FText::FromString(TEXT("{0}: {1}")), MenuInfo.DisplayName, Line));
	}

	if (IsValid(BDR_Dialogue))
	{
		BDR_Dialogue->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UMD_MerchantMenuWidget::SetMenuPanelVisible(bool bVisible)
{
	if (IsValid(BDR_BodyPanel) == true)
	{
		BDR_BodyPanel->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UMD_MerchantMenuWidget::HandleTalkClicked()
{
	// 누를 때마다 다음 줄로 넘기고 끝에서 처음으로 돈다.
	// 대사가 비어 있으면 인사말을 다시 띄운다 — 빈 대사창이 뜨는 것보다 낫다.
	if (MenuInfo.TalkLines.Num() > 0)
	{
		TalkLineIndex = (TalkLineIndex + 1) % MenuInfo.TalkLines.Num();
		ShowDialogueLine(MenuInfo.TalkLines[TalkLineIndex]);
	}
	else
	{
		ShowDialogueLine(MenuInfo.Greeting);
	}

	OnTalkRequested.Broadcast();
}

void UMD_MerchantMenuWidget::HandleLoanClicked()
{
	OnLoanRequested.Broadcast();
}

void UMD_MerchantMenuWidget::HandleExitClicked()
{
	OnExitRequested.Broadcast();
	CloseMenu();
}
