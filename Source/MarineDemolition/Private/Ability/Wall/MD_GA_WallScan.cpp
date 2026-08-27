// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallScan.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "TimerManager.h"
#include "Audio/MD_AudioRelayComponent.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Character/MD_Character.h"
#include "World/Wall/MD_CuttableWall.h"

UMD_GA_WallScan::UMD_GA_WallScan()
{
	ScanPulseSoundTag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Tool.Scanner.PulseLoop"), /*ErrorIfNotFound*/ false);
}

void UMD_GA_WallScan::PlayScanPulse()
{
	// DA_MD_Audio_Tool 의 펄스 항목은 루프다. 끊을 시점이 없으면 영원히 남으므로
	// ScanDuration 이 0 이하면 아예 재생하지 않는다.
	if (!ScanPulseSoundTag.IsValid() || ScanDuration <= 0.f)
	{
		return;
	}

	UWorld* World = GetWorld();
	UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Audio || !IsValid(Avatar))
	{
		return;
	}

	// 이 어빌리티는 쿨다운이 없어 3초 안에 다시 스캔할 수 있다.
	// 그대로 두면 펄스가 겹쳐 울리므로 직전 것을 먼저 끊는다.
	// InstancedPerActor 라 핸들·타이머가 플레이어별로 유지된다 (MD_GA_WallBase 생성자).
	StopScanPulse();

	ScanPulseHandle = Audio->PlayEventAttached(ScanPulseSoundTag, Avatar->GetRootComponent());
	// 어빌리티가 안 도는 머신에는 중계로 알린다
	if (UMD_AudioRelayComponent* const Relay = GetAudioRelay())
	{
		Relay->PlayAttachedForAll(ScanPulseSoundTag);
	}
	if (!ScanPulseHandle.IsValid())
	{
		return;
	}

	// 즉발 어빌리티라 곧 EndAbility 가 불리지만 어빌리티 인스턴스는 살아 있으므로
	// 자기 타이머를 들고 있어도 된다. EndAbility 에서 끊지 않는 것은 의도다 —
	// 하이라이트가 남아 있는 3초 동안 소리도 같이 가야 한다.
	World->GetTimerManager().SetTimer(
		ScanPulseStopTimer, this, &UMD_GA_WallScan::StopScanPulse, ScanDuration, /*bLoop*/ false);
}

void UMD_GA_WallScan::StopScanPulse()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(ScanPulseStopTimer);
	}

	if (!ScanPulseHandle.IsValid())
	{
		return;
	}

	if (UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr)
	{
		Audio->StopHandle(ScanPulseHandle, ScanPulseFadeOutSeconds);
		if (UMD_AudioRelayComponent* const Relay = GetAudioRelay())
		{
			Relay->StopAttachedForAll(ScanPulseSoundTag, ScanPulseFadeOutSeconds);
		}
	}

	ScanPulseHandle = FMD_AudioHandle();
}

void UMD_GA_WallScan::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (AMD_CuttableWall* Wall = GetAimedWall(ActorInfo))
	{
		// 스캐너를 전달 → 실루엣은 스캔한 플레이어 로컬 클라에서만 노출
		Wall->ScanHighlight(ScanDuration, ActorInfo->AvatarActor.Get());

		// 펄스음은 실제로 벽을 잡았을 때만 낸다 — 허공에 쏘고 소리가 나면
		// 결과 없는 피드백이 되어 판정을 오해하게 만든다
		PlayScanPulse();

		// 스윕 연출 시작 — MPC는 월드 전역이라 스캔한 플레이어 로컬 머신에서만 세팅
		const APawn* Avatar = Cast<APawn>(ActorInfo->AvatarActor.Get());
		if (ScanMPC && Avatar && Avatar->IsLocallyControlled())
		{
			if (UMaterialParameterCollectionInstance* MPCInst = GetWorld()->GetParameterCollectionInstance(ScanMPC))
			{
				FVector Origin, Extent;
				Wall->GetActorBounds(false, Origin, Extent);
				// 머티리얼 Time 노드와 같은 게임 시간 기준
				MPCInst->SetScalarParameterValue(TEXT("ScanStartTime"), GetWorld()->GetTimeSeconds());
				MPCInst->SetVectorParameterValue(TEXT("ScanBoxCenter"), FLinearColor(Origin));
				// 격자가 벽 가장자리에서 잘리지 않게 여유 패딩
				MPCInst->SetVectorParameterValue(TEXT("ScanBoxExtent"), FLinearColor(Extent + FVector(30.f)));
			}
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UMD_AudioRelayComponent* UMD_GA_WallScan::GetAudioRelay() const
{
	const AMD_Character* const Avatar = Cast<AMD_Character>(GetAvatarActorFromActorInfo());
	return Avatar ? Avatar->GetAudioRelayComponent() : nullptr;
}
