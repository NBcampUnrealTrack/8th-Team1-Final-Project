// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Voice/MD_CharacterVOIPTalker.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Player/MD_PlayerState.h"
#include "TimerManager.h"

namespace
{
	constexpr float PlayerStateRetryIntervalSeconds = 0.2f;
	constexpr float VoiceMicThreshold = 0.003f;
}

void UMD_CharacterVOIPTalker::BeginPlay()
{
	Super::BeginPlay();

	TryRegisterWithOwnerPlayerState();
	if (bVoiceRegistrationComplete == false)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				PlayerStateRetryTimerHandle,
				this,
				&UMD_CharacterVOIPTalker::TryRegisterWithOwnerPlayerState,
				PlayerStateRetryIntervalSeconds,
				true);
		}
	}
}

void UMD_CharacterVOIPTalker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerStateRetryTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UMD_CharacterVOIPTalker::TryRegisterWithOwnerPlayerState()
{
	if (bVoiceRegistrationComplete)
	{
		return;
	}

	const APawn* OwningPawn = Cast<APawn>(GetOwner());
	APlayerState* OwningState = IsValid(OwningPawn) ? OwningPawn->GetPlayerState() : nullptr;
	if (IsValid(OwningState) == false)
	{
		return;
	}

	RegisterWithPlayerState(OwningState);
	UVOIPStatics::SetMicThreshold(VoiceMicThreshold);

	// Pawn 재생성 전 PlayerState에 저장된 로컬 음량 조회
	AMD_PlayerState* ProjectPlayerState = Cast<AMD_PlayerState>(OwningState);
	if (IsValid(ProjectPlayerState) == true)
	{
		float StoredVolumeMultiplier = ProjectPlayerState->GetVoiceVolumeMultiplier();
		SetVolumeMuliplier(StoredVolumeMultiplier);
	}

	bVoiceRegistrationComplete = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerStateRetryTimerHandle);
	}
}

void UMD_CharacterVOIPTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	Super::OnTalkingBegin(AudioComponent);

	ActiveVoiceAudioComponent = AudioComponent;

	if (IsValid(ActiveVoiceAudioComponent))
	{
		ActiveVoiceAudioComponent->SetVolumeMultiplier(VolumeMultiplier);
	}
}

void UMD_CharacterVOIPTalker::OnTalkingEnd()
{
	ActiveVoiceAudioComponent = nullptr;

	Super::OnTalkingEnd();
}

void UMD_CharacterVOIPTalker::SetVolumeMuliplier(float NewVolumeMultiplier)
{
	// UI 정규화 음량과 공통으로 사용하는 허용 배율 적용
	float ClampedVolumeMultiplier = FMath::Clamp(NewVolumeMultiplier, 0.0f, MaxVolumeMultiplier);
	VolumeMultiplier = ClampedVolumeMultiplier;

	if (IsValid(ActiveVoiceAudioComponent) == true)
	{
		ActiveVoiceAudioComponent->SetVolumeMultiplier(VolumeMultiplier);
	}
}
