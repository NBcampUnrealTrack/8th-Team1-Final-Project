// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "World/Interaction/MD_DayEndConsole.h"
#include "Player/MD_PlayerController.h"
#include "Components/TextBlock.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

int32 UMD_DailyReportWidget::CollectDailyReportValues(const TArray<FMD_DailyReportValue>& Values, FDailyReportValues& Amounts)
{
	// 합계를 사용하지 않는 점수 원장도 정수 넘침 없이 순회하기 위한 누적값
	int64 Total = 0;
	// 중복 분류의 첫 항목이 최종 값으로 남도록 역순 순회
	for (int32 Index = Values.Num() - 1; Index >= 0; --Index)
	{
		// 조회표와 합계에서 함께 사용할 현재 원장 항목
		const FMD_DailyReportValue& Entry = Values[Index];
		// 표시 여부와 관계없이 모든 항목의 실제 변동 합산
		Total += Entry.Amount;
		// 동적 할당 없이 분류별 슬롯을 조회할 인덱스
		int32 CategoryIndex = static_cast<uint8>(Entry.Category);
		// 조회표 범위에 포함되는 정의된 분류만 기록
		if (CategoryIndex < Amounts.Num())
		{
			// 앞선 항목의 값으로 덮어써 최초 값 선택 유지
			Amounts[CategoryIndex] = Entry.Amount;
		}
	}
	// 기존 정산 숫자 형식에 맞춘 크레딧 전체 변동 합계 반환
	return static_cast<int32>(Total);
}

FText UMD_DailyReportWidget::FormatDailyReportAmount(int32 Amount)
{
	// 시안과 동일한 부호 및 천 단위 구분 설정
	FNumberFormattingOptions Options;
	// 양수와 영점의 증감 부호 표시
	Options.AlwaysSign = true;
	// 큰 수치의 자리 구분 활성화
	Options.UseGrouping = true;
	// 지역화된 정수 표시 문자열을 호출부에 직접 반환
	return FText::AsNumber(Amount, &Options);
}

void UMD_DailyReportWidget::SetAmountText(UTextBlock* AmountTextBlock, int32 Amount)
{
	// 선택적으로 배치된 정산 숫자 영역의 유효성 확인
	if (IsValid(AmountTextBlock) == false)
	{
		// 숫자 영역이 없는 경우 색상 적용 생략
		return;
	}
	// 기존 캐시를 한 번 조회하거나 최초 표시용 빈 항목 생성
	TPair<int32, FText>& CachedAmount = AmountTextCache.FindOrAdd(AmountTextBlock);
	// 최초 표시 또는 실제 금액 변경에만 지역화된 숫자 생성
	if (CachedAmount.Value.IsEmpty() == true || CachedAmount.Key != Amount)
	{
		// 해시 재검색 없이 현재 항목의 금액 갱신
		CachedAmount.Key = Amount;
		// 같은 캐시 항목에 새 금액의 지역화 문자열 보관
		CachedAmount.Value = FormatDailyReportAmount(Amount);
	}
	// 동일한 텍스트의 반복 적용 방지 및 외부에서 바뀐 표시 복원
	if (AmountTextBlock->GetText().IdenticalTo(CachedAmount.Value) == false)
	{
		// 값이 유지되는 경우 기존 숫자 서식의 지역화 정보 재사용
		AmountTextBlock->SetText(CachedAmount.Value);
	}
	// 현재 색상 설정과 숫자 부호를 함께 반영하는 표시 색상
	FSlateColor AmountColor(Amount < 0 ? NegativeAmountTextColor : PositiveAmountTextColor);
	// 숫자가 같아도 Blueprint에서 변경한 색상 설정 반영
	if (AmountTextBlock->GetColorAndOpacity() != AmountColor)
	{
		// 실제 색상 변경이 있는 영역만 갱신
		AmountTextBlock->SetColorAndOpacity(AmountColor);
	}
}

void UMD_DailyReportWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // 확정 보고서의 Slate 재구성 이후 남은 시간 갱신 재개
    if (bShowingFinalReport == true)
    {
        // 재구성 중 경과한 서버 시간을 즉시 반영
        UpdateRemainingTime();
    }
}

void UMD_DailyReportWidget::NativeDestruct()
{
    // 화면 제거 이후 실행될 카운트다운 예약 해제
    StopRemainingTimeUpdates();
    // 비활성화 콜백 없이 제거되는 화면의 서버 구독 정리
    StopLedgerUpdates();
    // 해제된 숫자 영역과 표시 문자열의 캐시 정리
    AmountTextCache.Reset();
    // 다음 구성에서 날짜 부제목의 최초 생성 허용
    CachedReportDay.Reset();
    // 사용이 끝난 지역화 부제목 참조 해제
    CachedReportHeader = FText::GetEmpty();
    Super::NativeDestruct();
}

void UMD_DailyReportWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
    // 서버 확정 화면은 초기화 및 별도 원장 조회 없이 유지
    if (bShowingFinalReport == true)
    {
        // 활성화 이벤트 이후에도 두 버튼 입력 잠금 유지
        SetContinueEnabled(false);
        // 활성화 이벤트 이후에도 이동 대기 버튼 숨김 유지
        SetContinueVisibility(ESlateVisibility::Collapsed);
        // 활성화 시점의 서버 시간에 맞춰 카운트다운 예약 갱신
        UpdateRemainingTime();
        return;
    }
    // 마감 전 확인 화면에는 이전 카운트다운 표시 제거
    if (IsValid(Txt_TravelTimer) == true)
    {
        // 풀에 남은 이전 보고서의 이동 문구 초기화
        Txt_TravelTimer->SetText(FText::GetEmpty());
        // 카운트다운 시작 전 전용 표시 영역 숨김
        Txt_TravelTimer->SetVisibility(ESlateVisibility::Collapsed);
    }
    // 콘솔을 통해 연 확인 화면에만 잔업 버튼 표시
    SetResumeVisibility(OwningConsole.IsValid() == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    // 일반 확인 화면으로 재사용할 때 퇴근 버튼 표시 복원
    SetContinueVisibility(ESlateVisibility::Visible);
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
        // 최초 응답 전에 중복 없는 수신 이벤트 연결
        Controller->OnDailyLedgerReceived.AddUniqueDynamic(this, &UMD_DailyReportWidget::HandleDailyLedgerReceived);
        // 서버 점수와 크레딧 기록 요청
        Controller->RequestDailyLedger();
    }
}

void UMD_DailyReportWidget::NativeOnDeactivated()
{
    // 풀에 반환된 화면의 카운트다운 실행 중단
    StopRemainingTimeUpdates();
    // 화면 수명에 맞춰 실시간 원장 응답 정리
    StopLedgerUpdates();
    // 풀 재사용 시 일반 콘솔 보고서로 초기화
    bShowingFinalReport = false;
    // 닫힌 보고서가 이전 콘솔 참조를 재사용하지 않도록 정리
    OwningConsole.Reset();
	Super::NativeOnDeactivated();
}

void UMD_DailyReportWidget::SetReport(const FMD_InspectionReport& Report)
{
	// 현재 정산 날짜 부제목 갱신
	if (IsValid(Txt_ReportHeader) == true)
	{
		// 날짜가 달라진 경우에만 숫자 서식 및 부제목 생성
		if (CachedReportDay.IsSet() == false || CachedReportDay.GetValue() != Report.Day)
		{
			// 두 자리 날짜를 위한 형식 설정
			FNumberFormattingOptions DayOptions;
			// 날짜의 불필요한 자리 구분 제거
			DayOptions.UseGrouping = false;
			// 한 자리 날짜의 앞자리 영점 표시
			DayOptions.MinimumIntegralDigits = 2;
			// 동일 날짜의 후속 원장 갱신에서 재사용할 부제목 생성
			CachedReportHeader = FText::Format(NSLOCTEXT("MDDailyReport", "ReportHeader", "DAY {0} 작업 정산"), FText::AsNumber(Report.Day, &DayOptions));
			// 생성된 부제목의 날짜 기록
			CachedReportDay = Report.Day;
		}
		// 실제 표시가 달라진 경우에만 부제목 갱신
		if (Txt_ReportHeader->GetText().IdenticalTo(CachedReportHeader) == false)
		{
			// 현재 날짜의 지역화된 부제목 적용
			Txt_ReportHeader->SetText(CachedReportHeader);
		}
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
        // 같은 배지 문구의 반복 적용 방지
        if (Txt_Badge->GetText().IdenticalTo(BadgeText) == false)
        {
            // 실제로 달라진 상황별 문구만 배지에 적용
            Txt_Badge->SetText(BadgeText);
        }
    }
	// 점수 영역의 분류별 값과 합계 갱신
	SetScoreValues(Report);
	// 크레딧 영역의 분류별 값과 전체 변동 합계 갱신
	SetCreditValues(Report);
}

void UMD_DailyReportWidget::SetScoreValues(const FMD_InspectionReport& Report)
{
	// 누락 분류를 영점으로 표시하는 스택 기반 점수 조회표
	FDailyReportValues Amounts{};
	// 점수 배열을 한 번만 읽어 분류별 표시 값 구성
	CollectDailyReportValues(Report.DailyScoreTotals, Amounts);
	// 분류 작업으로 발생한 점수 표시
	SetAmountText(Txt_Score_Categorizing, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Categorizing)]);
	// 복귀 판정으로 발생한 점수 표시
	SetAmountText(Txt_Score_Return, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Return)]);
	// 사고 및 기타 점수 조정 표시
	SetAmountText(Txt_Score_Accident, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Accident)]);
	// 퀘스트 완료로 발생한 점수 표시
	SetAmountText(Txt_Score_Quest, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Quest)]);
	// 당일 전체 점수 변동 표시
	SetAmountText(Txt_Score_Total, Report.DailyScore);
}

void UMD_DailyReportWidget::SetCreditValues(const FMD_InspectionReport& Report)
{
	// 누락 분류를 영점으로 표시하는 스택 기반 크레딧 조회표
	FDailyReportValues Amounts{};
	// 분류별 조회표와 전체 변동 합계를 같은 순회에서 구성
	int32 TotalCredits = CollectDailyReportValues(Report.DailyCreditTotals, Amounts);
	// 일차별 착수금 표시
	SetAmountText(Txt_Credit_Retainer, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Retainer)]);
	// 분류 작업 환급액 표시
	SetAmountText(Txt_Credit_Categorizing, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Categorizing)]);
	// 사고 및 기타 금액 조정 표시
	SetAmountText(Txt_Credit_Accident, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Accident)]);
	// 점검 평가 보상 표시
	SetAmountText(Txt_Credit_Evaluation, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Evaluation)]);
	// 퀘스트 보상 및 지출 표시
	SetAmountText(Txt_Credit_Quest, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Quest)]);
	// 당일 전체 크레딧 변동 표시
	SetAmountText(Txt_Credit_Total, TotalCredits);
}

void UMD_DailyReportWidget::SetOwningConsole(AMD_DayEndConsole* InConsole)
{
    // 풀 재사용 시 이전 콘솔 대신 현재 상호작용 대상 저장
    OwningConsole = InConsole;
    // 활성 화면 재사용 시에도 콘솔 진입 여부에 맞는 버튼 표시
    SetResumeVisibility(OwningConsole.IsValid() == true && bShowingFinalReport == false ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMD_DailyReportWidget::RequestContinue()
{
    // 활성 보고서 입력만 하루 마감 요청으로 전달
    if (IsActivated() == false || bShowingFinalReport == true)
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
    if (IsActivated() == false || bShowingFinalReport == true)
    {
        return;
    }
    // 기존 숫자 서식과 부호별 색상을 유지한 서버 기록 표시
    SetReport(Report);
}

void UMD_DailyReportWidget::StopLedgerUpdates()
{
    // 현재 화면의 RPC 응답 구독 대상 조회
    AMD_PlayerController* Controller = Cast<AMD_PlayerController>(GetOwningPlayer());
    if (IsValid(Controller) == true && Controller->OnDailyLedgerReceived.IsAlreadyBound(this, &UMD_DailyReportWidget::HandleDailyLedgerReceived) == true)
    {
        // 닫힌 화면으로 전달되는 응답 구독 정리
        Controller->OnDailyLedgerReceived.RemoveDynamic(this, &UMD_DailyReportWidget::HandleDailyLedgerReceived);
        // 서버의 원장 변경 전송과 이전 응답 중단
        Controller->StopDailyLedger();
    }
}

void UMD_DailyReportWidget::SetFinalReport(FMD_InspectionReport& Report, double EndServerTime)
{
    // 최종 정산을 서버 표시 시간이 끝날 때까지 고정
    bShowingFinalReport = true;
    // 네트워크 전달 지연을 포함하는 서버 종료 시각 보관
    ReportEndServerTime = EndServerTime;
    // 새 확정 보고서의 최초 카운트다운 표시 허용
    DisplayedRemainingSeconds = INDEX_NONE;
    // 최종 정산에서는 잔업 버튼과 차지하던 공간 제거
    SetResumeVisibility(ESlateVisibility::Collapsed);
    // 이미 열린 콘솔 화면의 실시간 구독 종료
    StopLedgerUpdates();
    // 다시 마감을 요청할 콘솔 연결 해제
    OwningConsole.Reset();
    // 모든 플레이어가 동일한 서버 정산 값 표시
    SetReport(Report);
    // 퇴근 및 잔업 버튼의 수동 입력 차단
    SetContinueEnabled(false);
    // 이동 대기 중 퇴근 버튼과 차지하던 공간 제거
    SetContinueVisibility(ESlateVisibility::Collapsed);
    // 확정 보고서에서만 남은 시간 영역 표시
    if (IsValid(Txt_TravelTimer) == true)
    {
        // 전용 텍스트 영역의 레이아웃 복원
        Txt_TravelTimer->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    // 첫 프레임부터 실제 남은 시간 표시
    UpdateRemainingTime();
}

bool UMD_DailyReportWidget::NativeOnHandleBackAction()
{
    // 5초 자동 종료 전에는 뒤로가기를 소비하여 화면 유지
    if (bShowingFinalReport == true)
    {
        return true;
    }
    return Super::NativeOnHandleBackAction();
}

void UMD_DailyReportWidget::StopRemainingTimeUpdates()
{
    // 월드가 정리되는 중에도 안전하게 타이머 관리자 접근
    UWorld* World = GetWorld();
    if (IsValid(World) == true)
    {
        // 이 보고서가 예약한 단발 타이머만 해제
        World->GetTimerManager().ClearTimer(RemainingTimeTimer);
    }
}

void UMD_DailyReportWidget::UpdateRemainingTime()
{
    // 타이머 예약 및 서버 시각 조회에 사용할 월드
    UWorld* World = GetWorld();
    // 해제된 월드의 타이머 관리자 접근 방지
    if (IsValid(World) == false)
    {
        return;
    }
    // 종료 및 재예약에 사용할 타이머 관리자 한 번 조회
    FTimerManager& TimerManager = World->GetTimerManager();
    // 확정 보고서와 전용 시간 영역이 유효한 동안만 갱신
    if (bShowingFinalReport == false || IsValid(Txt_TravelTimer) == false)
    {
        // 표시할 대상이 사라진 경우 기존 예약 정리
        TimerManager.ClearTimer(RemainingTimeTimer);
        return;
    }
    // 서버와 동기화된 월드 시각을 제공하는 게임 상태 조회
    AGameStateBase* State = World->GetGameState();
    // 게임 상태가 없는 검증 환경에서는 현재 월드 시각 사용
    double ServerTime = IsValid(State) == true ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
    // 콜백 지연을 누적하지 않고 서버 종료 시각에서 매번 남은 시간 계산
    double RemainingTime = FMath::Max(0.0, ReportEndServerTime - ServerTime);
    // 종료 직전까지 올림 표시하고 지연된 종료 응답에서는 영점 유지
    int32 RemainingSeconds = FMath::CeilToInt(RemainingTime);
    // 표시할 초가 달라진 경우에만 남은 시간 텍스트 갱신
    if (DisplayedRemainingSeconds != RemainingSeconds)
    {
        // 같은 초에 다시 예약되는 경우의 중복 갱신 방지
        DisplayedRemainingSeconds = RemainingSeconds;
        // 남은 시간을 지정된 전용 텍스트 영역에 반영
        Txt_TravelTimer->SetText(FText::Format(NSLOCTEXT("MDDailyReport", "RemainingTime", "{0}초 후 이동"), FText::AsNumber(RemainingSeconds)));
    }
    // 영점 도달 후에는 서버 종료 신호까지 추가 타이머 없이 표시 유지
    if (RemainingSeconds > 0)
    {
        // 다음 표시 초의 경계 예약 및 부동소수점 오차에 의한 즉시 재호출 방지
        float NextUpdateDelay = static_cast<float>(FMath::Max(0.01, RemainingTime - (RemainingSeconds - 1)));
        // 엔진의 기존 예약 교체 기능으로 다음 표시 시점에 한 번만 실행
        TimerManager.SetTimer(RemainingTimeTimer, this, &UMD_DailyReportWidget::UpdateRemainingTime, NextUpdateDelay, false);
    }
    else
    {
        // 영점 도달 시 남아 있는 예약만 정리
        TimerManager.ClearTimer(RemainingTimeTimer);
    }
}

void UMD_DailyReportWidget::CloseFinalReport()
{
    // 일반 콘솔 확인 화면을 제외한 서버 확정 보고서만 종료
    if (bShowingFinalReport == true)
    {
        // Common UI 활성화 수명에 따른 화면 및 구독 정리
        DeactivateWidget();
    }
}
