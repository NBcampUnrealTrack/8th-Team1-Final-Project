// Fill out your copyright notice in the Description page of Project Settings.

#include "Component/MD_VoiceChatComponent.h"
#include "GameFramework/PlayerController.h"

UMD_VoiceChatComponent::UMD_VoiceChatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UMD_VoiceChatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerPlayerController = Cast<APlayerController>(GetOwner());
	StartTalking();
}

void UMD_VoiceChatComponent::Activate(bool bReset)
{
	Super::Activate(bReset);

	StartTalking();
}

void UMD_VoiceChatComponent::Deactivate()
{
	StopTalking();

	Super::Deactivate();
}

void UMD_VoiceChatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTalking();

	Super::EndPlay(EndPlayReason);
}

void UMD_VoiceChatComponent::SetPushToTalk(const bool bNewUsePushToTalk)
{
	// Owner 객체(PlayerController)의 유효성 및 로컬 컨트롤러 검사
	if (OwnerPlayerController.IsValid() == false || OwnerPlayerController->IsLocalPlayerController() == false)
	{
		return;
	}

	// 프로퍼티 값 변경
	bUsePushToTalk = bNewUsePushToTalk;

	if (IsVoiceActivated() == true)
	{
		StartTalking();
	}
	else
	{
		StopTalking();
	}
}

void UMD_VoiceChatComponent::StartTalking()
{
	if (OwnerPlayerController.IsValid() == false || OwnerPlayerController->IsLocalController() == false)
	{
		return;
	}

	bIsMicrophonActive = true;

	// 음성 채팅 기능 활성화 조건 검사
	if (IsVoiceActivated() == true)
	{
		// 음성 채팅 기능 활성화
		OwnerPlayerController->StartTalking();
		return;
	}

	// 음성 채팅 기능 비활성화
	StopTalking();
}

void UMD_VoiceChatComponent::StopTalking()
{
	if (OwnerPlayerController.IsValid() == false || OwnerPlayerController->IsLocalController() == false)
	{
		return;
	}

	bIsMicrophonActive = false;

	// 음성 채팅 기능 비활성화
	OwnerPlayerController->StopTalking();
}
