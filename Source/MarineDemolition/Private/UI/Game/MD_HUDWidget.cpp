// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_HUDWidget.h"

#include "GameplayTags/MD_GameplayTags.h"
#include "Input/UIActionBindingHandle.h"

TOptional<FUIInputConfig> UMD_HUDWidget::GetDesiredInputConfig() const
{
	// HUD가 활성 화면일 때 캐릭터와 카메라 입력을 허용하는 설정을 구성한다.
	FUIInputConfig InputConfig = FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently);

	// Common UI Action Router가 적용할 게임 입력 설정을 반환한다.
	return InputConfig;
}

void UMD_HUDWidget::NativeOnInitialized()
{
	// 공통 Activatable 초기화를 먼저 완료한다.
	Super::NativeOnInitialized();

	// HUD가 중앙 관리자의 GameHUD Stack을 대상으로 사용되도록 지정한다.
	TargetLayerTag = MDGameplayTags::UI_Layer_GameHUD;
}
