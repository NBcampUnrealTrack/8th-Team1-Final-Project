// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Interaction/MD_DayEndConsoleWidget.h"
#include "Components/Button.h"
#include "Player/MD_PlayerController.h"
#include "World/Interaction/MD_DayEndConsole.h"

void UMD_DayEndConsoleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (EndDayButton)
	{
		EndDayButton->OnClicked.AddDynamic(this, &UMD_DayEndConsoleWidget::HandleEndDayClicked);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UMD_DayEndConsoleWidget::HandleCloseClicked);
	}
}

void UMD_DayEndConsoleWidget::NativeDestruct()
{
	// 트래블 등 어떤 경로로 닫혀도 커서/입력 모드 복구
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}

	Super::NativeDestruct();
}

void UMD_DayEndConsoleWidget::HandleEndDayClicked()
{
	AMD_PlayerController* PC = Cast<AMD_PlayerController>(GetOwningPlayer());

	if (IsValid(PC) && IsValid(OwningConsole))
	{
		PC->ServerRequestEndDay(OwningConsole);
	}

	CloseWidget();
}

void UMD_DayEndConsoleWidget::HandleCloseClicked()
{
	CloseWidget();
}

void UMD_DayEndConsoleWidget::CloseWidget()
{
	// 입력 모드 복구는 NativeDestruct에서 처리
	RemoveFromParent();
}
