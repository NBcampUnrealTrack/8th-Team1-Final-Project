// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "World/Interaction/MD_DayEndConsole.h"
#include "Player/MD_PlayerController.h"
#include "Components/TextBlock.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Internationalization/Internationalization.h"

int64 UMD_DailyReportWidget::CollectDailyReportValues(const TArray<FMD_DailyReportValue>& Values, FDailyReportValues& Amounts)
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
	// 여러 분류의 합계가 개별 원장의 범위를 넘어도 정확한 변동 합계 반환
	return Total;
}

FText UMD_DailyReportWidget::FormatDailyReportAmount(int64 Amount)
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

void UMD_DailyReportWidget::SetAmountText(UTextBlock* AmountTextBlock, int64 Amount)
{
	// 선택적으로 배치된 정산 숫자 영역의 유효성 확인
	if (IsValid(AmountTextBlock) == false)
	{
		// 숫자 영역이 없는 경우 색상 적용 생략
		return;
	}
	// 기존 캐시를 한 번 조회하거나 최초 표시용 빈 항목 생성
	TPair<int64, FText>& CachedAmount = AmountTextCache.FindOrAdd(AmountTextBlock);
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
    // 제거된 화면의 재생 요청이 다음 구성에 남지 않도록 초기화
    bSummaryAnimationRequested = false;
    // Slate 해제 이후의 합계 콜백 차단 및 재구성용 최종 표시 유지
    StopSummaryAnimation();
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
    // 풀에서 재사용하는 화면도 이번 Blueprint 연출 시점까지 대기
    bSummaryAnimationRequested = false;
    // 일반 화면의 활성화 이벤트가 이전 보고서를 연출 대상으로 사용하지 않도록 초기화
    if (bShowingFinalReport == false)
    {
        // 새 서버 응답이 올 때까지 디자이너 또는 이전 표시값 유지
        bHasSummaryTargets = false;
    }
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
    // 닫힌 화면의 이전 요청으로 후속 보고서 연출을 시작하지 않도록 초기화
    bSummaryAnimationRequested = false;
    // 닫힌 화면의 합계 연출 중단 및 실제 값 복원
    StopSummaryAnimation();
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
    // 동일 보고서의 반복 수신으로 진행 중인 연출을 중단하지 않기 위한 이전 점수
    int64 PreviousSummaryScore = SummaryScore;
    // 분류별 항목이 바뀌어도 합계가 같으면 기존 연출을 유지하기 위한 이전 크레딧
    int64 PreviousSummaryCredit = SummaryCredit;
	// 현재 정산 날짜 부제목 갱신
	if (IsValid(Txt_SubHeader) == true)
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
		if (Txt_SubHeader->GetText().IdenticalTo(CachedReportHeader) == false)
		{
			// 현재 날짜의 지역화된 부제목 적용
			Txt_SubHeader->SetText(CachedReportHeader);
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
    // 누적 값과 현장명은 부호 색상을 덮어쓰지 않고 디자이너 스타일 유지
    auto SetSummaryText = [](UTextBlock* TextBlock, FText Text)
    {
        // 선택적 바인딩과 실제 표시 변경 여부 확인
        if (IsValid(TextBlock) == true && TextBlock->GetText().EqualTo(Text) == false)
        {
            // 새 보고서의 표시값 적용
            TextBlock->SetText(Text);
        }
    };
    // 서버 보고서와 동일한 현장 이름 표시
    SetSummaryText(Txt_Location, Report.SiteName);
    // 일일 점수와 구분한 누적 평가 점수 표시
    SetSummaryText(Txt_CumulativeScore, FText::AsNumber(Report.CumulativeScore));
    // 일일 크레딧 변동과 구분한 실제 보유 잔액 표시
    SetSummaryText(Txt_CumulativeCredit, FText::AsNumber(Report.Credits));
    // 다음 점검이 없는 종료 상태에서 기준 대신 대시 표시
    SetSummaryText(Txt_NextThreshold, Report.NextThreshold > 0 ? FText::AsNumber(Report.NextThreshold)
        : NSLOCTEXT("MDDailyReport", "NoNextThreshold", "-"));
	// 점수 영역의 분류별 값과 합계 갱신
	SetScoreValues(Report);
	// 크레딧 영역의 분류별 값과 전체 변동 합계 갱신
	SetCreditValues(Report);
    // 영점 합계도 첫 서버 응답에서 연출하도록 보고서 준비 상태까지 비교
    bool bSummaryTargetsChanged = bHasSummaryTargets != Report.bValid
        || PreviousSummaryScore != SummaryScore || PreviousSummaryCredit != SummaryCredit;
    // 초기화용 보고서 대신 실제 서버 합계가 준비되었는지 보관
    bHasSummaryTargets = Report.bValid;
    // 목표가 달라진 경우에만 기존 연출 예약 교체
    if (bSummaryTargetsChanged == true)
    {
        // 새 목표를 먼저 표시하지 않고 현재 숫자를 다음 연출 시작값으로 보존
        StopSummaryAnimation(false);
        // 일반 보고서는 Blueprint 연출 요청 이후 도착한 응답과 실시간 변경도 반영
        if (bHasSummaryTargets == true && bSummaryAnimationRequested == true && bShowingFinalReport == false)
        {
            // 최근 요청한 재생 시간으로 현재 표시부터 새 합계까지 연출
            AnimateSummaryValues(SummaryAnimationDuration);
        }
    }
    // 비동기 서버 응답과 최종 정산 모두에서 데이터 준비 완료 알림
    BP_OnReportUpdated(bShowingFinalReport);
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
	SetAmountText(Txt_Score_Accident, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Accident)]
		+ Amounts[static_cast<uint8>(EMD_DailyReportCategory::Other)]);
	// 퀘스트 완료로 발생한 점수 표시
	SetAmountText(Txt_Score_Quest, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Quest)]);
    // 현재 표시와 구분하여 다음 연출의 실제 점수 목표 보관
    SummaryScore = Report.DailyScore;
}

void UMD_DailyReportWidget::SetCreditValues(const FMD_InspectionReport& Report)
{
	// 누락 분류를 영점으로 표시하는 스택 기반 크레딧 조회표
	FDailyReportValues Amounts{};
    // 분류별 조회표와 실제 합계를 같은 순회에서 구성하여 연출 목표로 보관
    SummaryCredit = CollectDailyReportValues(Report.DailyCreditTotals, Amounts);
	// 일차별 착수금 표시
	SetAmountText(Txt_Credit_Retainer, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Retainer)]);
	// 분류 작업 환급액 표시
	SetAmountText(Txt_Credit_Categorizing, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Categorizing)]);
	// 사고 및 기타 금액 조정 표시
	SetAmountText(Txt_Credit_Accident, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Accident)]
		+ Amounts[static_cast<uint8>(EMD_DailyReportCategory::Other)]);
	// 점검 평가 보상 표시
	SetAmountText(Txt_Credit_Evaluation, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Evaluation)]);
	// 퀘스트 보상 및 지출 표시
	SetAmountText(Txt_Credit_Quest, Amounts[static_cast<uint8>(EMD_DailyReportCategory::Quest)]);
}

int64 UMD_DailyReportWidget::ReadSummaryAmount(UTextBlock* AmountTextBlock, int64 FallbackAmount)
{
    // 선택적 표시 영역이 없으면 해당 합계의 불필요한 보간 생략
    if (IsValid(AmountTextBlock) == false)
    {
        return FallbackAmount;
    }
    // 캐시 비교와 직접 입력값 해석에 사용할 실제 표시 텍스트
    FText CurrentText = AmountTextBlock->GetText();
    // 기존 숫자 서식으로 표시한 값은 문자열 변환 없이 정확한 정수 재사용
    TPair<int64, FText>* CachedAmount = AmountTextCache.Find(AmountTextBlock);
    // BP에서 텍스트를 바꾼 경우에는 이전 캐시 사용 방지
    if (CachedAmount != nullptr && CurrentText.IdenticalTo(CachedAmount->Value) == true)
    {
        return CachedAmount->Key;
    }
    // 디자이너 또는 BP가 지정한 숫자 양끝의 여백 제거
    FString NumberString = CurrentText.ToString().TrimStartAndEnd();
    // 전체 문자열이 숫자인지 확인할 해석 길이
    int32 ParsedLength = 0;
    // 표시값을 손실 없이 받을 정수 저장 공간
    int64 Amount = 0;
    // 부호와 자리 구분을 허용하는 정수 해석 옵션
    FNumberParsingOptions Options;
    // 쉼표 등 현재 지역의 자리 구분 기호 지원
    Options.UseGrouping = true;
    // 정수 범위를 벗어나는 표시값의 넘침 차단
    Options.InsideLimits = true;
    // 잘못된 값을 최댓값으로 바꾸지 않고 해석 실패로 처리
    Options.UseClamping = false;
    // FText 숫자 출력과 같은 지역 규칙으로 전체 정수 해석
    if (FastDecimalFormat::StringToNumber(*NumberString, NumberString.Len(),
        FInternationalization::Get().GetCurrentLocale()->GetDecimalNumberFormattingRules(), Options, Amount, &ParsedLength) == true
        && ParsedLength == NumberString.Len())
    {
        return Amount;
    }
    // 빈 문구나 숫자가 아닌 표시에서는 임의의 영점 대신 실제 합계 사용
    return FallbackAmount;
}

void UMD_DailyReportWidget::AnimateSummaryValues(float Duration)
{
    // 서버 응답보다 먼저 호출되거나 실시간 원장이 변경되어도 재생 요청 유지
    bSummaryAnimationRequested = true;
    // 지연 응답과 후속 갱신에도 Blueprint에서 지정한 재생 시간 사용
    SummaryAnimationDuration = Duration;
    // 재호출 시 현재 표시를 덮어쓰지 않고 이전 예약만 정리
    StopSummaryAnimation(false);
    // 초기화용 영점으로 연출하지 않고 실제 보고서 수신까지 표시값 보존
    if (bHasSummaryTargets == false)
    {
        return;
    }
    // 타이머와 경과 시간에 사용할 동일 월드 조회
    UWorld* World = GetWorld();
    // 연출이 불가능한 조건에서는 실제 합계로 즉시 마무리
    if (FMath::IsFinite(Duration) == false || Duration <= 0.0f || IsValid(World) == false)
    {
        // 잘못된 시간이나 해제된 월드에서 중간 표시가 남지 않도록 정리
        StopSummaryAnimation();
        return;
    }
    // 함수가 호출된 순간의 실제 점수 표시를 보간 시작값으로 저장
    SummaryScoreStart = ReadSummaryAmount(Txt_Summary_Score, SummaryScore);
    // 함수가 호출된 순간의 실제 크레딧 표시를 보간 시작값으로 저장
    SummaryCreditStart = ReadSummaryAmount(Txt_Summary_Credit, SummaryCredit);
    // 목표가 영점이어도 현재 값이 다르면 연출하고 동일한 두 값만 생략
    if (SummaryScoreStart == SummaryScore && SummaryCreditStart == SummaryCredit)
    {
        // 동일한 합계의 숫자 서식과 색상만 복원하고 반복 예약 생략
        StopSummaryAnimation();
        return;
    }
    // 읽어 온 점수 시작값에 기존 숫자 서식과 부호 색상 적용
    SetAmountText(Txt_Summary_Score, SummaryScoreStart);
    // 읽어 온 크레딧 시작값에 기존 숫자 서식과 부호 색상 적용
    SetAmountText(Txt_Summary_Credit, SummaryCreditStart);
    // 프레임 지연과 무관하게 전체 진행률을 계산할 시작 시각 저장
    SummaryAnimationStartTime = World->GetTimeSeconds();
    // 타이머 누적 호출 없이 프레임당 한 번만 숫자를 갱신하는 설정
    FTimerManagerTimerParameters TimerParameters;
    // 목표 시각까지 반복 갱신 예약
    TimerParameters.bLoop = true;
    // 낮은 프레임률에서 불필요한 따라잡기 콜백 방지
    TimerParameters.bMaxOncePerFrame = true;
    // 짧은 연출도 지정한 시간부터 완료를 확인하도록 갱신 간격 제한
    World->GetTimerManager().SetTimer(SummaryAnimationTimer, this, &UMD_DailyReportWidget::UpdateSummaryAnimation,
        FMath::Min(Duration, 1.0f / 60.0f), TimerParameters);
}

void UMD_DailyReportWidget::UpdateSummaryAnimation()
{
    // 시작 시각과 동일한 기준의 현재 월드 조회
    UWorld* World = GetWorld();
    // 표시 대상이나 시간 기준이 사라진 연출 정리
    if (IsValid(World) == false || SummaryAnimationDuration <= 0.0f
        || (IsValid(Txt_Summary_Score) == false && IsValid(Txt_Summary_Credit) == false))
    {
        // 남아 있는 타이머 해제 및 실제 합계 복원
        StopSummaryAnimation();
        return;
    }
    // 콜백 횟수 대신 실제 경과 시간으로 두 합계의 공통 진행률 계산
    double Alpha = FMath::Clamp((World->GetTimeSeconds() - SummaryAnimationStartTime) / SummaryAnimationDuration, 0.0, 1.0);
    // 목표 시각 이후에는 부동소수점 보간 대신 원본 정수 사용
    if (Alpha >= 1.0)
    {
        // 최종 합계의 정확한 표시 및 반복 예약 종료
        StopSummaryAnimation();
        return;
    }
    // 정수끼리의 차감 넘침 없이 현재 표시와 목표값을 보간하는 처리
    auto InterpolateAmount = [Alpha](int64 Start, int64 Target)
    {
        // 부호가 달라도 정수 범위 밖의 중간 차감을 만들지 않는 실수 보간
        double Value = FMath::LerpStable(static_cast<double>(Start), static_cast<double>(Target), Alpha);
        // 최댓값의 실수 반올림으로 정수 변환 범위를 초과하는 경우 보호
        if (Value >= static_cast<double>(MAX_int64))
        {
            return MAX_int64;
        }
        // 최솟값 경계에서도 안전한 정수 표시 보장
        if (Value <= static_cast<double>(MIN_int64))
        {
            return MIN_int64;
        }
        // 기존 정수 단위 표시 방식 유지
        return FMath::TruncToInt64(Value);
    };
    // 현재 표시에서 실제 점수까지 증가 또는 감소
    SetAmountText(Txt_Summary_Score, InterpolateAmount(SummaryScoreStart, SummaryScore));
    // 현재 표시에서 실제 크레딧까지 같은 진행률로 변화
    SetAmountText(Txt_Summary_Credit, InterpolateAmount(SummaryCreditStart, SummaryCredit));
}

void UMD_DailyReportWidget::StopSummaryAnimation(bool bApplyFinalValues)
{
    // 위젯 수명이 끝나는 중에도 유효한 월드에서만 예약 해제
    UWorld* World = GetWorld();
    if (IsValid(World) == true)
    {
        // 이동 카운트다운과 구분하여 합계 연출 타이머만 정리
        World->GetTimerManager().ClearTimer(SummaryAnimationTimer);
    }
    // 월드가 없는 경우에도 이전 예약 핸들 재사용 방지
    SummaryAnimationTimer.Invalidate();
    // 재시작과 보고서 교체에서는 표시값을 보존하고 완료 시에만 목표 적용
    if (bApplyFinalValues == true)
    {
        // 표시 캐시와 부호 색상을 유지한 정확한 최종 점수 적용
        SetAmountText(Txt_Summary_Score, SummaryScore);
        // 보간 오차 없이 서버 기록에서 계산한 실제 크레딧 적용
        SetAmountText(Txt_Summary_Credit, SummaryCredit);
    }
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

void UMD_DailyReportWidget::RequestResume()
{
    // 확정 보고서나 이미 닫힌 화면의 잔업 입력 차단
    if (IsActivated() == false || bShowingFinalReport == true)
    {
        // 서버의 최종 정산 표시 시간 유지
        return;
    }
    // 활성 콘솔 확인 화면을 닫고 작업 입력 복원
    DeactivateWidget();
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
