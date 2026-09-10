// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_DailyReportWidget.generated.h"

class UTextBlock;
class AMD_DayEndConsole;

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

	// Blueprint 그래프에서 전환 버튼의 입력 상태 제어
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "MD|Daily Report")
	void SetContinueEnabled(bool bEnabled);

	// Blueprint 버튼 이벤트에서 서버의 하루 마감 요청 호출
	UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
	void RequestContinue();

    // UI를 연 콘솔을 서버 마감 검증 대상으로 전달
    UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
    void SetOwningConsole(AMD_DayEndConsole* InConsole);

    // 서버가 확정한 보고서를 입력 잠금 상태로 표시
    void SetFinalReport(FMD_InspectionReport& Report, double EndServerTime);

    // 서버 종료 신호에 따른 확정 보고서 비활성화
    void CloseFinalReport();

protected:
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    virtual bool NativeOnHandleBackAction() override;

	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;

protected:
    // 서버 표시 시간 동안 실시간 갱신 및 수동 닫기 차단
    bool bShowingFinalReport = false;

    // 서버가 보고서를 닫고 이동을 시작할 월드 시각
    double ReportEndServerTime = 0.0;

    // 같은 초의 남은 시간 문구를 반복 적용하지 않기 위한 캐시
    int32 DisplayedRemainingSeconds = INDEX_NONE;

    // 서버 기준 남은 시간을 전용 텍스트에 표시
    void UpdateRemainingTime();

    // Blueprint가 소유한 잔업 버튼의 표시 상태 제어
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Daily Report")
    void SetResumeVisibility(ESlateVisibility ResumeVisibility);

    // 이동 대기 상태에 따른 퇴근 버튼 표시 제어
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Daily Report")
    void SetContinueVisibility(ESlateVisibility ContinueVisibility);

    // 이전 실시간 원장 구독을 해제하는 공통 정리
    void StopLedgerUpdates();

    // 퇴근 입력의 서버 거리 검증에 사용할 콘솔 참조
    TWeakObjectPtr<AMD_DayEndConsole> OwningConsole;

    // 서버 응답으로 받은 점수와 크레딧 기록 표시
    UFUNCTION()
    void HandleDailyLedgerReceived(FMD_InspectionReport Report);

	// 양수와 영점 정산 숫자에 적용할 색상
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor PositiveAmountTextColor = FLinearColor(FColor(7, 142, 230));

	// 음수 정산 숫자에 적용할 색상
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor NegativeAmountTextColor = FLinearColor(FColor(234, 107, 114));

	// 현재 정산 Day를 표시하는 부제목
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ReportHeader;

    // 다음 일자 이동까지 남은 시간 표시 영역
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_TravelTimer;

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

	// 점수 영역의 분류별 값과 당일 총점 및 부호 색상 적용
	void SetScoreValues(const FMD_InspectionReport& Report);

	// 크레딧 영역의 분류별 값과 전체 변동 합계 및 부호 색상 적용
	void SetCreditValues(const FMD_InspectionReport& Report);

	// 원장 배열에서 요청한 분류의 정산 값 조회
	static int32 FindDailyReportValue(const TArray<FMD_DailyReportValue>& Values, EMD_DailyReportCategory Category);

	// 점수와 크레딧에 부호 및 천 단위 구분 적용
	static FText FormatDailyReportAmount(int32 Amount);

	// 정산 숫자의 서식과 부호 색상을 공통 경로로 적용
	void SetAmountText(UTextBlock* AmountTextBlock, int32 Amount);

	// Blueprint 그래프에서 정산 결과에 맞는 전환 문구 표시
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Daily Report")
	void SetContinueText(const FText& ButtonText);

};
