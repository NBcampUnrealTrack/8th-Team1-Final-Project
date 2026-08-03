// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Interaction/MD_TravelConsoleWidget.h"
#include "Components/Button.h"
#include "Player/MD_PlayerController.h"
#include "World/Interaction/MD_TravelConsole.h"

void UMD_TravelConsoleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	if (DepartButton)
	{
		DepartButton->OnClicked.AddDynamic(this, &UMD_TravelConsoleWidget::HandleDepartClicked);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UMD_TravelConsoleWidget::HandleCloseClicked);
	}
}

void UMD_TravelConsoleWidget::NativeDestruct()
{
	// 트래블 등 어떤 경로로 닫혀도 커서/입력 모드 복구
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}

	Super::NativeDestruct();
}

void UMD_TravelConsoleWidget::HandleDepartClicked()
{
	AMD_PlayerController* PC = Cast<AMD_PlayerController>(GetOwningPlayer());

	if (IsValid(PC) && IsValid(OwningConsole))
	{
		PC->ServerRequestTravel(OwningConsole);
	}

	CloseWidget();
}

void UMD_TravelConsoleWidget::HandleCloseClicked()
{
	CloseWidget();
}

void UMD_TravelConsoleWidget::CloseWidget()
{
	// 입력 모드 복구는 NativeDestruct에서 처리
	RemoveFromParent();
}
