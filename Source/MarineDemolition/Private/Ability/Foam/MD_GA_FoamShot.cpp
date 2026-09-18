#include "Ability/Foam/MD_GA_FoamShot.h"

#include "Audio/MD_AudioSubsystem.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_FoamGunComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "World/Foam/MD_FoamProjectile.h"

namespace
{
	// 실패 사유 노출 — PlaceSupport와 같은 임시 방식
	void ShowFoamFailMessage(const FString& Reason)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, Reason);
		}
	}
}

UMD_GA_FoamShot::UMD_GA_FoamShot()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 기본 태그. 대응 정의는 DA_MD_Audio_Hazard 에 있고, 없으면 조용히 넘어간다
	EmptySoundTag = FGameplayTag::RequestGameplayTag(
		TEXT("SFX.Hazard.FoamGun.Empty"), /*ErrorIfNotFound*/ false);
}

void UMD_GA_FoamShot::PlayLocalSound(const FGameplayTag& EventTag, const AActor* Avatar) const
{
	const APawn* const AvatarPawn = Cast<APawn>(Avatar);
	if (!EventTag.IsValid() || !IsValid(AvatarPawn) || !AvatarPawn->IsLocallyControlled())
	{
		return;
	}

	const UWorld* const World = GetWorld();
	if (UMD_AudioSubsystem* Audio = World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr)
	{
		Audio->PlayEventAtLocation(EventTag, AvatarPawn->GetActorLocation());
	}
}

UMD_FoamGunComponent* UMD_GA_FoamShot::GetFoamGun(const FGameplayAbilityActorInfo* ActorInfo)
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	return Avatar ? Avatar->FindComponentByClass<UMD_FoamGunComponent>() : nullptr;
}

bool UMD_GA_FoamShot::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                         const FGameplayAbilityActorInfo* ActorInfo,
                                         const FGameplayTagContainer* SourceTags,
                                         const FGameplayTagContainer* TargetTags,
                                         FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const UMD_FoamGunComponent* FoamGun = GetFoamGun(ActorInfo);
	return FoamGun && FoamGun->GetProjectileClass() != nullptr;
}

void UMD_GA_FoamShot::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo,
                                      const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AMD_Character* Character = Cast<AMD_Character>(ActorInfo->AvatarActor.Get());
	UMD_FoamGunComponent* FoamGun = GetFoamGun(ActorInfo);
	if (!Character || !FoamGun)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 클라이언트도 같은 검사로 즉시 피드백을 준다. 실소모는 아래 서버 블록에서
	if (!FoamGun->HasChargeForShot())
	{
		ShowFoamFailMessage(TEXT("폼 충전이 부족합니다 (카트리지 필요)"));

		// 헛방 찰칵. 충전이 없다는 것을 화면 글자 말고 소리로도 알려 준다
		PlayLocalSound(EmptySoundTag, Character);

		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 쏜 본인은 여기서 바로 듣는다. 중계만 믿으면 왕복 지연이 붙는 정도가 아니라
	// 로컬 조작 폰은 멀티캐스트에서 걸러져 아예 소리가 나지 않는다
	PlayLocalSound(FireSoundTag, Character);

	// 발사체는 로컬 플레이어가 보는 총구(1인칭 도구 소켓)에서 시작해야 하므로 로컬 측이 원점·방향을
	// 계산해 서버에 요청한다. 충전 소모·스폰·발사음 중계는 서버 RPC 안에서 처리하고, 호스트는 즉시 실행된다.
	// 원격 폰의 서버 측 활성화는 여기서 아무것도 스폰하지 않는다 (이중 발사 방지)
	if (Character->IsLocallyControlled())
	{
		FVector Origin;
		FVector AimDir;
		FoamGun->ComputeLaunch(Origin, AimDir);
		FoamGun->ServerFire(Origin, AimDir, FireSoundTag);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
