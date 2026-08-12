// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Merchant/MD_MerchantMenuWidget.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "Player/MD_PlayerController.h"
#include "World/Merchant/MD_Merchant.h"

namespace
{
	/** 호감도 표시 범위 (`02` §7) */
	constexpr float AffinityMin = -4.f;
	constexpr float AffinityMax = 5.f;
}

void UMD_MerchantMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 상인 메뉴 열릴 때 입력 모드를 UI로 변경하고 마우스 커서 표시
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
		// 게임 입력을 비활성화하여 캐릭터 이동 차단
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
		}

		// 메뉴가 화면을 덮으므로 HUD를 숨긴다. 복구는 NativeDestruct에서.
		if (AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(PC))
		{
			MDPlayerController->SetGameplayHUDVisible(false);
		}
	}

	// 바인딩되지 않은 버튼은 그냥 건너뛴다 — BindWidgetOptional이라 null일 수 있다.
	if (IsValid(BTN_Talk))
	{
		BTN_Talk->OnClicked.AddDynamic(this, &UMD_MerchantMenuWidget::HandleTalkClicked);
	}
	if (IsValid(BTN_Shop))
	{
		BTN_Shop->OnClicked.AddDynamic(this, &UMD_MerchantMenuWidget::HandleShopClicked);
	}
	if (IsValid(BTN_Quest))
	{
		BTN_Quest->OnClicked.AddDynamic(this, &UMD_MerchantMenuWidget::HandleQuestClicked);
	}
	if (IsValid(BTN_Loan))
	{
		BTN_Loan->OnClicked.AddDynamic(this, &UMD_MerchantMenuWidget::HandleLoanClicked);
	}
	if (IsValid(BTN_Exit))
	{
		BTN_Exit->OnClicked.AddDynamic(this, &UMD_MerchantMenuWidget::HandleExitClicked);
	}

	RefreshDisplay();
}

void UMD_MerchantMenuWidget::NativeDestruct()
{
	// 어떤 경로로 닫혀도 커서/입력 모드를 복구한다 (MD_TravelConsoleWidget과 동일 처리).
	// 상점 화면을 이 메뉴 위에 겹쳐 띄우게 되면, 메뉴를 살려둔 채 스위칭해야 여기서
	// 입력 모드가 초기화되는 문제를 피할 수 있다.
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
		// 게임 입력 복구
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->EnableInput(PC);
		}

		// HUD 복구. 상점·퀘스트 화면은 이 메뉴를 살려둔 채 겹쳐 뜨므로,
		// 메뉴가 실제로 사라지는 이 시점이 HUD를 되돌릴 유일한 지점이다.
		if (AMD_PlayerController* MDPlayerController = Cast<AMD_PlayerController>(PC))
		{
			MDPlayerController->SetGameplayHUDVisible(true);
		}
	}

	Super::NativeDestruct();
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
	if (IsValid(TXT_MerchantName))
	{
		TXT_MerchantName->SetText(MenuInfo.DisplayName);
	}
	// 대사창은 인사말로 시작한다. '대화'를 누르면 TalkLines로 넘어간다.
	ShowDialogueLine(MenuInfo.Greeting);

	// 호감도 — 부호를 항상 붙여야 -0.5와 +0.5가 구분되고,
	// 이름을 붙여야 숫자만 보고 무슨 값인지 헷갈리지 않는다 (`04` §3).
	if (IsValid(TXT_Affinity))
	{
		const FString Sign = MenuInfo.Affinity >= 0.f ? TEXT("+") : TEXT("-");
		TXT_Affinity->SetText(FText::FromString(
			FString::Printf(TEXT("호감도 %s%.1f"), *Sign, FMath::Abs(MenuInfo.Affinity))));
	}
	if (IsValid(PB_Affinity))
	{
		const float Normalized = (MenuInfo.Affinity - AffinityMin) / (AffinityMax - AffinityMin);
		PB_Affinity->SetPercent(FMath::Clamp(Normalized, 0.f, 1.f));
	}
	if (IsValid(TXT_AffinityToNextUnlock))
	{
		TXT_AffinityToNextUnlock->SetText(FText::FromString(
			FString::Printf(TEXT("다음 해금까지 호감도 %.1f"), MenuInfo.AffinityToNextUnlock)));
	}

	// 크레딧
	if (IsValid(TXT_Credits))
	{
		TXT_Credits->SetText(FormatCredits(MenuInfo.Credits));
	}
	if (IsValid(TXT_DailyAdvance))
	{
		TXT_DailyAdvance->SetText(FormatCredits(MenuInfo.DailyAdvance));
	}

	// 대출
	if (IsValid(TXT_DebtBalance))
	{
		TXT_DebtBalance->SetText(FormatCredits(MenuInfo.DebtBalance));
	}
	if (IsValid(TXT_LoanLimit))
	{
		TXT_LoanLimit->SetText(FormatCredits(MenuInfo.LoanLimit));
	}
	if (IsValid(TXT_DailyInterestRate))
	{
		TXT_DailyInterestRate->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f%%/일"), MenuInfo.DailyInterestRate * 100.f)));
	}
	if (IsValid(TXT_NextInterestPreview))
	{
		// `04` §4: 예상 다음 이자를 숫자로 미리 보여준다.
		const int32 NextInterest = FMath::RoundToInt(MenuInfo.DebtBalance * MenuInfo.DailyInterestRate);
		TXT_NextInterestPreview->SetText(FormatCredits(NextInterest));
	}
	if (IsValid(TXT_LoanPenaltyNotice))
	{
		TXT_LoanPenaltyNotice->SetText(FText::FromString(TEXT("패널티는 이자뿐입니다")));
	}

	// 계약 / 퀘스트
	if (IsValid(TXT_ContractDay))
	{
		TXT_ContractDay->SetText(FText::FromString(
			FString::Printf(TEXT("Day %d / %d"), MenuInfo.ContractDay, MenuInfo.ContractTotalDays)));
	}
	if (IsValid(TXT_ActiveQuestReward))
	{
		TXT_ActiveQuestReward->SetText(FormatCredits(MenuInfo.ActiveQuestReward));
	}
	if (IsValid(TXT_QuestPenaltyNotice))
	{
		TXT_QuestPenaltyNotice->SetText(FText::FromString(TEXT("실패해도 위약금은 없다. 호감도만 내려간다.")));
	}
}

void UMD_MerchantMenuWidget::CloseMenu()
{
	// 입력 모드 복구는 NativeDestruct에서 처리한다.
	RemoveFromParent();
}

void UMD_MerchantMenuWidget::ShowDialogueLine(const FText& Line)
{
	if (IsValid(TXT_Greeting))
	{
		// `노라 브릭스: 목록이나 봐.` 형식. 이름이 비어 있으면 대사만 띄운다 —
		// 화자 표기가 있어야 대사창이 UI 안내문이 아니라 대화로 읽힌다.
		TXT_Greeting->SetText(MenuInfo.DisplayName.IsEmpty()
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
	if (IsValid(BDR_MenuPanel))
	{
		BDR_MenuPanel->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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

void UMD_MerchantMenuWidget::HandleShopClicked()
{
	OnShopRequested.Broadcast();
}

void UMD_MerchantMenuWidget::HandleQuestClicked()
{
	OnQuestRequested.Broadcast();
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

FText UMD_MerchantMenuWidget::FormatCredits(int32 Amount)
{
	return FText::FromString(FString::Printf(TEXT("%d₡"), Amount));
}
