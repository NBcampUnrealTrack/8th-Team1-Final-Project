// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoanWidget.h"

#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"

void UMD_LoanWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(Btn_LoanApplyButton) == true)
	{
		Btn_LoanApplyButton->OnClicked.AddUniqueDynamic(this, &UMD_LoanWidget::HandleBorrowClicked);
	}

	if (IsValid(Btn_LoanRepayButton) == true)
	{
		Btn_LoanRepayButton->OnClicked.AddUniqueDynamic(this, &UMD_LoanWidget::HandleRepayClicked);
	}

	if (IsValid(Btn_RepayAll) == true)
	{
		Btn_RepayAll->OnClicked.AddUniqueDynamic(this, &UMD_LoanWidget::HandleRepayAllClicked);
	}

	if (IsValid(Btn_Back) == true)
	{
		Btn_Back->OnClicked.AddUniqueDynamic(this, &UMD_LoanWidget::HandleBackClicked);
	}

	RefreshDisplay();
}

void UMD_LoanWidget::NativeDestruct()
{
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(true);
	}

	Super::NativeDestruct();
}

void UMD_LoanWidget::SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget)
{
	OwningMerchantMenu = InMenuWidget;
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(false);
	}
}

void UMD_LoanWidget::SetLoanInfo(const FMD_MerchantMenuInfo& InLoanInfo)
{
	LoanInfo = InLoanInfo;
	RefreshDisplay();
}

void UMD_LoanWidget::RefreshDisplay()
{
	int32 AvailableLoanAmount = FMath::Max(0, LoanInfo.LoanLimit - LoanInfo.DebtBalance);
	int32 NextInterestAmount = FMath::RoundToInt(LoanInfo.DebtBalance * LoanInfo.DailyInterestRate);

	if (IsValid(Txt_CurrentdebtNum) == true)
	{
		FText DebtText = ToLoanCreditText(LoanInfo.DebtBalance);
		Txt_CurrentdebtNum->SetText(DebtText);
	}

	if (IsValid(Txt_LoanInterestNum) == true)
	{
		FText LoanInterestText = ToLoanCreditText(NextInterestAmount);
		Txt_LoanInterestNum->SetText(LoanInterestText);
	}

	if (IsValid(Txt_CreditsNum) == true)
	{
		FText CreditText = ToLoanCreditText(LoanInfo.Credits);
		Txt_CreditsNum->SetText(CreditText);
	}

	if (IsValid(Txt_AvailableLoanNum) == true)
	{
		FText CreditText = ToLoanCreditText(AvailableLoanAmount);
		Txt_AvailableLoanNum->SetText(CreditText);
	}

	if (IsValid(Txt_LoanLimitNum) == true)
	{
		FText CreditText = ToLoanCreditText(LoanInfo.LoanLimit);
		Txt_LoanLimitNum->SetText(CreditText);
	}

	if (IsValid(Btn_LoanApplyButton) == true)
	{
		Btn_LoanApplyButton->SetIsEnabled(AvailableLoanAmount > 0);
	}

	if (IsValid(Btn_LoanRepayButton) == true)
	{
		Btn_LoanRepayButton->SetIsEnabled(LoanInfo.DebtBalance > 0 && LoanInfo.Credits > 0);
	}

	if (IsValid(Btn_RepayAll) == true)
	{
		Btn_RepayAll->SetIsEnabled(LoanInfo.DebtBalance > 0 && LoanInfo.Credits >= LoanInfo.DebtBalance);
	}
}

int32 UMD_LoanWidget::GetRequestedAmount() const
{
	if (IsValid(Et_Repayment) == false)
	{
		return 0;
	}

	FString AmountText = Et_Repayment->GetText().ToString().TrimStartAndEnd();
	if (AmountText.IsNumeric() == false)
	{
		return 0;
	}

	int64 ParsedAmount = FCString::Atoi64(*AmountText);

	int32 Amount = ParsedAmount > 0 && ParsedAmount <= MAX_int32 ? static_cast<int32>(ParsedAmount) : 0;
	return Amount;
}

void UMD_LoanWidget::HandleBorrowClicked()
{
	int32 RequestedAmount = GetRequestedAmount();
	int32 AvailableLoanAmount = FMath::Max(0, LoanInfo.LoanLimit - LoanInfo.DebtBalance);
	if (RequestedAmount > 0 && RequestedAmount <= AvailableLoanAmount)
	{
		OnBorrowRequested.Broadcast(RequestedAmount);
	}
}

void UMD_LoanWidget::HandleRepayClicked()
{
	int32 RequestedAmount = GetRequestedAmount();
	if (RequestedAmount > 0 && RequestedAmount <= LoanInfo.DebtBalance && RequestedAmount <= LoanInfo.Credits)
	{
		OnRepayRequested.Broadcast(RequestedAmount);
	}
}

void UMD_LoanWidget::HandleRepayAllClicked()
{
	if (LoanInfo.DebtBalance > 0 && LoanInfo.Credits >= LoanInfo.DebtBalance)
	{
		OnRepayRequested.Broadcast(LoanInfo.DebtBalance);
	}
}

void UMD_LoanWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

FText UMD_LoanWidget::ToLoanCreditText(int32 Amount)
{
	FString CreditString = FString::Printf(TEXT("%d₡"), Amount);
	return FText::FromString(CreditString);
}
