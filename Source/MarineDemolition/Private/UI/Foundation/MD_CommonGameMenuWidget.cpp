// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Foundation/MD_CommonGameMenuWidget.h"

void UMD_CommonGameMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 입력 모드를 변경할 로컬 플레이어 컨트롤러를 가져온다.
	APlayerController* OwningPlayerController = GetOwningPlayer();
	if (IsValid(OwningPlayerController) == true)
	{
		// 게임 입력을 차단하고 메뉴 UI만 입력을 받도록 설정한다.
		FInputModeUIOnly InputMode = FInputModeUIOnly();
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		OwningPlayerController->SetInputMode(InputMode);

		OwningPlayerController->SetShowMouseCursor(true);
	}
}

void UMD_CommonGameMenuWidget::NativeOnDeactivated()
{
	// 입력 모드를 변경할 로컬 플레이어 컨트롤러를 가져온다.
	APlayerController* OwningPlayerController = GetOwningPlayer();
	if (IsValid(OwningPlayerController) == true)
	{
		// UI 입력을 차단하고 게임만 입력을 받도록 설정한다.
		FInputModeGameOnly InputMode = FInputModeGameOnly();
		InputMode.SetConsumeCaptureMouseDown(true);

		OwningPlayerController->SetInputMode(InputMode);

		OwningPlayerController->SetShowMouseCursor(false);
	}

	Super::NativeOnDeactivated();
}
