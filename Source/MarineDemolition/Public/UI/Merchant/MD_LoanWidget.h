// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "UI/Merchant/MD_MerchantUITypes.h"
#include "MD_LoanWidget.generated.h"

class UEditableText;
class UTextBlock;
class UMD_CommonButtonBase;
class UMD_MerchantMenuWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_LoanAmountRequest, int32, Amount);

/**
 * 대출 UI를 위한 클래스
 */
UCLASS()
class MARINEDEMOLITION_API UMD_LoanWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;

public:
	// 서버 대출 요청을 상인 액터로 전달하기 위한 금액 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Loan")
	FMD_LoanAmountRequest OnBorrowRequested;

	// 서버 상환 요청을 상인 액터로 전달하기 위한 금액 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Loan")
	FMD_LoanAmountRequest OnRepayRequested;

private:
	// 화면 전환 중 입력 모드를 유지할 배경 상인 메뉴 참조
	UPROPERTY()
	TObjectPtr<UMD_MerchantMenuWidget> OwningMerchantMenu;

	// WBP_Loan에서 대출 신청을 실행하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UMD_CommonButtonBase> Btn_Loan;

	// WBP_Loan에서 입력 금액 상환을 실행하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UMD_CommonButtonBase> Btn_Repay;

	// 대출과 상환 양쪽에서 공용으로 사용하는 거래 금액 입력란
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UEditableText> InputField_Amount;

	// 현재 미상환 잔액을 표시하는 값 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> Txt_DebtValue;

	// 다음 일 마감에 합산될 예상 이자를 표시하는 값 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> Txt_InterestValue;

	// 현재 호감도와 부채를 반영한 추가 대출 가능액 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> Txt_ValidLimitValue;

	// 현재 호감도 구간의 총 대출 한도 텍스트
	UPROPERTY(BlueprintReadOnly, Category = "MD|Loan", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> Txt_MaxLimitValue;

	// 복제된 크레딧과 부채를 한 번에 화면에 반영하기 위한 표시 데이터
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Loan", meta = (AllowPrivateAccess = "true"))
	FMD_MerchantMenuInfo LoanInfo;

public:
	// 배경 메뉴를 숨긴 상태로 유지하고 뒤로가기에서 다시 표시하기 위한 설정 함수
	void SetOwningMerchantMenu(UMD_MerchantMenuWidget* InMenuWidget);

	// 복제된 최신 대출 원장을 저장하고 화면을 즉시 갱신하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Loan")
	void SetLoanInfo(const FMD_MerchantMenuInfo& InLoanInfo);

	// 현재 원장과 입력 가능 상태를 모든 바인딩 위젯에 반영하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Loan")
	void RefreshDisplay();

private:
	// 금액 입력란의 문자열을 서버 요청에 사용할 양수 정수로 변환하는 함수
	int32 GetRequestedAmount() const;

protected:
	// Blueprint 버튼 이벤트에서 입력된 금액의 서버 대출 처리를 요청하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Loan")
	void HandleBorrowClicked();

	// Blueprint 버튼 이벤트에서 입력된 금액의 서버 상환 처리를 요청하는 함수
	UFUNCTION(BlueprintCallable, Category = "MD|Loan")
	void HandleRepayClicked();
};
