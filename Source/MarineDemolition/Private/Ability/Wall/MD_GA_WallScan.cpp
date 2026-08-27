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

	// 재생과 정지를 모두 중계 컴포넌트에 맡긴다.
	//
	// 예전에는 이 어빌리티가 직접 재생하고 자기 인스턴스에 3초 타이머를 걸었는데,
	// 즉발 어빌리티라 EndAbility 직후 인스턴스가 회수되면 타이머만 조용히 죽어
	// 펄스가 영원히 울렸다. 컴포넌트는 아바타와 수명을 같이하므로 안전하다.
	if (UMD_AudioRelayComponent* const Relay = GetAudioRelay())
	{
		Relay->PlayAttachedForDuration(ScanPulseSoundTag, ScanDuration, ScanPulseFadeOutSeconds);
	}
}

void UMD_GA_WallScan::StopScanPulse()
{
	if (UMD_AudioRelayComponent* const Relay = GetAudioRelay())
	{
		Relay->StopEverywhere(ScanPulseSoundTag, ScanPulseFadeOutSeconds);
	}
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
