// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MD_VoiceChatComponent.generated.h"

class APlayerState;

/**
 * Owns the local PlayerController's network voice transmission state.
 * This component is intentionally not replicated because microphone preferences are local.
 */
UCLASS(ClassGroup = (VoiceChat), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_VoiceChatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_VoiceChatComponent();

	virtual void BeginPlay() override;

	virtual void Activate(bool bReset = false) override;

	virtual void Deactivate() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	// 눌러서 말하기 옵션
	// True : 특정 키를 누르고 있는 경우에만 음성 전송
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice Chat")
	bool bUsePushToTalk = true;

	// 현재 음성 입력 키가 입력중인지를 나타내는 변수
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Voice Chat")
	bool bIsMicrophonActive = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Voice Chat")
	TWeakObjectPtr<APlayerController> OwnerPlayerController;

public:
	UFUNCTION(BlueprintPure, Category = "Voice Chat")
	FORCEINLINE bool IsPushToTalkEnabled() const { return bUsePushToTalk; }

	UFUNCTION(BlueprintPure, Category = "Voice Chat")
	FORCEINLINE bool GetMicrophoneIsActive() const { return bIsMicrophonActive; }

	UFUNCTION(BlueprintPure, Category = "Voice Chat")
	FORCEINLINE bool IsVoiceActivated() const { return IsActive() && bIsMicrophonActive; }

	UFUNCTION(BlueprintCallable, Category = "Voice Chat")
	void SetPushToTalk(const bool bNewUsePushToTalk);

	UFUNCTION(BlueprintCallable, Category = "Voice Chat")
	void StartTalking();

	UFUNCTION(BlueprintCallable, Category = "Voice Chat")
	void StopTalking();

	// 임의 Romote Player의 음성을 Mute/UnMute 하도록 요청하는 함수
	UFUNCTION(BlueprintCallable, Category = "Voice Chat")
	bool MuteRemotePlayer(APlayerState* TargetPlayerState, bool bNewMute);

	// 임의 Remote Player의 음성이 Mute 처리 되었는지를 반환하는 함수
	UFUNCTION(BlueprintPure, Category = "Voice Chat")
	bool IsRemotePlayerMuted(APlayerState* TargetPlayerState) const;

protected:
	bool CanModifyRemotePlayerMute(APlayerState* TargetPlayerState) const;
};
