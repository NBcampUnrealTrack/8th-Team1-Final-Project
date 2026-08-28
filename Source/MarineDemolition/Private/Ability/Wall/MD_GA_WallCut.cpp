// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallCut.h"

#include "World/Wall/MD_CuttableWall.h"

#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Player/MD_PlayerState.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Audio/MD_AudioRelayComponent.h"
#include "Components/SceneComponent.h"

UMD_GA_WallCut::UMD_GA_WallCut()
{
	// 기본 태그. 대응 정의는 DA_MD_Audio_Cut 에 있고, 없으면 조용히 넘어간다
	CutStartSoundTag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Cut.Cutter.Start"), /*ErrorIfNotFound*/ false);
	CutLoopSoundTag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Cut.Cutter.Loop"), /*ErrorIfNotFound*/ false);
	CutStopSoundTag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Cut.Cutter.Stop"), /*ErrorIfNotFound*/ false);
}

void UMD_GA_WallCut::StartCutAudio()
{
	const UWorld* World = GetWorld();
	UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Audio || !IsValid(Avatar))
	{
		return;
	}

	USceneComponent* AttachTo = Avatar->GetRootComponent();

	// 이 어빌리티는 남의 머신에서 돌지 않는다. 조작 중인 본인은 여기서 바로
	// 재생하고(왕복 지연 없이), 다른 클라이언트에게는 중계로 알린다
	UMD_AudioRelayComponent* const Relay = GetAudioRelay();

	if (CutStartSoundTag.IsValid())
	{
		Audio->PlayEventAttached(CutStartSoundTag, AttachTo);
		if (Relay)
		{
			Relay->PlayAttachedForAll(CutStartSoundTag);
		}
	}

	// 루프는 핸들을 들고 있다가 EndAbility 에서 끈다
	if (CutLoopSoundTag.IsValid() && !CutLoopHandle.IsValid())
	{
		CutLoopHandle = Audio->PlayEventAttached(CutLoopSoundTag, AttachTo);
		if (Relay)
		{
			Relay->PlayAttachedForAll(CutLoopSoundTag);
		}
	}
}

UMD_AudioRelayComponent* UMD_GA_WallCut::GetAudioRelay() const
{
	const AMD_Character* const Avatar = Cast<AMD_Character>(GetAvatarActorFromActorInfo());
	return Avatar ? Avatar->GetAudioRelayComponent() : nullptr;
}

void UMD_GA_WallCut::StopCutAudio()
{
	const UWorld* World = GetWorld();
	UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
	if (!Audio)
	{
		CutLoopHandle = FMD_AudioHandle();
		return;
	}

	UMD_AudioRelayComponent* const Relay = GetAudioRelay();

	if (CutLoopHandle.IsValid())
	{
		Audio->StopHandle(CutLoopHandle, CutLoopFadeOutSeconds);
		CutLoopHandle = FMD_AudioHandle();
	}
	if (Relay && CutLoopSoundTag.IsValid())
	{
		Relay->StopAttachedForAll(CutLoopSoundTag, CutLoopFadeOutSeconds);
	}

	// 정지음은 아바타가 사라진 뒤에도 남을 수 있어 위치로 재생한다
	if (CutStopSoundTag.IsValid())
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			Audio->PlayEventAtLocation(CutStopSoundTag, Avatar->GetActorLocation());
		}
		if (Relay)
		{
			Relay->PlayAtOwnerLocationForAll(CutStopSoundTag);
		}
	}
}

namespace
{
	/**
	 * 절단이 끝났는지.
	 *
	 * 서버는 bCut(CanCut) 으로 즉시 알 수 있지만 그 값은 복제되지 않는다.
	 * 클라이언트에서도 몽타주가 멈춰야 하므로 복제되는 진행 비율을 함께 본다.
	 */
	bool IsCutFinished(const AMD_CuttableWall* Wall)
	{
		if (!IsValid(Wall))
		{
			return true;
		}

		return !Wall->CanCut() || Wall->GetCutProgressRatio() >= 1.0f;
	}
}

bool UMD_GA_WallCut::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// 자를 수 있는 대상을 보고 있을 때만 발동한다.
	// 여기서 막으면 몽타주도 VFX도 시작되지 않는다 (허공 절단 방지)
	AActor* Target = nullptr;
	FHitResult Hit;
	if (!TryGetAimedTargetHit(ActorInfo, Target, Hit))
	{
		return false;
	}

	// 이미 잘린 격벽에는 다시 발동하지 않는다.
	// 격벽이 아닌 대상(퀘스트 오브젝트)은 이 검사를 건너뛴다
	if (const AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(Target))
	{
		return !IsCutFinished(Wall);
	}

	return true;
}

void UMD_GA_WallCut::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                     const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
	AActor* Target = nullptr;
	FHitResult TargetHit;
	if (!TryGetAimedTargetHit(ActorInfo, Target, TargetHit))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ProgressTimerHandle, this, &UMD_GA_WallCut::TickCutProgress, ProgressInterval, true);
	}

	// 서버 값이 사본 프록시로 복제되고, 소유 클라는 지연 없이 즉시 켠다
	if (AMD_Character* Character = Cast<AMD_Character>(GetAvatarActorFromActorInfo()))
	{
		Character->SetCutterVFXActive(true);
	}

	StartCutAudio();

	OnCutStarted();
}

void UMD_GA_WallCut::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProgressTimerHandle);
	}

	if (AMD_Character* Character = Cast<AMD_Character>(GetAvatarActorFromActorInfo()))
	{
		Character->SetCutterVFXActive(false);
	}

	StopCutAudio();

	OnCutStopped();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}



void UMD_GA_WallCut::TickCutProgress()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo)
	{
		return;
	}

	AMD_CuttableWall* Wall = nullptr;
	FHitResult WallHit;
	const bool bHasAimedWallHit = TryGetAimedWallHit(
		ActorInfo,
		Wall,
		WallHit);

	// 조준이 벗어나면 진행만 멈추고 어빌리티는 유지한다.
	// 전선·수도관을 확인하려고 잠깐 시선을 돌리는 정석 플레이를 처벌하지 않기 위해서다 (기획 §1)
	if (!bHasAimedWallHit || !IsValid(Wall))
	{
		return;
	}

	// 조준한 격벽이 이미 잘렸다면 계속 갈 이유가 없다
	if (IsCutFinished(Wall))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
			/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
		return;
	}

	// AddCutProgress가 서버 권한을 검사하므로 클라이언트 호출은 무시된다.
	// 클라이언트는 복제된 CutProgressSeconds로 UI만 그린다 (기획 §7-5)
	Wall->AddCutProgressFromHit(
		ProgressInterval * GetCutSpeedMultiplier(),
		ActorInfo->AvatarActor.Get(),
		WallHit);

	// 이번 틱에 절단이 끝났으면 몽타주·VFX·루프 사운드를 모두 정리한다
	if (IsCutFinished(Wall))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
			/*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
	}
}

float UMD_GA_WallCut::GetCutSpeedMultiplier() const
{
	float Multiplier = 1.0f;

	const AMD_Character* Character = Cast<AMD_Character>(GetAvatarActorFromActorInfo());
	if (!IsValid(Character))
	{
		return Multiplier;
	}

	// 고속 절단기(IT-C1) 등 장비 배율
	if (const UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent())
	{
		if (const UMD_ToolDefinition* Tool = Hotbar->GetCurrentTool())
		{
			Multiplier *= Tool->CutSpeedMultiplier;
		}
	}

	// 부상 시 작업 속도 저하 (기획 02 §6)
	if (const AMD_PlayerState* MDPlayerState = Character->GetPlayerState<AMD_PlayerState>())
	{
		if (MDPlayerState->GetHealthState() == EMD_HealthState::Injured)
		{
			Multiplier *= InjuredCutSpeedMultiplier;
		}
	}

	return Multiplier;
}
