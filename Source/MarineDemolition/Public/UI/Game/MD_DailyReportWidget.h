// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_DailyReportWidget.generated.h"

class UButton;
class UTextBlock;

// 일일 보고서의 다음 전환 요청을 소유 PlayerController에 전달하는 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_OnDailyReportContinueRequested);

/**
 * WBP_DailyReport의 정산 데이터 표시와 확인 버튼 입력을 담당하는 전용 위젯 클래스
 */
UCLASS()
class MARINEDEMOLITION_API UMD_DailyReportWidget : public UUserWidget
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
	virtual void NativeOnInitialized() override;

	virtual void NativeDestruct() override;

private:
	// 다음 Day 또는 결과 맵 전환 확인 입력을 받는 버튼
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Nextday;

	// 현재 정산 Day와 판정 결과를 표시하는 제목
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_tHeader;

	// 판정 결과에 맞는 확인 버튼 문구 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Nextday;

	// 분류 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_CategorizingScore_Value;

	// 복귀 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ReturnScore_Value;

	// 사고·기타 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_AccidentScore_Value;

	// 퀘스트 점수 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestScore_Value;

	// 당일 총점 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_TotalScore_Value;

	// 착수금 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_RetainerCredit_Value;

	// 분류 환급 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_CategorizingCredit_Value;

	// 사고·기타 조정 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_AccidentCredit_Value;

	// 평가 보상 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_EvaluationCredit_Value;

	// 퀘스트 금액 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_QuestCredit_Value;

	// 구매·대출·상환 등 기타 금액 값 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_OtherCredit_Value;

	// 당일 최종 크레딧 변동 합계 표시 영역
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_TotalCredit_Value;

	// WBP 버튼 클릭을 외부 전환 요청 이벤트로 변환하는 처리
	UFUNCTION()
	void HandleContinueClicked();

};
