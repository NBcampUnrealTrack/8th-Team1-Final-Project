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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// 새 접속과 별도 설정이 없는 플레이어의 기본 음성 재생 배율
	static constexpr float DefaultVolumeMultiplier = 1.5f;

	// 개별 플레이어 음성 재생에 허용되는 최대 배율
	static constexpr float MaxVolumeMultiplier = 2.0f;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveVoiceAudioComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float VolumeMultiplier = DefaultVolumeMultiplier;

public:
	// 현재 개별 플레이어 음성 재생 배율 반환
	UFUNCTION(BlueprintPure, Category = "Voice Chat")
	FORCEINLINE float GetVolumeMultiplier() { return VolumeMultiplier; }

	UFUNCTION(BlueprintCallable, Category = "Voice Chat")
	void SetVolumeMuliplier(float NewVolumeMultiplier);

private:
	void TryRegisterWithOwnerPlayerState();

	FTimerHandle PlayerStateRetryTimerHandle;
	bool bVoiceRegistrationComplete = false;

};
