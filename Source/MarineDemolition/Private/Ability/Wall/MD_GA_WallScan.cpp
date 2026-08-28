// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallScan.h"

#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "TimerManager.h"
#include "Audio/MD_AudioRelayComponent.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Character/MD_Character.h"
#include "Gameplay/Scanning/MD_Scannable.h"
#include "GameplayTags/MD_GameplayTags.h"

UMD_GA_WallScan::UMD_GA_WallScan()
{
	ScanPulseSoundTag = FGameplayTag::RequestGameplayTag(TEXT("SFX.Tool.Scanner.PulseLoop"), /*ErrorIfNotFound*/ false);
	RequiredTargetTags.AddTag(MDGameplayTags::Interaction_Scannable);

	ScanObjectTypes = {
		UEngineTypes::ConvertToObjectType(ECC_WorldStatic),
		UEngineTypes::ConvertToObjectType(ECC_WorldDynamic),
		UEngineTypes::ConvertToObjectType(ECC_PhysicsBody)
	};
}

bool UMD_GA_WallScan::IsLocationInsideScanCone(
	const FVector& ViewLocation,
	const FVector& ViewForward,
	const FVector& TargetLocation,
	const float Radius,
	const float ConeHalfAngleDegrees)
{
	if (Radius <= 0.0f || ViewForward.IsNearlyZero())
	{
		return false;
	}

	const FVector ToTarget = TargetLocation - ViewLocation;
	const double DistanceSquared = ToTarget.SizeSquared();
	if (DistanceSquared > FMath::Square(static_cast<double>(Radius)))
	{
		return false;
	}

	// 카메라 위치와 같은 지점은 방향을 정의할 수 없지만 원뿔 꼭짓점 안으로 본다.
	if (DistanceSquared <= UE_DOUBLE_SMALL_NUMBER)
	{
		return true;
	}

	const double ClampedHalfAngle = FMath::Clamp(static_cast<double>(ConeHalfAngleDegrees), 0.0, 180.0);
	const double MinimumAlignment = FMath::Cos(FMath::DegreesToRadians(ClampedHalfAngle));
	const double Alignment = FVector::DotProduct(ViewForward.GetSafeNormal(), ToTarget.GetSafeNormal());
	return Alignment + UE_DOUBLE_KINDA_SMALL_NUMBER >= MinimumAlignment;
}

bool UMD_GA_WallScan::FindScanTargets(
	const FGameplayAbilityActorInfo* ActorInfo,
	TArray<AActor*>& OutTargets,
	FVector& OutScanOrigin,
	FVector& OutScanForward) const
{
	OutTargets.Reset();
	OutScanOrigin = FVector::ZeroVector;
	OutScanForward = FVector::ZeroVector;

	const APawn* Avatar = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	const AController* Controller = Avatar ? Avatar->GetController() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !Controller || !World || ScanRadius <= 0.0f || ScanObjectTypes.IsEmpty())
	{
		return false;
	}

	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(OutScanOrigin, ViewRotation);
	OutScanForward = ViewRotation.Vector().GetSafeNormal();

	TArray<AActor*> Candidates;
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(const_cast<APawn*>(Avatar));
	UKismetSystemLibrary::SphereOverlapActors(
		World,
		OutScanOrigin,
		ScanRadius,
		ScanObjectTypes,
		AActor::StaticClass(),
		ActorsToIgnore,
		Candidates);

	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate)
			|| !IsLocationInsideScanCone(
				OutScanOrigin,
				OutScanForward,
				Candidate->GetActorLocation(),
				ScanRadius,
				ScanConeHalfAngleDegrees)
			|| !ActorHasAnyTargetTag(Candidate, RequiredTargetTags)
			|| !Candidate->GetClass()->ImplementsInterface(UMD_Scannable::StaticClass()))
		{
			continue;
		}

		OutTargets.AddUnique(Candidate);
	}

	return true;
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
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!World || !IsValid(Avatar))
	{
		return;
	}

	// 이 어빌리티는 쿨다운이 없어 3초 안에 다시 스캔할 수 있다.
	// 그대로 두면 펄스가 겹쳐 울리므로 직전 것을 먼저 끊는다.
	// InstancedPerActor 라 핸들·타이머가 플레이어별로 유지된다 (MD_GA_WallBase 생성자).
	StopScanPulse();

	UMD_AudioSubsystem* const Audio = World->GetSubsystem<UMD_AudioSubsystem>();
	const APawn* const AvatarPawn = Cast<APawn>(Avatar);
	if (Audio && AvatarPawn && AvatarPawn->IsLocallyControlled())
	{
		ScanPulseHandle = Audio->PlayEventAttached(ScanPulseSoundTag, Avatar->GetRootComponent());
	}

	UMD_AudioRelayComponent* const Relay = GetAudioRelay();
	if (Relay && Avatar->HasAuthority())
	{
		Relay->PlayAttachedForAll(ScanPulseSoundTag);
		bScanPulseRelayed = true;
	}

	if (!ScanPulseHandle.IsValid() && !bScanPulseRelayed)
	{
		return;
	}

	// EndAbility 는 이 어빌리티 객체에 바인딩된 타이머를 지운다. 따라서 일반 UObject
	// 델리게이트 대신 약한 참조를 캡처한 람다를 써서 하이라이트 종료 시점까지 유지한다.
	const TWeakObjectPtr<UMD_GA_WallScan> WeakAbility(this);
	const TWeakObjectPtr<UMD_AudioSubsystem> WeakAudio(Audio);
	const TWeakObjectPtr<UMD_AudioRelayComponent> WeakRelay(Relay);
	const FMD_AudioHandle PulseHandle = ScanPulseHandle;
	const bool bPulseRelayed = bScanPulseRelayed;
	const FGameplayTag PulseSoundTag = ScanPulseSoundTag;
	const float FadeOutSeconds = ScanPulseFadeOutSeconds;
	World->GetTimerManager().SetTimer(
		ScanPulseStopTimer,
		FTimerDelegate::CreateLambda(
			[WeakAbility, WeakAudio, WeakRelay, PulseHandle, bPulseRelayed, PulseSoundTag, FadeOutSeconds]()
		{
			if (UMD_GA_WallScan* Ability = WeakAbility.Get())
			{
				Ability->StopScanPulse();
				return;
			}

			if (PulseHandle.IsValid())
			{
				if (UMD_AudioSubsystem* AudioSubsystem = WeakAudio.Get())
				{
					AudioSubsystem->StopHandle(PulseHandle, FadeOutSeconds);
				}
			}

			if (bPulseRelayed)
			{
				if (UMD_AudioRelayComponent* AudioRelay = WeakRelay.Get())
				{
					AudioRelay->StopAttachedForAll(PulseSoundTag, FadeOutSeconds);
				}
			}
		}),
		ScanDuration,
		/*bLoop*/ false);
}

void UMD_GA_WallScan::StopScanPulse()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(ScanPulseStopTimer);
	}

	if (ScanPulseHandle.IsValid())
	{
		if (UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr)
		{
			Audio->StopHandle(ScanPulseHandle, ScanPulseFadeOutSeconds);
		}
	}

	if (bScanPulseRelayed)
	{
		if (UMD_AudioRelayComponent* const Relay = GetAudioRelay())
		{
			Relay->StopAttachedForAll(ScanPulseSoundTag, ScanPulseFadeOutSeconds);
		}
	}

	ScanPulseHandle = FMD_AudioHandle();
	bScanPulseRelayed = false;
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

	APawn* Avatar = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Avatar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	TArray<AActor*> ScanTargets;
	FVector ScanOrigin;
	FVector ScanForward;
	FindScanTargets(ActorInfo, ScanTargets, ScanOrigin, ScanForward);
	if (!ScanTargets.IsEmpty())
	{
		if (Avatar->IsLocallyControlled())
		{
			for (AActor* Target : ScanTargets)
			{
				// Execute 경로를 사용해야 Blueprint 전용 인터페이스 구현도 호출된다.
				IMD_Scannable::Execute_OnScanned(Target, ScanDuration, Avatar);
			}

			// 스윕 연출 시작 — MPC는 월드 전역이라 스캔한 플레이어 로컬 머신에서만 세팅
			if (ScanMPC)
			{
				if (UMaterialParameterCollectionInstance* MPCInst = GetWorld()->GetParameterCollectionInstance(ScanMPC))
				{
					MPCInst->SetVectorParameterValue(TEXT("ScanCenter"), FLinearColor(ScanOrigin));
					MPCInst->SetVectorParameterValue(TEXT("ScanForward"), FLinearColor(ScanForward));
					MPCInst->SetScalarParameterValue(TEXT("ScanRadius"), ScanRadius);
					const float ClampedHalfAngle = FMath::Clamp(ScanConeHalfAngleDegrees, 0.0f, 180.0f);
					MPCInst->SetScalarParameterValue(
						TEXT("ScanConeCosine"),
						FMath::Cos(FMath::DegreesToRadians(ClampedHalfAngle)));
					// 다른 파라미터를 모두 갱신한 뒤 머티리얼 Time 기준 시작 트리거를 마지막에 쓴다.
					MPCInst->SetScalarParameterValue(TEXT("ScanStartTime"), GetWorld()->GetTimeSeconds());
				}
			}
		}

		// 로컬에서는 즉시 재생하고, 서버 실행에서는 다른 클라이언트에 중계한다.
		// 어느 쪽도 실제 스캔 대상이 있을 때만 시작한다.
		PlayScanPulse();
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UMD_AudioRelayComponent* UMD_GA_WallScan::GetAudioRelay() const
{
	const AMD_Character* const Avatar = Cast<AMD_Character>(GetAvatarActorFromActorInfo());
	return Avatar ? Avatar->GetAudioRelayComponent() : nullptr;
}
