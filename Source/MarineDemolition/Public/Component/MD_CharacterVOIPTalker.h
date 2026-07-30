// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
#include "MD_CharacterVOIPTalker.generated.h"

/**
 * 
 */
UCLASS(meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_CharacterVOIPTalker : public UVOIPTalker
{
	GENERATED_BODY()
	
public:
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveVoiceAudioComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float VolumeMultiplier = 1.5f;

public:
	UFUNCTION(BlueprintCallable, Category = "Voice Chat")
	void SetVolumeMuliplier(float NewVolumeMultiplier);

};
