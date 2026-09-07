// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_DailyReportWidget.generated.h"

class UMD_CommonButtonBase;
class UTextBlock;

// 일일 보고서의 다음 전환 요청을 소유 PlayerController에 전달하는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnDailyReportContinueRequested);

/**
 * GameMenu 레이어에서 정산 데이터와 확인 입력을 처리하는 위젯 클래스
 */
UCLASS()
class MARINEDEMOLITION_API UMD_DailyReportWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

public:
	// 서버가 확정한 일일 보고서 데이터를 기존 WBP 텍스트 영역에 반영하는 처리
	UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
	void SetReport(const FMD_InspectionReport& Report);

	// 전환 요청 이후 중복 클릭 여부를 WBP 내부에서 제어하는 처리
	UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
	void SetContinueEnabled(bool bEnabled);

	// 기본 버튼이 없는 파생 WBP에서도 Blueprint 입력으로 동일한 전환 요청을 발생시키는 진입점
	UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
	void RequestContinue();

	// 확인 버튼 입력을 실제 서버 전환 요청과 분리하여 외부 소유자에게 전달하는 이벤트
	UPROPERTY(BlueprintAssignable, Category = "MD|Daily Report")
	FMD_OnDailyReportContinueRequested OnContinueRequested;

protected:
	virtual void NativeConstruct() override;

	virtual void NativeDestruct() override;

protected:
	// 양수와 영점 정산 숫자에 적용할 색상
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor PositiveAmountTextColor = FLinearColor(FColor(7, 142, 230));

	// 음수 정산 숫자에 적용할 색상
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor NegativeAmountTextColor = FLinearColor(FColor(234, 107, 114));

	// BP에서 잔업 입력을 연결하는 공통 버튼
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MD|Daily Report")
	TObjectPtr<UMD_CommonButtonBase> Btn_Resume;

private:
	// 다음 Day 또는 결과 맵 전환 확인 입력을 받는 버튼
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_EndDay;

	// 현재 정산 Day를 표시하는 부제목
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ReportHeader;

	// 현재 점검 결과를 표시하는 상태 배지
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Badge;

	// 분류 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Score_Categorizing;

	// 복귀 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Score_Return;

	// 사고·기타 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Score_Accident;

	// 퀘스트 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Score_Quest;

	// 당일 총점 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Score_Total;

	// 착수금 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credit_Retainer;

	// 분류 환급 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credit_Categorizing;

	// 사고·기타 조정 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credit_Accident;

	// 평가 보상 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credit_Evaluation;

	// 퀘스트 금액 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credit_Quest;

	// 당일 최종 크레딧 변동 합계 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Credit_Total;

	// 원장 배열에서 요청한 분류의 정산 값 조회
	static int32 FindDailyReportValue(const TArray<FMD_DailyReportValue>& Values, EMD_DailyReportCategory Category);

	// 부호와 천 단위 구분을 적용하고 최종 크레딧에만 통화 기호 추가
	static FText FormatDailyReportAmount(int32 Amount, bool bIncludeCreditUnit);

	// 정산 숫자의 부호에 따른 색상 적용
	void ApplyAmountTextColor(UTextBlock* AmountTextBlock, int32 Amount);

	// WBP 버튼 클릭을 외부 전환 요청 이벤트로 변환하는 처리
	UFUNCTION()
	void HandleContinueClicked();

};
