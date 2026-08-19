// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UMD_DailyReportWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Blueprint 그래프 유무와 관계없이 전용 클래스가 버튼 입력을 단일 경로로 소유하기 위한 연결
	if (IsValid(Btn_Nextday) == true)
	{
		Btn_Nextday->OnClicked.RemoveDynamic(this, &UMD_DailyReportWidget::HandleContinueClicked);
		Btn_Nextday->OnClicked.AddDynamic(this, &UMD_DailyReportWidget::HandleContinueClicked);
	}
}

void UMD_DailyReportWidget::NativeDestruct()
{
	// 재생성된 보고서가 제거된 인스턴스의 버튼 델리게이트를 유지하지 않도록 하는 정리
	if (IsValid(Btn_Nextday) == true)
	{
		Btn_Nextday->OnClicked.RemoveDynamic(this, &UMD_DailyReportWidget::HandleContinueClicked);
	}

	Super::NativeDestruct();
}

void UMD_DailyReportWidget::SetReport(const FMD_InspectionReport& Report)
{
	if (IsValid(Txt_tHeader) == true)
	{
		Txt_tHeader->SetText(FText::Format(NSLOCTEXT("MDDailyReport", "Header", "Day {0} 정산 - {1}"), FText::AsNumber(Report.Day), UEnum::GetDisplayValueAsText(Report.Result)));
	}

	if (IsValid(Txt_TodayNum) == true)
	{
		Txt_TodayNum->SetText(FText::AsNumber(Report.DailyScore));
	}

	if (IsValid(Txt_CumulativeNum) == true)
	{
		Txt_CumulativeNum->SetText(FText::AsNumber(Report.Score));
	}

	// 아직 확정 데이터가 없는 두 영역에서 WBP 디자인 예시값을 숨기기 위한 임시 표시
	if (IsValid(Txt_HandselNum) == true)
	{
		Txt_HandselNum->SetText(NSLOCTEXT("MDDailyReport", "PendingAdvance", "-"));
	}

	if (IsValid(Txt_AdjustmentNum) == true)
	{
		Txt_AdjustmentNum->SetText(NSLOCTEXT("MDDailyReport", "PendingAdjustment", "-"));
	}

	if (IsValid(Txt_Credit) == true)
	{
		// 클라이언트 복제 순서와 무관하게 서버 보고서에 포함된 확정 크레딧 표시
		FText CreditsValue = FText::Format(NSLOCTEXT("MDDailyReport", "CreditsFormat", "{0}₡"), FText::AsNumber(Report.Credits));
		Txt_Credit->SetText(CreditsValue);
	}

	if (IsValid(Txt_Nextday) == true)
	{
		// 계약 종료 결과는 다음 Day가 아니므로 중립적인 확인 문구를 사용하기 위한 판정
		bool bEndsContract = Report.Result == EMD_InspectionResult::ContractCleared || Report.Result == EMD_InspectionResult::GameOver;
		Txt_Nextday->SetText(bEndsContract == true ? NSLOCTEXT("MDDailyReport", "Confirm", "확인") : NSLOCTEXT("MDDailyReport", "NextDay", "다음 날"));
	}
}

void UMD_DailyReportWidget::SetContinueEnabled(bool bEnabled)
{
	if (IsValid(Btn_Nextday) == true)
	{
		Btn_Nextday->SetIsEnabled(bEnabled);
	}
}

void UMD_DailyReportWidget::RequestContinue()
{
	OnContinueRequested.Broadcast();
}

void UMD_DailyReportWidget::HandleContinueClicked()
{
	RequestContinue();
}
