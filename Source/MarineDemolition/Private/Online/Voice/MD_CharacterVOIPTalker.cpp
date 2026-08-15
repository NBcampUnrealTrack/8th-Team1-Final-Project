// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Voice/MD_CharacterVOIPTalker.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
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
	VolumeMultiplier = FMath::Clamp(NewVolumeMultiplier, 0, 4);

	if (IsValid(ActiveVoiceAudioComponent) == true)
	{
		ActiveVoiceAudioComponent->SetVolumeMultiplier(VolumeMultiplier);
	}
}
