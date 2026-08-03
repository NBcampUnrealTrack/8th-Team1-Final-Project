// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Return/MD_ReturnStatusWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Player/MD_PlayerState.h"
#include "TimerManager.h"

void UMD_ReturnStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(ReturnStatusText))
	{
		ReturnStatusText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UWorld* World = GetWorld())
	{
		UpdateReturnStatusText();

		World->GetTimerManager().SetTimer(
			UpdateReturnStatusTimerHandle,
			this,
			&UMD_ReturnStatusWidget::UpdateReturnStatusText,
			0.1f,
			true
		);
	}
}

void UMD_ReturnStatusWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateReturnStatusTimerHandle);
	}

	Super::NativeDestruct();
}

void UMD_ReturnStatusWidget::UpdateReturnStatusText()
{
	if (!IsValid(ReturnStatusText))
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();

	if (!IsValid(OwningPlayer))
	{
		ReturnStatusText->SetVisibility(ESlateVisibility::Collapsed);

		return;
	}

	AMD_PlayerState* MDPlayerState = OwningPlayer->GetPlayerState<AMD_PlayerState>();

	if (!IsValid(MDPlayerState))
	{
		ReturnStatusText->SetVisibility(ESlateVisibility::Collapsed);

		return;
	}

	switch (MDPlayerState->GetReturnStatus())
	{
	case EMD_ReturnStatus::Pending:
		ReturnStatusText->SetVisibility(ESlateVisibility::Collapsed);
		break;

	case EMD_ReturnStatus::Returned:
		ReturnStatusText->SetText(FText::FromString(TEXT("복귀 완료")));

		ReturnStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
		break;

	case EMD_ReturnStatus::Failed:
		ReturnStatusText->SetText(FText::FromString(TEXT("복귀 실패")));

		ReturnStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
		break;

	default:
		ReturnStatusText->SetVisibility(ESlateVisibility::Collapsed);
		break;
	}
}

