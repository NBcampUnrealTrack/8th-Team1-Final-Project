// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_DailyReportWidget.generated.h"

class UTextBlock;
class AMD_DayEndConsole;

/**
 * GameMenu 레이어에서 정산 데이터와 확인 입력을 처리하는 위젯 클래스
 */
UCLASS(meta = (DisableNativeTick))
class MARINEDEMOLITION_API UMD_DailyReportWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

public:
	// 서버가 확정한 일일 보고서 데이터를 기존 WBP 텍스트 영역에 반영하는 처리
	UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
	void SetReport(const FMD_InspectionReport& Report);

    // 두 합계의 현재 표시값에서 지정한 초 동안 실제 보고서 값까지 변화
    UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
    void AnimateSummaryValues(float Duration = 1.0f);

	// Blueprint에서 정산 화면의 퇴근 및 잔업 버튼 입력 상태 제어
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "MD|Daily Report")
	void SetContinueEnabled(bool bEnabled);

	// Blueprint 버튼 이벤트에서 서버의 하루 마감 요청 호출
	UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
	void RequestContinue();

    // Blueprint 잔업 버튼에서 일반 보고서만 닫고 작업 재개
    UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
    void RequestResume();

    // UI를 연 콘솔을 서버 마감 검증 대상으로 전달
    UFUNCTION(BlueprintCallable, Category = "MD|Daily Report")
    void SetOwningConsole(AMD_DayEndConsole* InConsole);

    // 서버가 확정한 보고서를 입력 잠금 상태로 표시
    void SetFinalReport(FMD_InspectionReport& Report, double EndServerTime);

    // 서버 종료 신호에 따른 확정 보고서 비활성화
    void CloseFinalReport();

protected:
    virtual void NativeConstruct() override;

    virtual void NativeDestruct() override;

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

    // 남은 시간이 다음 정수 초로 바뀔 때 실행할 단발 타이머
    FTimerHandle RemainingTimeTimer;

    // 연출 중 표시값과 구분하여 보관할 실제 일일 점수 합계
    int64 SummaryScore = 0;

    // 분류별 크레딧을 합산한 실제 일일 변동액
    int64 SummaryCredit = 0;

    // 이번 연출을 시작할 때 점수 텍스트에 표시된 값
    int64 SummaryScoreStart = 0;

    // 이번 연출을 시작할 때 크레딧 텍스트에 표시된 값
    int64 SummaryCreditStart = 0;

    // 합계 연출 중에만 실행할 반복 타이머
    FTimerHandle SummaryAnimationTimer;

    // 콜백 지연을 누적하지 않기 위한 연출 시작 월드 시각
    double SummaryAnimationStartTime = 0.0;

    // 현재 표시값부터 실제 합계까지 변화할 게임 시간의 초 단위 길이
    float SummaryAnimationDuration = 0.0f;

    // 현재 활성화에서 Blueprint가 요청한 합계 연출의 대기 및 재생 여부
    bool bSummaryAnimationRequested = false;

    // 초기화용 빈 보고서와 서버에서 도착한 실제 합계 구분
    bool bHasSummaryTargets = false;

    // 숫자 영역별 마지막 값과 지역화된 표시 문자열 재사용
    TMap<TWeakObjectPtr<UTextBlock>, TPair<int64, FText>> AmountTextCache;

    // 경과 시간에 맞춰 점수와 크레딧 합계를 함께 보간
    void UpdateSummaryAnimation();

    // 반복 예약 정리 및 필요할 때 실제 합계로 표시 마무리
    void StopSummaryAnimation(bool bApplyFinalValues = true);

    // 현재 텍스트의 부호와 지역화된 자리 구분을 해석하여 연출 시작값 조회
    int64 ReadSummaryAmount(UTextBlock* AmountTextBlock, int64 FallbackAmount);

    // 새 보고서의 표시 반영 이후 Blueprint에서 합계 연출 시작 시점 선택
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Daily Report", meta = (DisplayName = "On Report Updated"))
    void BP_OnReportUpdated(bool bFinalReport);

    // 합계가 최종값에 도달한 뒤 강조 연출을 연결하는 완료 이벤트
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|Daily Report", meta = (DisplayName = "On Summary Animation Finished"))
    void BP_OnSummaryAnimationFinished();

    // 서버 기준 남은 시간을 전용 텍스트에 표시
    void UpdateRemainingTime();

    // 비활성화 및 Slate 해제 시 예약된 카운트다운 정리
    void StopRemainingTimeUpdates();

    // Blueprint에서 잔업 버튼의 표시 상태 제어
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
	TObjectPtr<UTextBlock> Txt_SubHeader;

    // 다음 일자 이동까지 남은 시간 표시 영역
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_TravelTimer;

	// 현재 점검 결과를 표시하는 상태 배지
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Badge;

    // 보고서가 집계된 현장의 표시 이름
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Location;

    // 계약 동안 누적된 평가 점수 표시
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_CumulativeScore;

    // 당일 증감과 구분할 현재 보유 크레딧 표시
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_CumulativeCredit;

    // 서버가 계산한 다음 점검 기준 표시
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_NextThreshold;

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
	TObjectPtr<UTextBlock> Txt_Summary_Score;

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
	TObjectPtr<UTextBlock> Txt_Summary_Credit;

	// 점수 영역의 분류별 값과 당일 총점 및 부호 색상 적용
	void SetScoreValues(const FMD_InspectionReport& Report);

	// 크레딧 영역의 분류별 값과 전체 변동 합계 및 부호 색상 적용
	void SetCreditValues(const FMD_InspectionReport& Report);

	// 누락 분류의 영점과 최초 분류 값을 보관하는 고정 크기 조회표
	using FDailyReportValues = TStaticArray<int64, static_cast<uint8>(EMD_DailyReportCategory::Other) + 1>;

	// 한 번의 원장 순회로 새 조회표 구성 및 표시 외 항목을 포함한 총합 반환
	static int64 CollectDailyReportValues(const TArray<FMD_DailyReportValue>& Values, FDailyReportValues& Amounts);

	// 점수와 크레딧에 부호 및 천 단위 구분 적용
	static FText FormatDailyReportAmount(int64 Amount);

	// 정산 숫자의 서식과 부호 색상을 공통 경로로 적용
	void SetAmountText(UTextBlock* AmountTextBlock, int64 Amount);

};
