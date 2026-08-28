#include "Audio/MD_AudioRelayComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

UMD_AudioRelayComponent::UMD_AudioRelayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UMD_AudioSubsystem* UMD_AudioRelayComponent::GetAudio() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMD_AudioSubsystem>() : nullptr;
}

bool UMD_AudioRelayComponent::ShouldIgnoreMulticast() const
{
	// 조작 중인 본인은 어빌리티가 이미 직접 재생했다
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn && OwnerPawn->IsLocallyControlled();
}

// ------------------------------------------------------------------ 서버 진입점

void UMD_AudioRelayComponent::PlayAttachedForAll(const FGameplayTag& EventTag)
{
	if (EventTag.IsValid() && GetOwner() && GetOwner()->HasAuthority())
	{
		Multicast_PlayAttached(EventTag);
	}
}

void UMD_AudioRelayComponent::StopAttachedForAll(
	const FGameplayTag& EventTag, const float FadeOutSeconds)
{
	if (EventTag.IsValid() && GetOwner() && GetOwner()->HasAuthority())
	{
		Multicast_StopAttached(EventTag, FadeOutSeconds);
	}
}

void UMD_AudioRelayComponent::PlayAtOwnerLocationForAll(const FGameplayTag& EventTag)
{
	if (EventTag.IsValid() && GetOwner() && GetOwner()->HasAuthority())
	{
		Multicast_PlayAtLocation(EventTag, GetOwner()->GetActorLocation());
	}
}

// -------------------------------------------------------------------- 멀티캐스트

void UMD_AudioRelayComponent::Multicast_PlayAttached_Implementation(const FGameplayTag EventTag)
{
	if (ShouldIgnoreMulticast())
	{
		return;
	}

	UMD_AudioSubsystem* Audio = GetAudio();
	AActor* Owner = GetOwner();
	if (!Audio || !IsValid(Owner))
	{
		return;
	}

	// 같은 태그가 이미 돌고 있으면 갈아 끼운다. 겹쳐 울리는 것보다 낫다
	if (const FMD_AudioHandle* Existing = RelayedLoops.Find(EventTag))
	{
		Audio->StopHandle(*Existing, -1.0f);
	}

	const FMD_AudioHandle Handle =
		Audio->PlayEventAttached(EventTag, Owner->GetRootComponent());
	if (Handle.IsValid())
	{
		// 유효한 핸들은 루프뿐이다. 원샷은 스스로 끝나므로 보관하지 않는다
		RelayedLoops.Add(EventTag, Handle);
	}
	else
	{
		RelayedLoops.Remove(EventTag);
	}
}

void UMD_AudioRelayComponent::Multicast_StopAttached_Implementation(
	const FGameplayTag EventTag, const float FadeOutSeconds)
{
	if (ShouldIgnoreMulticast())
	{
		return;
	}

	FMD_AudioHandle Handle;
	if (!RelayedLoops.RemoveAndCopyValue(EventTag, Handle))
	{
		return;
	}

	if (UMD_AudioSubsystem* Audio = GetAudio())
	{
		Audio->StopHandle(Handle, FadeOutSeconds);
	}
}

void UMD_AudioRelayComponent::Multicast_PlayAtLocation_Implementation(
	const FGameplayTag EventTag, const FVector_NetQuantize Location)
{
	if (ShouldIgnoreMulticast())
	{
		return;
	}

	if (UMD_AudioSubsystem* Audio = GetAudio())
	{
		Audio->PlayEventAtLocation(EventTag, Location);
	}
}

void UMD_AudioRelayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 아바타가 사라지는데 중계 루프가 남으면 멈출 수단이 없어진다
	if (UMD_AudioSubsystem* Audio = GetAudio())
	{
		for (const TPair<FGameplayTag, FMD_AudioHandle>& Pair : RelayedLoops)
		{
			Audio->StopHandle(Pair.Value, 0.0f);
		}
	}
	RelayedLoops.Reset();

	Super::EndPlay(EndPlayReason);
}
