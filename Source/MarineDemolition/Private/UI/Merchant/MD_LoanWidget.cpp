// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Merchant/MD_LoanWidget.h"

#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "UI/MD_CommonButtonBase.h"

void UMD_LoanWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 위젯 생성 전에 받은 원장과 재구성된 표시 요소 동기화
	RefreshDisplay();
}

void UMD_LoanWidget::SetLoanInfo(const FMD_MerchantMenuInfo& InLoanInfo)
{
	// 서버에서 전달된 최신 거래 원장 보관
	LoanInfo = InLoanInfo;
	// 입력 중인 금액을 유지하면서 잔액과 거래 가능 상태 갱신
	RefreshDisplay();
}

void UMD_LoanWidget::RefreshDisplay()
{
	// 현재 부채를 제외한 추가 대출 가능액 계산
	int32 AvailableLoanAmount = FMath::Max(0, LoanInfo.LoanLimit - LoanInfo.DebtBalance);
	// 다음 3분 주기에 부과될 고정 이자를 서버와 동일한 원금 기준 계산으로 표시
	int32 NextInterestAmount = UMD_MerchantSubsystem::CalculateLoanInterest(LoanInfo.DebtBalance, LoanInfo.LoanPrincipal);

	// 표시가 달라진 텍스트만 갱신하여 반복 원장 통지에 따른 재배치 방지
	auto SetDisplayText = [](UTextBlock* TextBlock, FText Text)
	{
		// 선택적 바인딩 누락과 동일 표시 내용 검사
		if (IsValid(TextBlock) == true && TextBlock->GetText().EqualTo(Text) == false)
		{
			// 변경된 숫자 표시 반영
			TextBlock->SetText(Text);
		}
	};

	// 현재 보유 크레딧 표시
	SetDisplayText(Txt_Credit, FText::AsNumber(LoanInfo.Credits));
	// 현재 미상환 잔액 표시
	SetDisplayText(Txt_Debt, FText::AsNumber(LoanInfo.DebtBalance));
	// 다음 주기의 예상 이자 표시
	SetDisplayText(Txt_Interest, FText::AsNumber(NextInterestAmount));
	// 현재 부채를 제외한 추가 대출 가능액 표시
	SetDisplayText(Txt_Margin, FText::AsNumber(AvailableLoanAmount));
	// 현재 호감도 구간의 총 대출 한도 표시
	SetDisplayText(Txt_Limit, FText::AsNumber(LoanInfo.LoanLimit));

	// 선택적인 대출 버튼 참조 검사
	if (IsValid(Btn_Borrow) == true)
	{
		// 추가 대출 여지가 남은 경우에만 대출 버튼 활성화
		Btn_Borrow->SetIsEnabled(AvailableLoanAmount > 0);
	}

	// 선택적인 상환 버튼 참조 검사
	if (IsValid(Btn_Repay) == true)
	{
		// 부채와 보유 크레딧이 모두 남은 경우에만 상환 버튼 활성화
		Btn_Repay->SetIsEnabled(LoanInfo.DebtBalance > 0 && LoanInfo.Credits > 0);
	}
}

int32 UMD_LoanWidget::GetRequestedAmount() const
{
	// 입력란이 생략된 레이아웃의 거래 요청 차단
	if (IsValid(InputField_Credit) == false)
	{
		// 유효한 요청 금액이 없는 상태 반환
		return 0;
	}

	// 공용 입력란의 앞뒤 공백을 제거한 거래 금액
	FString AmountText = InputField_Credit->GetText().ToString().TrimStartAndEnd();
	// 숫자 누적 중 범위 검사를 수행할 요청 금액
	int32 ParsedAmount = 0;
	// 소수점이나 부호가 정수 일부로 해석되지 않도록 전체 입력 검사
	for (TCHAR Character : AmountText)
	{
		// 크레딧 정수에 사용할 수 없는 문자 차단
		if (Character < TEXT('0') || Character > TEXT('9'))
		{
			// 잘못된 입력의 거래 요청 차단
			return 0;
		}
		// 현재 자릿수의 숫자 변환
		int32 Digit = Character - TEXT('0');
		// 다음 자릿수 누적 전에 int32 범위 초과 검사
		if (ParsedAmount > (MAX_int32 - Digit) / 10)
		{
			// 원장 범위를 초과한 입력의 거래 요청 차단
			return 0;
		}
		// 검증된 자릿수를 요청 금액에 누적
		ParsedAmount = ParsedAmount * 10 + Digit;
	}

	// 빈 입력과 영은 거래 불가 금액으로 반환
	return ParsedAmount;
}

void UMD_LoanWidget::HandleBorrowClicked()
{
	// Blueprint 클릭 이벤트의 공용 입력 금액 확인
	int32 RequestedAmount = GetRequestedAmount();
	// 최신 원장의 추가 대출 가능액 확인
	int32 AvailableLoanAmount = FMath::Max(0, LoanInfo.LoanLimit - LoanInfo.DebtBalance);
	// 대출 한도와 수령 후 보유 크레딧 범위를 만족하는 요청 검사
	if (RequestedAmount > 0 && RequestedAmount <= AvailableLoanAmount && LoanInfo.Credits <= MAX_int32 - RequestedAmount)
	{
		// 상인 액터에 검증된 대출 요청 전달
		OnBorrowRequested.Broadcast(RequestedAmount);
	}
}

void UMD_LoanWidget::HandleRepayClicked()
{
	// Blueprint 클릭 이벤트의 공용 입력 금액 확인
	int32 RequestedAmount = GetRequestedAmount();
	// 부채와 보유 크레딧을 모두 초과하지 않는 상환 요청 검사
	if (RequestedAmount > 0 && RequestedAmount <= LoanInfo.DebtBalance && RequestedAmount <= LoanInfo.Credits)
	{
		// 상인 액터에 검증된 상환 요청 전달
		OnRepayRequested.Broadcast(RequestedAmount);
	}
}
