// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_DailyReportWidget.generated.h"

class UTextBlock;
class UImage;
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

    // 금액 변동과 전체 지출의 현재 표시값에서 실제 보고서 값까지 변화
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

    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

    virtual bool NativeOnHandleBackAction() override;

	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;

protected:
    // On Report Updated에서 개인별 여섯 그래프 좌표와 공용 합계를 조회할 현재 보고서
    UPROPERTY(BlueprintReadOnly, Category = "MD|Daily Report")
    FMD_InspectionReport ReportData;

    // 서버 표시 시간 동안 실시간 갱신 및 수동 닫기 차단
    bool bShowingFinalReport = false;

    // 서버가 보고서를 닫고 이동을 시작할 월드 시각
    double ReportEndServerTime = 0.0;

    // 같은 초의 남은 시간 문구를 반복 적용하지 않기 위한 캐시
    int32 DisplayedRemainingSeconds = INDEX_NONE;

    // 남은 시간이 다음 정수 초로 바뀔 때 실행할 단발 타이머
    FTimerHandle RemainingTimeTimer;

    // 연출 중 표시값과 구분하여 보관할 실제 전체 지출의 음수 표시값
    int64 SummaryLoss = 0;

    // 개인과 공용 거래를 포함한 실제 전체 변동액
    int64 SummaryCredit = 0;

    // 이번 연출을 시작할 때 지출 텍스트에 표시된 값
    int64 SummaryLossStart = 0;

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

    // 경과 시간에 맞춰 지출과 크레딧 합계를 함께 보간
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

    // 현재 보고서 부제목
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_SubHeader;

    // 최종 보고서의 로비 이동 카운트다운
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_TravelTimer;

    // 서버가 집계한 현장 이름
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Location;

    // 실제 팀 보유 잔액
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_CumulativeCredit;

    // 현재 사용하지 않는 다음 목표 표시
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_NextThreshold;

    // 개인과 공용 거래를 합산한 전체 증감액
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_CreditDelta;

    // 구매와 상환 및 패널티를 포함한 전체 지출
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_CreditLoss;

    // 현재 디자이너에 배치된 그래프 영역
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> BDR_Graph_Grid;

    // 1번 참가자 결과 행
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> HB_Player_1;

    // 1번 참가자 이름
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerName_1;

    // 1번 참가자 누적 순수익
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerResult_1;

    // 1번 그래프에 재사용할 디자이너 범례 색상
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_PlayerPallete_1;

    // 2번 참가자 결과 행
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> HB_Player_2;

    // 2번 참가자 이름
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerName_2;

    // 2번 참가자 누적 순수익
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerResult_2;

    // 2번 그래프에 재사용할 디자이너 범례 색상
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_PlayerPallete_2;

    // 3번 참가자 결과 행
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> HB_Player_3;

    // 3번 참가자 이름
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerName_3;

    // 3번 참가자 누적 순수익
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerResult_3;

    // 3번 그래프에 재사용할 디자이너 범례 색상
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_PlayerPallete_3;

    // 4번 참가자 결과 행
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> HB_Player_4;

    // 4번 참가자 이름
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerName_4;

    // 4번 참가자 누적 순수익
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_PlayerResult_4;

    // 4번 그래프에 재사용할 디자이너 범례 색상
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_PlayerPallete_4;

    // 실제 경과 시간의 1번째 표본 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Time_1;

    // 실제 경과 시간의 2번째 표본 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Time_2;

    // 실제 경과 시간의 3번째 표본 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Time_3;

    // 실제 경과 시간의 4번째 표본 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Time_4;

    // 실제 경과 시간의 5번째 표본 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Time_5;

    // 실제 경과 시간의 6번째 표본 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Time_6;

    // 위에서부터 1번째 크레딧 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Credit_1;

    // 위에서부터 2번째 크레딧 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Credit_2;

    // 위에서부터 3번째 크레딧 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Credit_3;

    // 위에서부터 4번째 크레딧 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Credit_4;

    // 위에서부터 5번째 크레딧 눈금
	UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Axis_Credit_5;

    // 디자이너 배율에 따라 함께 확대되는 그래프 선의 논리 픽셀 두께
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Daily Report|Graph", meta = (ClampMin = "0.0"))
    float GraphLineThickness = 2.0f;

    // 보고서 수신 때만 계산하고 페인트에서는 크기만 적용할 개인별 정규화 좌표
    TStaticArray<TArray<FVector2D>, 4> PlayerGraphPoints;

    // 현재 요약 영역에 사용할 전체 변동액과 지출 목표 갱신
	void SetCreditValues(const FMD_InspectionReport& Report);

    // 개인 결과와 여섯 시간 눈금 및 공통 세로 범위 갱신
    void UpdateCreditHistory();

	// 점수와 크레딧에 부호 및 천 단위 구분 적용
	static FText FormatDailyReportAmount(int64 Amount);

	// 정산 숫자의 서식과 부호 색상을 공통 경로로 적용
	void SetAmountText(UTextBlock* AmountTextBlock, int64 Amount);

};
