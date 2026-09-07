// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
#include "MD_CharacterVOIPTalker.generated.h"

class UMD_TalkNotificationWidget;

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
	// 발화 중 HUD 검색 및 알림 유효성 확인 간격
	UPROPERTY(EditAnywhere, Category = "Voice Chat", meta = (ClampMin = "0.05"))
	float TalkNotificationRetryInterval = 0.5f;

	// 생성된 알림의 파괴를 감지하기 위한 약한 참조
	UPROPERTY(Transient)
	TWeakObjectPtr<UMD_TalkNotificationWidget> TalkNotificationWidget;

	// 발화가 유지되는 동안에만 실행하는 알림 확인 타이머
	FTimerHandle TalkNotificationRetryTimer;

	// 로컬 송신 상태와 Pawn 소유권 변경을 확인하는 타이머
	FTimerHandle LocalVoiceStateTimer;

	// 발화 종료 후 알림이 다시 생성되지 않도록 확인하는 상태
	bool bIsTalking = false;

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

protected:
	// 로컬 마이크 활성 상태를 현재 발화 알림에 반영
	void UpdateLocalVoiceState();

	// 발화 알림 검색 타이머 시작 및 최초 생성 요청
	void StartTalkNotification();

	// HUD 검색 후 알림 생성과 발화자 등록 및 HUD 추가를 순서대로 실행
	void TryCreateTalkNotification();

	// 발화 종료 시 타이머와 약한 참조 알림 정리
	void StopTalkNotification();

private:
	void TryRegisterWithOwnerPlayerState();

	FTimerHandle PlayerStateRetryTimerHandle;
	bool bVoiceRegistrationComplete = false;

};
