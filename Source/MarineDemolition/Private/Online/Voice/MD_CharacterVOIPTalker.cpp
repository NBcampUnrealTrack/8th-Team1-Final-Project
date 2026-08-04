// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Voice/MD_CharacterVOIPTalker.h"

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
