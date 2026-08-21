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

void UMD_GA_WallCut::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                     const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
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
	if (!bHasAimedWallHit || !IsValid(Wall) || !Wall->CanCut())
	{
		return;
	}

	// AddCutProgress가 서버 권한을 검사하므로 클라이언트 호출은 무시된다.
	// 클라이언트는 복제된 CutProgressSeconds로 UI만 그린다 (기획 §7-5)
	Wall->AddCutProgressFromHit(
		ProgressInterval * GetCutSpeedMultiplier(),
		ActorInfo->AvatarActor.Get(),
		WallHit);
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
