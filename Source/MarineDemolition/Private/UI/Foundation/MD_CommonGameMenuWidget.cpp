// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "GameplayTags/MD_GameplayTags.h"

void UMD_CommonGameMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 입력 모드를 변경할 로컬 플레이어 컨트롤러를 가져온다.
	APlayerController* OwningPlayerController = GetOwningPlayer();
	if (IsValid(OwningPlayerController) == true)
	{
		// 게임 입력을 차단하고 메뉴 UI만 입력을 받도록 설정한다.
		FInputModeUIOnly InputMode = FInputModeUIOnly();
		InputMode.SetWidgetToFocus(TakeWidget());

		OwningPlayerController->SetInputMode(InputMode);
	}
}

void UMD_CommonGameMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	TargetLayerTag = MDGameplayTags::UI_Layer_GameMenu;
}
