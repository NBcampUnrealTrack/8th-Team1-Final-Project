// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Interaction/MD_TravelDifficultyWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

void UMD_TravelDifficultyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BTN_Diff_Safe)
	{
		BTN_Diff_Safe->OnClicked.AddDynamic(this, &UMD_TravelDifficultyWidget::HandleDiffSafeClicked);
	}
	if (BTN_Diff_Standard)
	{
		BTN_Diff_Standard->OnClicked.AddDynamic(this, &UMD_TravelDifficultyWidget::HandleDiffStandardClicked);
	}
	if (BTN_Diff_Dangerous)
	{
		BTN_Diff_Dangerous->OnClicked.AddDynamic(this, &UMD_TravelDifficultyWidget::HandleDiffDangerousClicked);
	}
	if (BTN_Diff_Extreme)
	{
		BTN_Diff_Extreme->OnClicked.AddDynamic(this, &UMD_TravelDifficultyWidget::HandleDiffExtremeClicked);
	}

	if (BTN_Back)
	{
		BTN_Back->OnClicked.AddDynamic(this, &UMD_TravelDifficultyWidget::HandleBackClicked);
	}
	if (BTN_Depart)
	{
		BTN_Depart->OnClicked.AddDynamic(this, &UMD_TravelDifficultyWidget::HandleDepartClicked);
	}

	RefreshDifficultyButtons();
	RefreshInspectionPreview();
}

bool UMD_TravelDifficultyWidget::IsHost() const
{
	// 리슨서버 호스트의 컨트롤러만 자기 클라이언트에서 권한을 갖는다
	const APlayerController* PC = GetOwningPlayer();
	return IsValid(PC) && PC->HasAuthority();
}

void UMD_TravelDifficultyWidget::SetSelectedSite(const FText& InSiteDisplayName)
{
	if (TXT_SelectedSite)
	{
		TXT_SelectedSite->SetText(InSiteDisplayName);
	}

	RefreshDifficultyButtons();
	RefreshInspectionPreview();
}

void UMD_TravelDifficultyWidget::ResetSelection()
{
	SelectedDifficulty = EMD_Difficulty::Standard;

	RefreshDifficultyButtons();
	RefreshInspectionPreview();
}

void UMD_TravelDifficultyWidget::SelectDifficulty(EMD_Difficulty NewDifficulty)
{
	if (!IsHost())
	{
		return;
	}

	SelectedDifficulty = NewDifficulty;

	RefreshDifficultyButtons();
	RefreshInspectionPreview();
}

void UMD_TravelDifficultyWidget::RefreshDifficultyButtons()
{
	const bool bHost = IsHost();

	UButton* const DiffButtons[] = { BTN_Diff_Safe, BTN_Diff_Standard, BTN_Diff_Dangerous, BTN_Diff_Extreme };
	const EMD_Difficulty DiffValues[] = {
		EMD_Difficulty::Safe,
		EMD_Difficulty::Standard,
		EMD_Difficulty::Dangerous,
		EMD_Difficulty::Extreme
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(DiffButtons); ++Index)
	{
		if (!DiffButtons[Index])
		{
			continue;
		}

		// 선택된 난이도는 비활성으로 눌린 상태를 표시한다
		const bool bSelected = (DiffValues[Index] == SelectedDifficulty);
		DiffButtons[Index]->SetIsEnabled(bHost && !bSelected);
	}

	// 클라이언트는 화면만 보고 조작은 못 한다. 뒤로는 항상 열어둔다
	if (BTN_Depart)
	{
		BTN_Depart->SetIsEnabled(bHost);
	}
}

void UMD_TravelDifficultyWidget::RefreshInspectionPreview()
{
	if (!TXT_InspectionPreview)
	{
		return;
	}

	const int32 Day2 = MDTravel::GetScaledInspectionTarget(MDTravel::BaseInspectionDay2, SelectedDifficulty);
	const int32 Day3 = MDTravel::GetScaledInspectionTarget(MDTravel::BaseInspectionDay3, SelectedDifficulty);
	const int32 Day4 = MDTravel::GetScaledInspectionTarget(MDTravel::BaseInspectionDay4, SelectedDifficulty);

	TXT_InspectionPreview->SetText(FText::Format(
		NSLOCTEXT("MDTravel", "InspectionPreview", "점검 기준 · Day2 {0} · Day3 {1} · Day4 {2}   (4인 기준)"),
		FText::AsNumber(Day2), FText::AsNumber(Day3), FText::AsNumber(Day4)));
}

void UMD_TravelDifficultyWidget::HandleDiffSafeClicked()
{
	SelectDifficulty(EMD_Difficulty::Safe);
}

void UMD_TravelDifficultyWidget::HandleDiffStandardClicked()
{
	SelectDifficulty(EMD_Difficulty::Standard);
}

void UMD_TravelDifficultyWidget::HandleDiffDangerousClicked()
{
	SelectDifficulty(EMD_Difficulty::Dangerous);
}

void UMD_TravelDifficultyWidget::HandleDiffExtremeClicked()
{
	SelectDifficulty(EMD_Difficulty::Extreme);
}

void UMD_TravelDifficultyWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

void UMD_TravelDifficultyWidget::HandleDepartClicked()
{
	if (!IsHost())
	{
		return;
	}

	OnDepartRequested.Broadcast(SelectedDifficulty);
}
