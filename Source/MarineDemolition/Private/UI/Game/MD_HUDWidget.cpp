// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_HUDWidget.h"

#include "Input/UIActionBindingHandle.h"
#include "Components/VerticalBox.h"
#include "UI/Game/MD_TalkNotificationWidget.h"

void UMD_HUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 전용 목록이 있는 경우 이전 항목 정리
	if (IsValid(VB_TalkList) == true)
	{
		// 디자이너 예시와 재구성 이전 항목을 실제 발화 목록으로 교체
		VB_TalkList->ClearChildren();
	}
}

void UMD_HUDWidget::NativeDestruct()
{
	// HUD 제거 시 알림의 부모 연결 정리
	if (IsValid(VB_TalkList) == true)
	{
		// Talker가 이전 HUD 알림을 재사용하지 않도록 부모 연결 해제
		VB_TalkList->ClearChildren();
	}

	Super::NativeDestruct();
}

UMD_TalkNotificationWidget* UMD_HUDWidget::CreateTalkNotification()
{
	// 목록이 없는 HUD에서 반환할 빈 생성 결과
	UMD_TalkNotificationWidget* Notification = nullptr;
	if (IsValid(VB_TalkList) == false)
	{
		return Notification;
	}

	// HUD에 설정된 클래스로 알림 위젯 생성
	UClass* NotificationClass = TalkNotificationClass.LoadSynchronous();
	Notification = CreateWidget<UMD_TalkNotificationWidget>(this, NotificationClass);
	if (IsValid(Notification) == true)
	{
		// 생성된 알림을 HUD 목록에 추가
		VB_TalkList->AddChildToVerticalBox(Notification);
	}

	// 생성된 알림 반환
	return Notification;
}

TOptional<FUIInputConfig> UMD_HUDWidget::GetDesiredInputConfig() const
{
	// HUD가 활성 화면일 때 캐릭터와 카메라 입력을 허용하는 설정을 구성한다.
	FUIInputConfig InputConfig = FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently);

	// Common UI Action Router가 적용할 게임 입력 설정을 반환한다.
	return InputConfig;
}
