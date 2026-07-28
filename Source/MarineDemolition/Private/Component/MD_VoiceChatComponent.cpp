// Fill out your copyright notice in the Description page of Project Settings.

#include "Component/MD_VoiceChatComponent.h"
#include "GameFramework/PlayerController.h"

UMD_VoiceChatComponent::UMD_VoiceChatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);
}

void UMD_VoiceChatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerPlayerController = Cast<APlayerController>(GetOwner());

	// Push To Talk 가 비활성화 되어있는 경우 컴포넌트가 활성화 되어있으면 마이크 활성화
	if (IsActive() && bUsePushToTalk == false)
	{
		StartTalking();
	}
}

void UMD_VoiceChatComponent::Activate(bool bReset)
{
	Super::Activate(bReset);

	// Push To Talk 가 비활성화 되어있는 경우 컴포넌트가 활성화 되어있으면 마이크 활성화
	if (bUsePushToTalk == false)
	{
		StartTalking();
	}
}

void UMD_VoiceChatComponent::Deactivate()
{
	// 컴포넌트 비활성화 시 마이크 비활성화
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
	// 기존 Push To Talk 설정값이 다른 경우에만 동작
	if (bUsePushToTalk == bNewUsePushToTalk)
	{
		return;
	}

	// 프로퍼티 값 변경
	bUsePushToTalk = bNewUsePushToTalk;

	// 컴포넌트가 비활성화 되어있거나 또는 Push To Talk 설정을 켜는 경우 마이크 비활성화
	if (IsActive() == false || bUsePushToTalk == true)
	{
		StopTalking();
	}
	else
	{
		// Push To Talk 설정을 끄는 경우 마이크 활성화
		StartTalking();
	}
}

void UMD_VoiceChatComponent::StartTalking()
{
	// 컴포넌트가 비활성화 되어있거나 또는 이미 마이크 활성화 되어있는 경우 함수 조기 종료
	if (IsActive() == false || bIsMicrophonActive == true)
	{
		return;
	}

	// PlayerController(Owner)의 유효성 및 로컬 컨트롤러 검사
	APlayerController* PlayerController = OwnerPlayerController.Get();
	if (IsValid(PlayerController) == false || PlayerController->IsLocalController() == false)
	{
		return;
	}

	// 마이크 활성화
	PlayerController->StartTalking();

	// 마이크 활성화 여부를 나타내는 플래그 변경
	bIsMicrophonActive = true;
}

void UMD_VoiceChatComponent::StopTalking()
{
	// PlayerController(Owner)의 유효성 및 로컬 컨트롤러 검사
	APlayerController* PlayerController = OwnerPlayerController.Get();
	if (bIsMicrophonActive == true && IsValid(PlayerController) == true && PlayerController->IsLocalController() == true)
	{
		// 마이크 비활성화
		PlayerController->StopTalking();
	}

	// 마이크 활성화 여부를 나타내는 플래그 설정
	bIsMicrophonActive = false;
}
