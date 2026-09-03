// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Voice/MD_CharacterVOIPTalker.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Player/MD_PlayerState.h"
#include "TimerManager.h"

void UMD_CharacterVOIPTalker::BeginPlay()
{
	Super::BeginPlay();

	TryRegisterWithOwnerPlayerState();
	if (bVoiceRegistrationComplete == false)
	{
		if (UWorld* World = GetWorld())
		{
			// PlayerState 등록 재시도 주기
			float PlayerStateRetryIntervalSeconds = 0.2f;
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
	// 이미 완료된 PlayerState 등록의 중복 처리 방지
	if (bVoiceRegistrationComplete == true)
	{
		return;
	}

	// 컴포넌트 소유 Pawn 준비 여부 확인
	APawn* OwningPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwningPawn) == false)
	{
		return;
	}

	// 복제된 PlayerState 준비 여부 확인
	APlayerState* OwningState = OwningPawn->GetPlayerState();
	if (IsValid(OwningState) == false)
	{
		return;
	}

	// VOIP 등록에 필요한 플레이어 고유 식별자의 복제 완료 대기
	FUniqueNetIdRepl OwningPlayerUniqueId = OwningState->GetUniqueId();
	if (OwningPlayerUniqueId.IsValid() == false)
	{
		return;
	}

	// 고유 식별자가 준비된 PlayerState와 VOIP Talker 연결
	RegisterWithPlayerState(OwningState);
	// Pawn 재생성 전 PlayerState에 저장된 로컬 음량 조회
	AMD_PlayerState* ProjectPlayerState = Cast<AMD_PlayerState>(OwningState);
	if (IsValid(ProjectPlayerState) == true)
	{
		float StoredVolumeMultiplier = ProjectPlayerState->GetVoiceVolumeMultiplier();
		SetVolumeMuliplier(StoredVolumeMultiplier);
	}

	// PlayerState와 고유 식별자를 확인한 등록 완료 상태 저장
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
