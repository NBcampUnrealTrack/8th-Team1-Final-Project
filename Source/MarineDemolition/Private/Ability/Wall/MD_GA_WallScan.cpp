// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Wall/MD_GA_WallScan.h"

#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Audio/MD_AudioRelayComponent.h"
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
