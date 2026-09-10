// Fill out your copyright notice in the Description page of Project Settings.

#include "Online/Voice/MD_VoiceChatComponent.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Util/MD_LogUtility.h"

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
	// 게임 인스턴스에 복원된 송신 모드 적용
	ApplySavedPushToTalkSetting();

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
	StopTalkingInternal();

	Super::Deactivate();
}

void UMD_VoiceChatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTalkingInternal();

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
		StopTalkingInternal();
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
		UE_LOG(MarineDemolition, Log, TEXT("%s 컴포넌트 비활성화 또는 이미 마이크 활성화 상태"), *UE_CALL_LOCATIONINFO);
		return;
	}

	// PlayerController(Owner)의 유효성 및 로컬 컨트롤러 검사
	APlayerController* PlayerController = OwnerPlayerController.Get();
	if (IsValid(PlayerController) == false || PlayerController->IsLocalController() == false)
	{
		UE_LOG(MarineDemolition, Log, TEXT("%s 잘못된 Player Controller 또는 Remote Player Controller"), *UE_CALL_LOCATIONINFO);
		return;
	}

	UE_LOG(MarineDemolition, Log, TEXT("%s 마이크 활성화"), *UE_CALL_LOCATIONINFO);

	// 지연 생성된 온라인 음성 엔진에 확정된 입력 장치 적용
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 설정 매니저가 존재하는 로컬 플레이어 처리
	if (IsValid(SettingsManager) == true)
	{
		// 실제 적용된 입력 선택을 소유한 카테고리 조회
		UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
		// 송신 시작 전 선택 장치의 캡처 준비
		if (IsValid(AudioCategory) == true)
		{
			// UI의 미적용 편집값과 분리된 입력 선택 반영
			AudioCategory->ApplyInputDeviceSettings();
		}
	}

	// 마이크 활성화
	PlayerController->StartTalking();

	// 마이크 활성화 여부를 나타내는 플래그 변경
	bIsMicrophonActive = true;
}

void UMD_VoiceChatComponent::StopTalking()
{
	// 항상 켜기 모드에서 입력 키 해제가 송신을 끄는 동작 차단
	if (bUsePushToTalk == false)
	{
		return;
	}

	StopTalkingInternal();
}

void UMD_VoiceChatComponent::ApplySavedPushToTalkSetting()
{
	// 로컬 플레이어의 마이크 설정에만 적용되는 소유자 확인
	APlayerController* PlayerController = OwnerPlayerController.Get();
	if (IsValid(PlayerController) == false)
	{
		return;
	}
	// 원격 컨트롤러에 로컬 송신 모드를 복원하는 동작 방지
	bool bIsLocalController = PlayerController->IsLocalController();
	if (bIsLocalController == false)
	{
		return;
	}

	// 현재 게임 인스턴스의 설정 매니저 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	if (IsValid(SettingsManager) == false)
	{
		return;
	}

	// 복원 완료된 오디오 설정 인스턴스 조회
	UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
	if (IsValid(AudioCategory) == false)
	{
		return;
	}

	// 로컬 음성 컴포넌트의 저장된 송신 모드 복원
	SetPushToTalk(AudioCategory->Settings.bUsePushToTalk);
}

void UMD_VoiceChatComponent::StopTalkingInternal()
{
	if (bIsMicrophonActive == false)
	{
		return;
	}

	// PlayerController(Owner)의 유효성 및 로컬 컨트롤러 검사
	APlayerController* PlayerController = OwnerPlayerController.Get();
	if (IsValid(PlayerController) == false || PlayerController->IsLocalController() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 잘못된 Player Controller 또는 Remote Player Controller"), *UE_CALL_LOCATIONINFO);
		bIsMicrophonActive = false;
		return;
	}

	UE_LOG(MarineDemolition, Log, TEXT("%s 마이크 비활성화"), *UE_CALL_LOCATIONINFO);

	// 마이크 비활성화
	PlayerController->StopTalking();

	// 마이크 활성화 여부를 나타내는 플래그 설정
	bIsMicrophonActive = false;
}

bool UMD_VoiceChatComponent::MuteRemotePlayer(APlayerState* TargetPlayerState, bool bNewMute)
{
	// 해당 PlayerState에 대하여 Mute/UnMute 변경이 가능한지 검사
	if (CanModifyRemotePlayerMute(TargetPlayerState) == false)
	{
		UE_LOG(MarineDemolition, Log, TEXT("%s 잘못된 TargetPlayerState 또는 LocallPlayerController가 아닌 곳에서 호출됨"), *UE_CALL_LOCATIONINFO);
		return false;
	}

	UE_LOG(MarineDemolition, Log, TEXT("%s TargetPlayerState의 Mute 값 변경"), *UE_CALL_LOCATIONINFO);

	// 설정 값에 따라 플레이어 Mute 또는 UnMute 요청
	if (bNewMute == true)
	{
		OwnerPlayerController->ServerMutePlayer(TargetPlayerState->GetUniqueId());
	}
	else
	{
		OwnerPlayerController->ServerUnmutePlayer(TargetPlayerState->GetUniqueId());
	}
	return true;
}

bool UMD_VoiceChatComponent::IsRemotePlayerMuted(APlayerState* TargetPlayerState) const
{
	// 해당 PlayerState에 대하여 Mute/UnMute 변경이 가능한지 검사
	if (CanModifyRemotePlayerMute(TargetPlayerState) == false)
	{
		return false;
	}

	// 플레이어의 Mute 상태 반환
	return OwnerPlayerController->IsPlayerMuted(*TargetPlayerState->GetUniqueId());
}

bool UMD_VoiceChatComponent::CanModifyRemotePlayerMute(APlayerState* TargetPlayerState) const
{
	// Onwer PlayerController의 유효성 및 로컬 컨트롤러 검사
	APlayerController* PlayerController = OwnerPlayerController.Get();
	if (IsValid(PlayerController) == false || PlayerController->IsLocalController() == false)
	{
		return false;
	}

	// 상태를 변경하려는 PlayerState의 유효성 및 로컬 PlayerState 검사
	if (IsValid(TargetPlayerState) == false || TargetPlayerState == PlayerController->PlayerState)
	{
		return false;
	}

	return TargetPlayerState->GetUniqueId().IsValid();
}
