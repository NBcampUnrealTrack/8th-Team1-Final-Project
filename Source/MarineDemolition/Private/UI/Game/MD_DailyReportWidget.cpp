// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Game/MD_DailyReportWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

namespace
{
	// 네트워크 전달 배열에서 요청한 Enum 분류의 합계를 조회
	int32 FindDailyReportValue(const TArray<FMD_DailyReportValue>& Values, EMD_DailyReportCategory Category)
	{
		int32 OutValue = 0;
		for (const FMD_DailyReportValue& ValueEntry : Values)
		{
			if (ValueEntry.Category == Category)
			{
				OutValue = ValueEntry.Amount;
				break;
			}
		}

		return OutValue;
	}
}

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
	// 모든 보고서 영역이 동일한 원장 배열 값을 사용하도록 카테고리별 표시값 조회
	int32 CategorizingScore = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Categorizing);
	int32 ReturnScore = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Return);
	int32 AccidentScore = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Accident);
	int32 QuestScore = FindDailyReportValue(Report.DailyScoreTotals, EMD_DailyReportCategory::Quest);
	int32 RetainerCredits = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Retainer);
	int32 CategorizingCredits = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Categorizing);
	int32 AccidentCredits = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Accident);
	int32 EvaluationCredits = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Evaluation);
	int32 QuestCredits = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Quest);
	int32 OtherCredits = FindDailyReportValue(Report.DailyCreditTotals, EMD_DailyReportCategory::Other);
	int32 TotalCredits = 0;
	for (const FMD_DailyReportValue& CreditEntry : Report.DailyCreditTotals)
	{
		TotalCredits += CreditEntry.Amount;
	}

	if (IsValid(Txt_tHeader) == true)
	{
		Txt_tHeader->SetText(FText::Format(NSLOCTEXT("MDDailyReport", "Header", "Day {0} 정산 - {1}"), FText::AsNumber(Report.Day), UEnum::GetDisplayValueAsText(Report.Result)));
	}

	// 점수 로그의 각 행을 서버가 분류한 당일 합계로 갱신
	if (IsValid(Txt_CategorizingScore_Value) == true)
	{
		Txt_CategorizingScore_Value->SetText(FText::AsNumber(CategorizingScore));
	}

	if (IsValid(Txt_ReturnScore_Value) == true)
	{
		Txt_ReturnScore_Value->SetText(FText::AsNumber(ReturnScore));
	}

	if (IsValid(Txt_AccidentScore_Value) == true)
	{
		Txt_AccidentScore_Value->SetText(FText::AsNumber(AccidentScore));
	}

	if (IsValid(Txt_QuestScore_Value) == true)
	{
		Txt_QuestScore_Value->SetText(FText::AsNumber(QuestScore));
	}

	if (IsValid(Txt_TotalScore_Value) == true)
	{
		Txt_TotalScore_Value->SetText(FText::AsNumber(Report.DailyScore));
	}

	// 크레딧 로그의 각 행에 통화 단위를 포함한 당일 변동값을 표시
	if (IsValid(Txt_RetainerCredit_Value) == true)
	{
		FText RetainerCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), RetainerCredits));
		Txt_RetainerCredit_Value->SetText(RetainerCreditText);
	}

	if (IsValid(Txt_CategorizingCredit_Value) == true)
	{
		FText CategorizingCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), CategorizingCredits));
		Txt_CategorizingCredit_Value->SetText(CategorizingCreditText);
	}

	if (IsValid(Txt_AccidentCredit_Value) == true)
	{
		FText AccidentCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), AccidentCredits));
		Txt_AccidentCredit_Value->SetText(AccidentCreditText);
	}

	if (IsValid(Txt_EvaluationCredit_Value) == true)
	{
		FText EvaluationCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), EvaluationCredits));
		Txt_EvaluationCredit_Value->SetText(EvaluationCreditText);
	}

	if (IsValid(Txt_QuestCredit_Value) == true)
	{
		FText QuestCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), QuestCredits));
		Txt_QuestCredit_Value->SetText(QuestCreditText);
	}

	if (IsValid(Txt_OtherCredit_Value) == true)
	{
		// 구매·대출·상환처럼 전용 행이 없는 당일 크레딧 변동을 기타 행에 표시
		FText OtherCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), OtherCredits));
		Txt_OtherCredit_Value->SetText(OtherCreditText);
	}

	if (IsValid(Txt_TotalCredit_Value) == true)
	{
		FText TotalCreditText = FText::FromString(FString::Printf(TEXT("%+d₡"), TotalCredits));
		Txt_TotalCredit_Value->SetText(TotalCreditText);
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
