// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "World/Interaction/MD_DayEndConsole.h"
#include "Player/MD_PlayerController.h"
#include "Components/TextBlock.h"

int32 UMD_DailyReportWidget::FindDailyReportValue(const TArray<FMD_DailyReportValue>& Values, EMD_DailyReportCategory Category)
{
	// 해당 분류가 없을 때 표시할 기본값
	int32 Result = 0;
	// 원장에 저장된 분류별 정산 값 탐색
	for (const FMD_DailyReportValue& Entry : Values)
	{
		// 요청한 분류와 일치하는 값 선택
		if (Entry.Category == Category)
		{
			// 기존 원장의 분류 합계 저장
			Result = Entry.Amount;
			// 분류당 하나인 합계 항목 검색 종료
			break;
		}
	}
	// 조회된 정산 값 반환
	return Result;
}

FText UMD_DailyReportWidget::FormatDailyReportAmount(int32 Amount)
{
	// 시안과 동일한 부호 및 천 단위 구분 설정
	FNumberFormattingOptions Options;
	// 양수와 영점의 증감 부호 표시
	Options.AlwaysSign = true;
	// 큰 수치의 자리 구분 활성화
	Options.UseGrouping = true;
	// 지역화된 정수 표시 문자열 저장
	FText AmountText = FText::AsNumber(Amount, &Options);
	// 완성된 정산 문자열 반환
	return AmountText;
}

void UMD_DailyReportWidget::SetAmountText(UTextBlock* AmountTextBlock, int32 Amount)
{
	// 선택적으로 배치된 정산 숫자 영역의 유효성 확인
	if (IsValid(AmountTextBlock) == false)
	{
		// 숫자 영역이 없는 경우 색상 적용 생략
		return;
	}
	// 모든 정산 숫자에 동일한 부호 및 자리 구분 적용
	AmountTextBlock->SetText(FormatDailyReportAmount(Amount));
	// 해당 숫자의 부호에 맞는 색상을 한 번만 적용
	AmountTextBlock->SetColorAndOpacity(FSlateColor(Amount < 0 ? NegativeAmountTextColor : PositiveAmountTextColor));
}

void UMD_DailyReportWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
    // 풀 재사용으로 남은 버튼 잠금 해제
    SetContinueEnabled(true);
    // 이전 활성화의 기록을 새 응답으로 오인하지 않도록 초기화
    FMD_InspectionReport EmptyReport;
    // 응답 대기 중의 기본 표시 적용
    SetReport(EmptyReport);
    // 점수와 크레딧 요청 API를 제공하는 컨트롤러 확인
    AMD_PlayerController* Controller = Cast<AMD_PlayerController>(GetOwningPlayer());
    if (IsValid(Controller) == true)
    {
        // 활성화마다 중복 응답 구독 방지
        Controller->OnDailyLedgerReceived.RemoveDynamic(this, &UMD_DailyReportWidget::HandleDailyLedgerReceived);
        // 최초 응답 전에 수신 이벤트 연결
        Controller->OnDailyLedgerReceived.AddDynamic(this, &UMD_DailyReportWidget::HandleDailyLedgerReceived);
        // 비활성화 시 응답 구독을 정리할 컨트롤러 보관
        LedgerController = Controller;
        // 서버 점수와 크레딧 기록 요청
        Controller->RequestDailyLedger();
    }
}

void UMD_DailyReportWidget::NativeOnDeactivated()
{
    // 현재 화면의 RPC 응답 구독 대상 조회
    AMD_PlayerController* Controller = LedgerController.Get();
    if (IsValid(Controller) == true)
    {
        // 닫힌 화면으로 전달되는 응답 구독 정리
        Controller->OnDailyLedgerReceived.RemoveDynamic(this, &UMD_DailyReportWidget::HandleDailyLedgerReceived);
        // 서버의 원장 변경 전송과 이전 응답 중단
        Controller->StopDailyLedger();
    }
    // 이전 소유 플레이어 참조 해제
    LedgerController.Reset();
    // 닫힌 보고서가 이전 콘솔 참조를 재사용하지 않도록 정리
    OwningConsole.Reset();
	Super::NativeOnDeactivated();
}

void UMD_DailyReportWidget::SetReport(const FMD_InspectionReport& Report)
{
	// 현재 정산 날짜 부제목 갱신
	if (IsValid(Txt_ReportHeader) == true)
	{
		// 두 자리 날짜를 위한 형식 설정
		FNumberFormattingOptions DayOptions;
		// 날짜의 불필요한 자리 구분 제거
		DayOptions.UseGrouping = false;
		// 한 자리 날짜의 앞자리 영점 표시
		DayOptions.MinimumIntegralDigits = 2;
		// 실제 날짜를 부제목에 반영
		Txt_ReportHeader->SetText(FText::Format(NSLOCTEXT("MDDailyReport", "ReportHeader", "DAY {0} 작업 정산"), FText::AsNumber(Report.Day, &DayOptions)));
	}
    // 마감 원인보다 최종 게임 판정을 우선하는 배지 표시
    if (IsValid(Txt_Badge) == true)
    {
        // 콘솔에서 열어 확인하는 수동 퇴근 문구
        FText BadgeText = NSLOCTEXT("MDDailyReport", "ConsoleBadge", "퇴근 확인");
        // 실패로 확정된 최종 결과 표시
        if (Report.Result == EMD_InspectionResult::GameOver)
        {
            // 시간 종료 및 콘솔 진입과 구분하는 실패 문구
            BadgeText = NSLOCTEXT("MDDailyReport", "GameOverBadge", "게임 오버");
        }
        // 계약 완료를 현재 사이트의 클리어로 표시
        else if (Report.Result == EMD_InspectionResult::ContractCleared)
        {
            // 시간 종료 및 콘솔 진입과 구분하는 성공 문구
            BadgeText = NSLOCTEXT("MDDailyReport", "SiteClearedBadge", "사이트 클리어");
        }
        // 최종 판정이 없는 자동 마감의 원인 표시
        else if (Report.EndReason == EMD_DayEndReason::TimeExpired)
        {
            // 제한 시간 도달에 따른 작업 종료 문구
            BadgeText = NSLOCTEXT("MDDailyReport", "TimeExpiredBadge", "작업 시간 종료");
        }
        // 선택한 상황별 문구를 배지에 적용
        Txt_Badge->SetText(BadgeText);
    }
	// 점수 영역의 분류별 값과 합계 갱신
	SetScoreValues(Report);
	// 크레딧 영역의 분류별 값과 전체 변동 합계 갱신
	SetCreditValues(Report);
	// 계약 종료 판정에 따른 확인 문구 선택
	bool bEndsContract = Report.Result == EMD_InspectionResult::ContractCleared || Report.Result == EMD_InspectionResult::GameOver;
	// 버튼 참조 없이 Blueprint에 결과별 전환 문구 전달
	SetContinueText(bEndsContract == true ? NSLOCTEXT("MDDailyReport", "Confirm", "확인") : NSLOCTEXT("MDDailyReport", "EndDay", "퇴근하기"));
}

void UMD_DailyReportWidget::SetScoreValues(const FMD_InspectionReport& Report)
{
	// 분류 작업으로 발생한 점수 표시
	SetAmountText(Txt_Score_Categorizing, FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Categorizing));
	// 복귀 판정으로 발생한 점수 표시
	SetAmountText(Txt_Score_Return, FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Return));
	// 사고 및 기타 점수 조정 표시
	SetAmountText(Txt_Score_Accident, FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Accident));
	// 퀘스트 완료로 발생한 점수 표시
	SetAmountText(Txt_Score_Quest, FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Quest));
	// 당일 전체 점수 변동 표시
	SetAmountText(Txt_Score_Total, Report.DailyScore);
}

void UMD_DailyReportWidget::SetCreditValues(const FMD_InspectionReport& Report)
{
	// 표시 항목 외 지출도 포함하는 전체 크레딧 변동 합계
	int32 TotalCredits = 0;
	// 서버가 전달한 모든 크레딧 항목 합산
	for (const FMD_DailyReportValue& Entry : Report.DailyCreditTotals)
	{
		// 분류와 무관한 실제 잔액 변동 반영
		TotalCredits += Entry.Amount;
	}
	// 일차별 착수금 표시
	SetAmountText(Txt_Credit_Retainer, FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Retainer));
	// 분류 작업 환급액 표시
	SetAmountText(Txt_Credit_Categorizing, FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Categorizing));
	// 사고 및 기타 금액 조정 표시
	SetAmountText(Txt_Credit_Accident, FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Accident));
	// 점검 평가 보상 표시
	SetAmountText(Txt_Credit_Evaluation, FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Evaluation));
	// 퀘스트 보상 및 지출 표시
	SetAmountText(Txt_Credit_Quest, FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Quest));
	// 당일 전체 크레딧 변동 표시
	SetAmountText(Txt_Credit_Total, TotalCredits);
}

void UMD_DailyReportWidget::SetOwningConsole(AMD_DayEndConsole* InConsole)
{
    // 풀 재사용 시 이전 콘솔 대신 현재 상호작용 대상 저장
    OwningConsole = InConsole;
}

void UMD_DailyReportWidget::RequestContinue()
{
    // 활성 보고서 입력만 하루 마감 요청으로 전달
    if (IsActivated() == false)
    {
        return;
    }
    // 서버 RPC를 보낼 로컬 소유 컨트롤러 조회
    AMD_PlayerController* Controller = Cast<AMD_PlayerController>(GetOwningPlayer());
    // 화면을 연 콘솔의 유효한 참조 조회
    AMD_DayEndConsole* Console = OwningConsole.Get();
    // 요청자와 콘솔이 유효한 경우에만 기존 서버 검증 경로 실행
    if (IsValid(Controller) == true && IsValid(Console) == true)
    {
        // 서버의 정산 및 중복 방지 후 다음 일자 전환 요청
        Controller->ServerRequestEndDay(Console);
    }
}

void UMD_DailyReportWidget::HandleDailyLedgerReceived(FMD_InspectionReport Report)
{
    // 비활성 화면에 도착한 원장 응답 제외
    if (IsActivated() == false)
    {
        return;
    }
    // 기존 숫자 서식과 부호별 색상을 유지한 서버 기록 표시
    SetReport(Report);
}
