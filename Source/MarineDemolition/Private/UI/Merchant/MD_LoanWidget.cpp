// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoanWidget.h"

#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "UI/MD_CommonButtonBase.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"

void UMD_LoanWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 대출 화면이 활성화된 동안 배경 상인 메뉴 패널 숨김
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(false);
	}

	// 현재 대출 원장을 활성화된 화면에 반영
	RefreshDisplay();
}

void UMD_LoanWidget::NativeOnDeactivated()
{
	// 대출 화면 비활성화 후 배경 상인 메뉴 패널 복원
	if (IsValid(OwningMerchantMenu) == true)
	{
		OwningMerchantMenu->SetMenuPanelVisible(true);
	}

	Super::NativeOnDeactivated();
}

void UMD_LoanWidget::SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget)
{
	// 활성화와 비활성화에서 제어할 배경 상인 메뉴 저장
	OwningMerchantMenu = InMenuWidget;
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

	if (IsValid(Txt_DebtValue) == true)
	{
		// 별도 크레딧 아이콘 옆에 현재 부채 숫자만 표시
		FText DebtText = FText::AsNumber(LoanInfo.DebtBalance);
		Txt_DebtValue->SetText(DebtText);
	}

	if (IsValid(Txt_InterestValue) == true)
	{
		// 별도 크레딧 아이콘 옆에 다음 정산 예상 이자 숫자만 표시
		FText LoanInterestText = FText::AsNumber(NextInterestAmount);
		Txt_InterestValue->SetText(LoanInterestText);
	}

	if (IsValid(Txt_ValidLimitValue) == true)
	{
		// 현재 부채를 제외한 추가 대출 가능액 표시
		FText AvailableLoanText = FText::AsNumber(AvailableLoanAmount);
		Txt_ValidLimitValue->SetText(AvailableLoanText);
	}

	if (IsValid(Txt_MaxLimitValue) == true)
	{
		// 현재 호감도 구간에서 허용되는 총 대출 한도 표시
		FText LoanLimitText = FText::AsNumber(LoanInfo.LoanLimit);
		Txt_MaxLimitValue->SetText(LoanLimitText);
	}

	if (IsValid(Btn_Loan) == true)
	{
		// 추가 대출 여지가 남은 경우에만 대출 버튼 활성화
		Btn_Loan->SetIsEnabled(AvailableLoanAmount > 0);
	}

	if (IsValid(Btn_Repay) == true)
	{
		// 부채와 상환 가능 크레딧이 모두 남은 경우에만 상환 버튼 활성화
		bool bCanRepay = LoanInfo.DebtBalance > 0;
		if (bCanRepay == true)
		{
			bCanRepay = LoanInfo.Credits > 0;
		}
		Btn_Repay->SetIsEnabled(bCanRepay);
	}
}

int32 UMD_LoanWidget::GetRequestedAmount() const
{
	if (IsValid(InputField_Amount) == false)
	{
		return 0;
	}

	// 현재 레이아웃의 공용 금액 입력 문자열 정리
	FString AmountText = InputField_Amount->GetText().ToString().TrimStartAndEnd();
	if (AmountText.IsNumeric() == false)
	{
		return 0;
	}

	// 크레딧 원장과 같은 int32 범위를 벗어난 입력을 변환 단계에서 거부하는 파싱
	int32 ParsedAmount = 0;
	if (LexTryParseString(ParsedAmount, *AmountText) == false || ParsedAmount <= 0)
	{
		return 0;
	}

	return ParsedAmount;
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
