// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "UI/MD_CommonButtonBase.h"
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

FText UMD_DailyReportWidget::FormatDailyReportAmount(int32 Amount, bool bIncludeCreditUnit)
{
	// 시안과 동일한 부호 및 천 단위 구분 설정
	FNumberFormattingOptions Options;
	// 양수와 영점의 증감 부호 표시
	Options.AlwaysSign = true;
	// 큰 수치의 자리 구분 활성화
	Options.UseGrouping = true;
	// 지역화된 정수 표시 문자열 저장
	FText AmountText = FText::AsNumber(Amount, &Options);
	// 최종 크레딧 합계에만 통화 단위 표시
	if (bIncludeCreditUnit == true)
	{
		// 정산 합계와 통화 기호 결합
		AmountText = FText::Format(NSLOCTEXT("MDDailyReport", "CreditAmount", "{0} ₡"), AmountText);
	}
	// 완성된 정산 문자열 반환
	return AmountText;
}

void UMD_DailyReportWidget::ApplyAmountTextColor(UTextBlock* AmountTextBlock, int32 Amount)
{
	// 선택적으로 배치된 정산 숫자 영역의 유효성 확인
	if (IsValid(AmountTextBlock) == false)
	{
		// 숫자 영역이 없는 경우 색상 적용 생략
		return;
	}
	// 양수와 영점에 적용할 기본 색상 선택
	FLinearColor AmountColor = PositiveAmountTextColor;
	// 음수 값에는 별도로 지정한 색상 선택
	if (Amount < 0)
	{
		// 차감 금액의 표시 색상 지정
		AmountColor = NegativeAmountTextColor;
	}
	// 해당 숫자 TextBlock에만 표시 색상 적용
	AmountTextBlock->SetColorAndOpacity(FSlateColor(AmountColor));
}

void UMD_DailyReportWidget::NativeConstruct()
{
	// 부모 위젯의 구성 처리
	Super::NativeConstruct();
	// 퇴근 버튼이 배치된 경우에만 전환 요청 연결
	if (IsValid(Btn_EndDay) == true)
	{
		// 재구성 시 기존 클릭 연결 정리
		auto& ClickedEvent = Btn_EndDay->OnClicked();
		// 현재 보고서 인스턴스의 클릭 연결 해제
		ClickedEvent.RemoveAll(this);
		// 공통 버튼 클릭을 기존 전환 요청으로 연결
		ClickedEvent.AddUObject(this, &UMD_DailyReportWidget::HandleContinueClicked);
	}
}

void UMD_DailyReportWidget::NativeDestruct()
{
	// 퇴근 버튼의 유효한 연결만 정리
	if (IsValid(Btn_EndDay) == true)
	{
		// 제거되는 위젯의 클릭 연결 해제
		auto& ClickedEvent = Btn_EndDay->OnClicked();
		// 현재 보고서 인스턴스의 클릭 연결 해제
		ClickedEvent.RemoveAll(this);
	}
	// 부모 위젯의 소멸 처리
	Super::NativeDestruct();
}

void UMD_DailyReportWidget::SetReport(const FMD_InspectionReport& Report)
{
	// 보이지 않는 기타 금액도 포함한 실제 크레딧 변동 합계
	int32 TotalCredits = 0;
	// 서버가 전달한 전체 크레딧 원장 순회
	for (const FMD_DailyReportValue& Entry : Report.DailyCreditTotals)
	{
		// 분류와 무관하게 모든 금액 합산
		TotalCredits += Entry.Amount;
	}
	// 현재 정산 날짜 부제목 갱신
	if (IsValid(Txt_ReportHeader) == true)
	{
		// 두 자리 날짜를 위한 형식 설정
		FNumberFormattingOptions DayOptions;
		// 날짜의 불필요한 자리 구분 제거
		DayOptions.UseGrouping = false;
		// 한 자리 날짜의 앞자리 영점 표시
		DayOptions.MinimumIntegralDigits = 2;
		// 실제 정산 날짜 문자열 저장
		FText DayText = FText::AsNumber(Report.Day, &DayOptions);
		// 날짜와 작업 정산 문구 결합
		FText HeaderText = FText::Format(NSLOCTEXT("MDDailyReport", "ReportHeader", "DAY {0} · 작업 정산"), DayText);
		// 실제 날짜를 부제목에 반영
		Txt_ReportHeader->SetText(HeaderText);
	}
	// 기존 부제목에 표시하던 판정 결과를 상태 배지로 이동
	if (IsValid(Txt_Badge) == true)
	{
		// 점검 결과의 지역화 문구 조회
		FText BadgeText = UEnum::GetDisplayValueAsText(Report.Result);
		// 점검 대상이 아닌 날은 작업 완료로 표시
		if (Report.Result == EMD_InspectionResult::NotApplicable)
		{
			// 일반 작업 종료 문구 지정
			BadgeText = NSLOCTEXT("MDDailyReport", "WorkComplete", "✓  작업 완료");
		}
		// 판정 결과를 상태 배지에 반영
		Txt_Badge->SetText(BadgeText);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Score_Categorizing) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Categorizing);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Score_Categorizing->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Score_Categorizing, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Score_Return) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Return);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Score_Return->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Score_Return, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Score_Accident) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Accident);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Score_Accident->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Score_Accident, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Score_Quest) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Quest);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Score_Quest->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Score_Quest, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Score_Total) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = Report.DailyScore;
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Score_Total->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Score_Total, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Credit_Retainer) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Retainer);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Credit_Retainer->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Credit_Retainer, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Credit_Categorizing) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Categorizing);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Credit_Categorizing->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Credit_Categorizing, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Credit_Accident) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Accident);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Credit_Accident->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Credit_Accident, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Credit_Evaluation) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Evaluation);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Credit_Evaluation->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Credit_Evaluation, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Credit_Quest) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Quest);
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, false);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Credit_Quest->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Credit_Quest, Amount);
	}
	// 현재 에셋에 배치된 정산 값 영역 갱신
	if (IsValid(Txt_Credit_Total) == true)
	{
		// 해당 영역에 대응하는 서버 정산 값 저장
		int32 Amount = TotalCredits;
		// 부호와 자리 구분이 적용된 표시 문구 저장
		FText AmountText = FormatDailyReportAmount(Amount, true);
		// 실제 정산 값을 바인딩된 텍스트에 적용
		Txt_Credit_Total->SetText(AmountText);
		// 값이 갱신되는 숫자 영역에만 부호별 색상 반영
		ApplyAmountTextColor(Txt_Credit_Total, Amount);
	}
	// 계약이 끝나는 판정의 확인 문구 선택
	bool bEndsContract = false;
	// 계약 클리어는 다음 날 진행을 종료하는 판정
	if (Report.Result == EMD_InspectionResult::ContractCleared)
	{
		// 계약 종료 상태 저장
		bEndsContract = true;
	}
	// 게임오버도 확인 후 결과 화면으로 전환하는 판정
	if (Report.Result == EMD_InspectionResult::GameOver)
	{
		// 게임 종료 상태 저장
		bEndsContract = true;
	}
	// 공통 버튼 콘텐츠로 전환 문구 표시
	if (IsValid(Btn_EndDay) == true)
	{
		// 일반 정산은 퇴근하기이며 계약 종료는 확인으로 표시
		FText ContinueText = bEndsContract == true ? NSLOCTEXT("MDDailyReport", "Confirm", "확인") : NSLOCTEXT("MDDailyReport", "EndDay", "퇴근하기");
		// 공통 버튼 내부 텍스트에 문구 반영
		Btn_EndDay->SetContentText(ContinueText);
	}
}

void UMD_DailyReportWidget::SetContinueEnabled(bool bEnabled)
{
	// 퇴근 전환 대기 중 잔업 입력도 함께 잠금
	if (IsValid(Btn_Resume) == true)
	{
		// 두 버튼의 입력 가능 상태 동기화
		Btn_Resume->SetIsEnabled(bEnabled);
	}
	// 실제 퇴근 버튼이 있는 경우에만 활성 상태 변경
	if (IsValid(Btn_EndDay) == true)
	{
		// 서버 전환 확인 대기 중 중복 입력 차단
		Btn_EndDay->SetIsEnabled(bEnabled);
	}
}

void UMD_DailyReportWidget::RequestContinue()
{
	// 소유 컨트롤러의 기존 서버 전환 확인 이벤트 발생
	OnContinueRequested.Broadcast();
}

void UMD_DailyReportWidget::HandleContinueClicked()
{
	// 공통 버튼의 퇴근 입력을 기존 전환 요청으로 전달
	RequestContinue();
}
