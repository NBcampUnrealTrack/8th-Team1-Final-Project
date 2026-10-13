#include "Audio/MD_AudioRelayComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

// 평상시엔 조용하고, 문제가 생기면 `Log LogMDAudioRelay Verbose` 로 켠다
DEFINE_LOG_CATEGORY_STATIC(LogMDAudioRelay, Log, All);

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

bool UMD_AudioRelayComponent::ShouldIgnoreMulticast(const FGameplayTag& EventTag) const
{
	// 이 머신에서 이미 직접 재생했다. 서버가 어빌리티를 함께 돌린 경우가 여기 걸린다
	if (LocalLoops.Contains(EventTag))
	{
		return true;
	}

	// 조작 중인 본인은 어빌리티가 이미 직접 재생했다
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn && OwnerPawn->IsLocallyControlled();
}

void UMD_AudioRelayComponent::PlayAttachedForDuration(
	const FGameplayTag& EventTag, const float DurationSeconds, const float FadeOutSeconds)
{
	UWorld* const World = GetWorld();
	AActor* const Owner = GetOwner();
	UMD_AudioSubsystem* Audio = GetAudio();
	if (!World || !IsValid(Owner) || !Audio || !EventTag.IsValid() || DurationSeconds <= 0.0f)
	{
		return;
	}

	// 같은 태그를 다시 쏘면 앞의 것을 먼저 정리한다. 겹쳐 울리면 판정이 흐려진다
	StopEverywhere(EventTag, FadeOutSeconds);

	const FMD_AudioHandle Handle =
		Audio->PlayEventAttached(EventTag, Owner->GetRootComponent());
	if (Handle.IsValid())
	{
		LocalLoops.Add(EventTag, Handle);
	}

	// 권한이 있으면 다른 머신에도 알린다. 이 머신은 위에서 이미 재생했으므로
	// 멀티캐스트가 돌아와도 LocalLoops 때문에 스스로 빠진다
	if (Owner->HasAuthority())
	{
		Multicast_PlayAttached(EventTag);
	}

	// 타이머는 컴포넌트가 들고 있는다. 어빌리티는 3초를 못 버틸 수 있다
	FTimerHandle& Timer = AutoStopTimers.FindOrAdd(EventTag);
	World->GetTimerManager().ClearTimer(Timer);

	const FGameplayTag CapturedTag = EventTag;
	const float CapturedFade = FadeOutSeconds;
	World->GetTimerManager().SetTimer(
		Timer,
		FTimerDelegate::CreateWeakLambda(this, [this, CapturedTag, CapturedFade]()
		{
			StopEverywhere(CapturedTag, CapturedFade);
		}),
		DurationSeconds,
		/*bLoop*/ false);

	UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] %s %.2f초 재생 예약 | 로컬핸들=%d | 권한=%s"),
		*EventTag.ToString(), DurationSeconds, Handle.Id,
		Owner->HasAuthority() ? TEXT("O") : TEXT("X"));
}

void UMD_AudioRelayComponent::StopEverywhere(
	const FGameplayTag& EventTag, const float FadeOutSeconds)
{
	if (UWorld* const World = GetWorld())
	{
		if (FTimerHandle* Timer = AutoStopTimers.Find(EventTag))
		{
			World->GetTimerManager().ClearTimer(*Timer);
		}
	}

	FMD_AudioHandle Local;
	if (LocalLoops.RemoveAndCopyValue(EventTag, Local))
	{
		if (UMD_AudioSubsystem* Audio = GetAudio())
		{
			Audio->StopHandle(Local, FadeOutSeconds);
		}
		UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] 로컬 정지: %s | 핸들=%d"),
			*EventTag.ToString(), Local.Id);
	}

	// 중계 정지는 로컬 재생 성공 여부와 무관하게 나가야 한다.
	// 여기서 빠지면 남의 화면에서 루프가 영원히 남는다
	StopAttachedForAll(EventTag, FadeOutSeconds);
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

void UMD_AudioRelayComponent::PlayConfirmedAtOwnerLocationForAll(const FGameplayTag& EventTag)
{
	if (EventTag.IsValid() && GetOwner() && GetOwner()->HasAuthority())
		Multicast_PlayConfirmedAtLocation(EventTag, GetOwner()->GetActorLocation());
}
void UMD_AudioRelayComponent::Multicast_PlayConfirmedAtLocation_Implementation(FGameplayTag EventTag, FVector_NetQuantize Location)
{
	if (UMD_AudioSubsystem* Audio = GetAudio()) Audio->PlayEventAtLocation(EventTag, Location);
}

void UMD_AudioRelayComponent::Multicast_PlayAttached_Implementation(const FGameplayTag EventTag)
{
	if (ShouldIgnoreMulticast(EventTag))
	{
		UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] 재생 무시(로컬조작): %s"), *EventTag.ToString());
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

	UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] 재생: %s | 핸들=%d"), *EventTag.ToString(), Handle.Id);
}

void UMD_AudioRelayComponent::Multicast_StopAttached_Implementation(
	const FGameplayTag EventTag, const float FadeOutSeconds)
{
	if (ShouldIgnoreMulticast(EventTag))
	{
		UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] 정지 무시(로컬조작): %s"), *EventTag.ToString());
		return;
	}

	FMD_AudioHandle Handle;
	if (!RelayedLoops.RemoveAndCopyValue(EventTag, Handle))
	{
		UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] 정지했으나 보관된 루프 없음: %s"), *EventTag.ToString());
		return;
	}

	UE_LOG(LogMDAudioRelay, Verbose, TEXT("[중계오디오] 정지: %s | 핸들=%d"), *EventTag.ToString(), Handle.Id);

	if (UMD_AudioSubsystem* Audio = GetAudio())
	{
		Audio->StopHandle(Handle, FadeOutSeconds);
	}
}

void UMD_AudioRelayComponent::Multicast_PlayAtLocation_Implementation(
	const FGameplayTag EventTag, const FVector_NetQuantize Location)
{
	if (ShouldIgnoreMulticast(EventTag))
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

	// 직접 시작한 루프와 타이머도 같이 접는다
	if (UMD_AudioSubsystem* Audio = GetAudio())
	{
		for (const TPair<FGameplayTag, FMD_AudioHandle>& Pair : LocalLoops)
		{
			Audio->StopHandle(Pair.Value, 0.0f);
		}
	}
	LocalLoops.Reset();

	if (UWorld* const World = GetWorld())
	{
		for (TPair<FGameplayTag, FTimerHandle>& Pair : AutoStopTimers)
		{
			World->GetTimerManager().ClearTimer(Pair.Value);
		}
	}
	AutoStopTimers.Reset();

	Super::EndPlay(EndPlayReason);
}
