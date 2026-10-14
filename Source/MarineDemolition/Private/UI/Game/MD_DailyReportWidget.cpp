// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "World/Interaction/MD_DayEndConsole.h"
#include "Player/MD_PlayerController.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Rendering/DrawElements.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Internationalization/Internationalization.h"

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
    Super::NativeDestruct();
}

void UMD_DailyReportWidget::NativeOnActivated()
{
	MDAudioFeedback::UI(this, TEXT("SFX.Mothership.Report.Open"));
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
        // 활성화 전에 수신한 최종 합계의 누락된 연출 요청 보완
        if (bHasSummaryTargets == true && bSummaryAnimationRequested == false)
        {
            // 활성 상태를 요구하는 기존 Blueprint 정산 연출 연결
            BP_OnReportUpdated(true);
        }
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
    // 기존 BP 갱신 이벤트보다 먼저 서버의 시간별·개인별 이력 보관
    ReportData = Report;
    // 동일 보고서의 반복 수신으로 진행 중인 연출을 중단하지 않기 위한 이전 지출
    int64 PreviousSummaryLoss = SummaryLoss;
    // 분류별 항목이 바뀌어도 합계가 같으면 기존 연출을 유지하기 위한 이전 크레딧
    int64 PreviousSummaryCredit = SummaryCredit;
	// 정산 부제목 (일차 개념 없음)
	if (IsValid(Txt_SubHeader) == true)
	{
		const FText Header = NSLOCTEXT("MDDailyReport", "ReportHeader", "작업 정산");
		if (Txt_SubHeader->GetText().IdenticalTo(Header) == false)
		{
			Txt_SubHeader->SetText(Header);
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
    // 일일 크레딧 변동과 구분한 실제 보유 잔액 표시
    SetSummaryText(Txt_CumulativeCredit, FText::AsNumber(Report.Credits));
    // 점검 목표가 사라져 기준 영역은 대시 표시 (정리 달성률 표시는 분모 확정 후)
    SetSummaryText(Txt_NextThreshold, NSLOCTEXT("MDDailyReport", "NoNextThreshold", "-"));
	// 현재 UI의 전체 변동 및 지출 연출 목표 갱신
	SetCreditValues(Report);
    // 개인 기록과 실제 조회 시각을 현재 그래프에 반영
    UpdateCreditHistory();
    // 영점 합계도 첫 서버 응답에서 연출하도록 보고서 준비 상태까지 비교
    bool bSummaryTargetsChanged = bHasSummaryTargets != Report.bValid
        || PreviousSummaryLoss != SummaryLoss || PreviousSummaryCredit != SummaryCredit;
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
    // 별도 합계 연출이 없는 일반 보고서의 실제 금액 표시
    if (bHasSummaryTargets == true && bShowingFinalReport == false && bSummaryAnimationRequested == false)
    {
        // 서버 집계와 동일한 합계 및 부호별 서식 적용
        StopSummaryAnimation();
    }
}

void UMD_DailyReportWidget::SetCreditValues(const FMD_InspectionReport& Report)
{
    // 팀 공용 금액까지 포함된 실제 전체 증감액을 그대로 사용
    SummaryCredit = Report.TotalCreditDelta;
    // 합계 상쇄 이전의 전체 지출을 음수로 표시
    SummaryLoss = -Report.TotalCreditsSpent;
    // 초기 응답 대기 및 풀 재사용에서 이전 금액 표시 제거
    if (Report.bValid == false)
    {
        // 증감 없는 기본 금액 표시
        SetAmountText(Txt_CreditDelta, 0);
        // 이전 계약의 지출 표시 초기화
        SetAmountText(Txt_CreditLoss, 0);
    }
}

void UMD_DailyReportWidget::UpdateCreditHistory()
{
    // 표시 시점과 무관하게 영점에서 시작하는 유효한 전체 기록 구간
    double Duration = FMath::IsFinite(ReportData.CreditHistoryDuration) == true ? FMath::Max(0.0, ReportData.CreditHistoryDuration) : 0.0;
    // 현재 배치 순서와 서버의 고정 개인 인덱스 대응
    UWidget* Rows[] = { HB_Player_1, HB_Player_2, HB_Player_3, HB_Player_4 };
    // 표시 이름과 금액에 같은 개인 인덱스 적용
    UTextBlock* Names[] = { Txt_PlayerName_1, Txt_PlayerName_2, Txt_PlayerName_3, Txt_PlayerName_4 };
    // 개인 순수익의 색상은 디자이너 범례 색상 유지
    UTextBlock* Results[] = { Txt_PlayerResult_1, Txt_PlayerResult_2, Txt_PlayerResult_3, Txt_PlayerResult_4 };
    // 여섯 시점에 대응하는 현재 시간축 텍스트
    UTextBlock* Times[] = { Txt_Axis_Time_1, Txt_Axis_Time_2, Txt_Axis_Time_3, Txt_Axis_Time_4, Txt_Axis_Time_5, Txt_Axis_Time_6 };
    // 위에서 아래로 배치된 공통 크레딧 눈금
    UTextBlock* Credits[] = { Txt_Axis_Credit_1, Txt_Axis_Credit_2, Txt_Axis_Credit_3, Txt_Axis_Credit_4, Txt_Axis_Credit_5 };
    // 하단 눈금을 정수 초로 표시하기 위한 숫자 서식
    FNumberFormattingOptions TimeFormat;
    // 시간축 라벨의 소수점 이하 표시 제거
    TimeFormat.MaximumFractionalDigits = 0;
    // 모든 개인 그래프에서 영점이 포함되는 공통 범위 초기화
    double MinimumCredit = 0.0;
    // 수입과 손실을 같은 좌표계로 비교할 최대값
    double MaximumCredit = 0.0;
    // 빈 참가자와 실제 영점 참가자를 구분하는 결과 갱신
    for (int32 Index = 0; Index < PlayerGraphPoints.Num(); ++Index)
    {
        // 이전 보고서의 선 좌표 제거 및 메모리 재사용
        PlayerGraphPoints[Index].Reset();
        // 실제 참가자가 존재하는 슬롯만 표시
        bool bHasPlayer = ReportData.PlayerCreditHistories.IsValidIndex(Index) == true
            && ReportData.PlayerCreditHistories[Index].bHasPlayer == true;
        // 빈 슬롯은 레이아웃에서 제외
        if (IsValid(Rows[Index]) == true)
        {
            // 풀 재사용이나 인원 변화에도 개인 행 표시 복원
            Rows[Index]->SetVisibility(bHasPlayer == true ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        }
        // 사라진 참가자의 이름도 남지 않도록 초기화
        if (IsValid(Names[Index]) == true)
        {
            // 서버가 보낸 개인 표시 이름 적용
            Names[Index]->SetText(bHasPlayer == true ? FText::FromString(ReportData.PlayerCreditHistories[Index].PlayerName) : FText::GetEmpty());
        }
        // 영점 참가자도 실제 누적 순수익 표시
        if (IsValid(Results[Index]) == true)
        {
            // 부호 서식만 적용하여 개인별 범례 색상 보존
            Results[Index]->SetText(bHasPlayer == true ? FormatDailyReportAmount(ReportData.PlayerCreditHistories[Index].NetCredits) : FText::GetEmpty());
        }
        // 존재하지 않는 참가자의 영점 선 생성 방지
        if (bHasPlayer == false)
        {
            // 현재 항목을 제외하고 다음 유효 기록으로 진행
            continue;
        }
        // 표시할 여섯 표본만으로 전체 그래프 범위 계산
        for (FVector2D Sample : ReportData.PlayerCreditHistories[Index].Samples)
        {
            // 잘못된 좌표의 Slate 전파 방지
            if (FMath::IsFinite(Sample.X) == false || FMath::IsFinite(Sample.Y) == false)
            {
                // 현재 항목을 제외하고 다음 유효 기록으로 진행
                continue;
            }
            // 보고서 수신 이후 페인트에서 원본 배열 재조회 생략
            PlayerGraphPoints[Index].Add(Sample);
            // 가장 낮은 개인 손실까지 포함
            MinimumCredit = FMath::Min(MinimumCredit, Sample.Y);
            // 가장 높은 개인 수익까지 포함
            MaximumCredit = FMath::Max(MaximumCredit, Sample.Y);
        }
    }
    // 모든 참가자가 영점인 경우에도 영점 선을 중앙에 표시
    if (MinimumCredit == MaximumCredit)
    {
        // 영점 아래의 두 눈금 확보
        MinimumCredit = -2.0;
        // 영점 위의 두 눈금 확보
        MaximumCredit = 2.0;
    }
    // 다섯 눈금에 사용할 정수 단위 최소 간격 계산
    double Step = FMath::Max(1.0, FMath::Pow(10.0, FMath::FloorToDouble(FMath::LogX(10.0, (MaximumCredit - MinimumCredit) / 4.0))));
    // 영점을 포함하여 전체 범위를 네 칸 안에 수용하는 간격 선택
    while (FMath::CeilToDouble(MaximumCredit / Step) - FMath::FloorToDouble(MinimumCredit / Step) > 4.0)
    {
        // 눈금 값을 읽기 쉬운 1·2·5 단위로 증가
        double Magnitude = FMath::Pow(10.0, FMath::FloorToDouble(FMath::LogX(10.0, Step)));
        // 다음 간격의 자리수 내 배율 선택
        Step = Step / Magnitude < 1.5 ? 2.0 * Magnitude : (Step / Magnitude < 3.5 ? 5.0 * Magnitude : 10.0 * Magnitude);
    }
    // 최저값을 눈금 간격에 맞춰 내림
    MinimumCredit = FMath::FloorToDouble(MinimumCredit / Step) * Step;
    // 항상 네 칸과 다섯 눈금 유지
    MaximumCredit = MinimumCredit + Step * 4.0;
    // 실제 서버 시간으로 여섯 시간축 라벨 갱신
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Times); ++Index)
    {
        // 현재 디자이너의 선택적 텍스트만 갱신
        if (IsValid(Times[Index]) == true)
        {
            // 가장 왼쪽은 항상 영점이며 나머지는 실제 종료점까지 균등 분할
            Times[Index]->SetText(FText::AsNumber(Index == 0 ? 0.0 : Duration * Index / (UE_ARRAY_COUNT(Times) - 1), &TimeFormat));
        }
    }
    // 눈금과 그래프의 상하 방향 일치
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Credits); ++Index)
    {
        // 현재 배치된 금액축만 갱신
        if (IsValid(Credits[Index]) == true)
        {
            // 선 좌표와 같은 범위의 정수 크레딧 표시
            Credits[Index]->SetText(FText::AsNumber(MaximumCredit - Step * Index));
        }
    }
    // 화면 해상도와 무관한 정규화 좌표를 수신 때 한 번 계산
    for (TArray<FVector2D>& Points : PlayerGraphPoints)
    {
        // 시작 직후 중복된 영점 표본도 보이는 선으로 배치할 순서 유지
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            // 기존 표본의 시간과 금액을 같은 위치에서 정규화
            FVector2D& Point = Points[Index];
            // 영점 길이에서는 같은 시점의 누적값을 가로로 펼쳐 시작 상태 표시
            Point.X = Duration > 0.0 ? FMath::Clamp(Point.X / Duration, 0.0, 1.0)
                : (Points.Num() > 1 ? static_cast<double>(Index) / (Points.Num() - 1) : 0.0);
            // Slate의 아래 방향 좌표에 맞춰 수익 방향 반전
            Point.Y = FMath::Clamp((MaximumCredit - Point.Y) / (MaximumCredit - MinimumCredit), 0.0, 1.0);
        }
    }
    // 유효성 검사와 페인트 갱신에 재사용할 Slate 위젯 조회
    TSharedPtr<SWidget> CachedWidget = GetCachedWidget();
    // 축 문구가 같아도 개인 선만 달라진 경우 페인트 캐시 갱신
    if (CachedWidget.IsValid() == true)
    {
        // 불필요한 Tick 없이 새 보고서가 도착한 화면만 다시 그리기
        CachedWidget->Invalidate(EInvalidateWidgetReason::Paint);
    }
}

int32 UMD_DailyReportWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    // 디자이너 기본 화면과 표시 영역이 없는 파생 위젯에서는 선 생성 생략
    if (IsDesignTime() == true || IsValid(BDR_Graph_Grid) == false || GraphLineThickness <= 0.0f)
    {
        // 부모 위젯이 그린 레이어 그대로 유지
        return TopLayer;
    }
    // 디자이너에서 변경한 범례 색상을 그대로 사용하는 그래프
    UImage* Palettes[] = { Img_PlayerPallete_1, Img_PlayerPallete_2, Img_PlayerPallete_3, Img_PlayerPallete_4 };
    // 이번 페인트에서 자식 레이아웃까지 반영된 실제 그래프 좌표
    FGeometry GraphGeometry = BDR_Graph_Grid->GetPaintSpaceGeometry();
    // 화면 비율과 ScaleBox에 독립적인 그래프 로컬 크기
    FVector2D Size = GraphGeometry.GetLocalSize();
    // 숨겨지거나 아직 배치되지 않은 그래프의 잘못된 선 방지
    if (Size.X <= GraphLineThickness || Size.Y <= GraphLineThickness)
    {
        // 부모 위젯이 그린 레이어 그대로 유지
        return TopLayer;
    }
    // 부모 애니메이션과 동일하게 적용할 그래프 투명도
    float Opacity = 1.0f;
    // 그래프까지 이어진 실제 위젯 계층의 숨김 및 투명도 반영
    for (UWidget* Parent = BDR_Graph_Grid; IsValid(Parent) == true; Parent = Parent->GetParent())
    {
        // 접힌 컨테이너나 숨겨진 영역의 선 잔상 방지
        if (Parent->IsVisible() == false)
        {
            // 부모 위젯이 그린 레이어 그대로 유지
            return TopLayer;
        }
        // 기존 보고서 표시 애니메이션의 투명도 누적
        Opacity *= Parent->GetRenderOpacity();
    }
    // 경계의 선 두께까지 그래프 안쪽에 유지
    FVector2D Inset(GraphLineThickness * 0.5f, GraphLineThickness * 0.5f);
    // 네 선에 재사용할 최대 여섯 점 버퍼
    TArray<FVector2D> Points;
    // 보고서당 고정 표본 수만큼 미리 확보
    Points.Reserve(6);
    // 그래프 영역 외부로 선이 번지지 않는 클리핑 적용
    OutDrawElements.PushClip(FSlateClippingZone(GraphGeometry));
    // 실제 참가자의 기록만 범례 순서로 그리기
    for (int32 Index = 0; Index < PlayerGraphPoints.Num(); ++Index)
    {
        // 빈 슬롯이나 선을 구성할 수 없는 기록 제외
        if (PlayerGraphPoints[Index].Num() < 2 || IsValid(Palettes[Index]) == false)
        {
            // 현재 항목을 제외하고 다음 유효 기록으로 진행
            continue;
        }
        // 이전 개인의 좌표를 다음 선에 섞지 않는 버퍼 초기화
        Points.Reset();
        // 최신 레이아웃 크기에서만 로컬 픽셀 위치 계산
        for (FVector2D Point : PlayerGraphPoints[Index])
        {
            // 데이터 범위를 그리드 내부의 실제 그리기 범위에 대응
            Points.Add(Inset + Point * (Size - Inset * 2.0));
        }
        // 범례와 부모 스타일의 색상·투명도를 함께 적용
        FLinearColor Color = Palettes[Index]->GetColorAndOpacity() * InWidgetStyle.GetColorAndOpacityTint();
        // 그래프 부모의 활성화 애니메이션 투명도 반영
        Color.A *= Opacity;
        // 기존 그리드 위에 개인별 누적 순수익 선 표시
        FSlateDrawElement::MakeLines(OutDrawElements, TopLayer + 1, GraphGeometry.ToPaintGeometry(), Points,
            bParentEnabled == true && GetIsEnabled() == true ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
            Color, true, GraphLineThickness);
    }
    // 다른 UI 영역에 그래프 클리핑이 남지 않도록 복원
    OutDrawElements.PopClip();
    // 뒤이어 그려지는 상위 UI의 레이어 충돌 방지
    return TopLayer + 1;
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
        // 유효한 화면에서 즉시 완료된 연출도 후속 효과에 통지
        if (IsValid(World) == true)
        {
            // 실제 최종값 적용 이후 Blueprint 완료 이벤트 실행
            BP_OnSummaryAnimationFinished();
        }
        return;
    }
    // 함수가 호출된 순간의 실제 지출 표시를 보간 시작값으로 저장
    SummaryLossStart = ReadSummaryAmount(Txt_CreditLoss, SummaryLoss);
    // 함수가 호출된 순간의 실제 크레딧 표시를 보간 시작값으로 저장
    SummaryCreditStart = ReadSummaryAmount(Txt_CreditDelta, SummaryCredit);
    // 목표가 영점이어도 현재 값이 다르면 연출하고 동일한 두 값만 생략
    if (SummaryLossStart == SummaryLoss && SummaryCreditStart == SummaryCredit)
    {
        // 동일한 합계의 숫자 서식과 색상만 복원하고 반복 예약 생략
        StopSummaryAnimation();
        // 이미 목표값인 경우에도 후속 강조 연출 연결
        BP_OnSummaryAnimationFinished();
        return;
    }
    // 읽어 온 지출 시작값에 기존 숫자 서식과 부호 색상 적용
    SetAmountText(Txt_CreditLoss, SummaryLossStart);
    // 읽어 온 크레딧 시작값에 기존 숫자 서식과 부호 색상 적용
    SetAmountText(Txt_CreditDelta, SummaryCreditStart);
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
        || (IsValid(Txt_CreditLoss) == false && IsValid(Txt_CreditDelta) == false))
    {
        // 남아 있는 타이머 해제 및 실제 합계 복원
        StopSummaryAnimation();
        return;
    }
    // 콜백 횟수 대신 실제 경과 시간으로 두 금액의 공통 진행률 계산
    double Alpha = FMath::Clamp((World->GetTimeSeconds() - SummaryAnimationStartTime) / SummaryAnimationDuration, 0.0, 1.0);
    // 목표 시각 이후에는 부동소수점 보간 대신 원본 정수 사용
    if (Alpha >= 1.0)
    {
        // 최종 합계의 정확한 표시 및 반복 예약 종료
        StopSummaryAnimation();
        // 정상 완료 시에만 Blueprint의 후속 강조 연출 실행
        BP_OnSummaryAnimationFinished();
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
    // 현재 표시에서 실제 지출까지 증가 또는 감소
    SetAmountText(Txt_CreditLoss, InterpolateAmount(SummaryLossStart, SummaryLoss));
    // 현재 표시에서 실제 크레딧까지 같은 진행률로 변화
    SetAmountText(Txt_CreditDelta, InterpolateAmount(SummaryCreditStart, SummaryCredit));
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
        // 표시 캐시와 부호 색상을 유지한 정확한 최종 지출 적용
        SetAmountText(Txt_CreditLoss, SummaryLoss);
        // 보간 오차 없이 서버 기록에서 계산한 실제 크레딧 적용
        SetAmountText(Txt_CreditDelta, SummaryCredit);
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
	if (!bShowingFinalReport) MDAudioFeedback::UI(this, TEXT("SFX.Mothership.Report.Stamp"));
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
