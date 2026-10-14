#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Audio/MD_AudioSubsystem.h"
#include "Engine/EngineTypes.h"
#include "MD_AudioRelayComponent.generated.h"

/**
 * 도구 작업음을 다른 클라이언트에게 중계한다.
 *
 * 어빌리티(UGameplayAbility)는 액터가 아니라 RPC 를 가질 수 없고, 실행 정책에 따라
 * 애초에 남의 머신에서는 돌지 않는다. 그래서 절단기·스캐너 소리가 조작한 본인과
 * 호스트에서만 났다. 이 컴포넌트가 아바타에 붙어 태그를 멀티캐스트로 넘긴다.
 *
 * 조작 중인 본인은 어빌리티가 **직접** 재생한다. 왕복 지연을 기다리면 도구가
 * 굼뜨게 느껴지기 때문이다. 멀티캐스트는 그 머신에서 스스로 빠진다.
 *
 * 사운드 에셋은 여기서도 알지 못한다. 넘기는 것은 태그뿐이고 소재는
 * 오디오 카탈로그가 정한다.
 */
UCLASS(ClassGroup = (MD), meta = (BlueprintSpawnableComponent))
class MARINEDEMOLITION_API UMD_AudioRelayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMD_AudioRelayComponent();

	/**
	 * 아바타에 붙는 소리를 모두에게 알린다. 서버에서만 호출한다.
	 * 루프면 태그별로 핸들을 보관해 두었다가 StopAttachedForAll 로 끊는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	void PlayAttachedForAll(const FGameplayTag& EventTag);

	/** 위 호출로 시작한 루프를 모두에게서 끊는다. 서버에서만 호출한다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	void StopAttachedForAll(const FGameplayTag& EventTag, float FadeOutSeconds = -1.0f);

	/** 아바타 위치에서 1회 재생. 아바타가 사라진 뒤에도 남아야 하는 소리 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	void PlayAtOwnerLocationForAll(const FGameplayTag& EventTag);
	/** Server-confirmed action: also play on the owner, who has not predicted this sound. */
	void PlayConfirmedAtOwnerLocationForAll(const FGameplayTag& EventTag);

	/**
	 * 태그를 지정한 시간 동안 재생하고 스스로 끊는다.
	 *
	 * 정해진 시간 뒤에 멎어야 하는 루프는 **타이머를 어빌리티에 걸면 안 된다.**
	 * 어빌리티 인스턴스는 발동이 끝나거나 도구가 회수될 때 사라질 수 있고,
	 * 그러면 타이머만 조용히 죽어 소리가 영원히 남는다.
	 * 이 컴포넌트는 아바타와 수명을 같이하므로 그런 일이 없다.
	 *
	 * 호출한 머신에서 바로 재생하고, 권한이 있으면 다른 머신에도 중계한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	void PlayAttachedForDuration(const FGameplayTag& EventTag, float DurationSeconds, float FadeOutSeconds = -1.0f);

	/** 이 태그의 루프를 이 머신과 (권한이 있으면) 다른 머신에서 함께 끊는다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Audio", meta = (AutoCreateRefTerm = "EventTag"))
	void StopEverywhere(const FGameplayTag& EventTag, float FadeOutSeconds = -1.0f);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 도구 조작은 초당 수십 번 오는 이벤트가 아니다. 대신 정지가 한 번 유실되면
	// 루프가 영원히 남아 복구할 방법이 없으므로 셋 다 Reliable 로 둔다.
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAttached(const FGameplayTag EventTag);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopAttached(const FGameplayTag EventTag, const float FadeOutSeconds);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAtLocation(const FGameplayTag EventTag, const FVector_NetQuantize Location);
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayConfirmedAtLocation(FGameplayTag EventTag, FVector_NetQuantize Location);

	/**
	 * 이 머신이 이 소리를 이미 직접 냈는지.
	 *
	 * 조작 중인 본인은 어빌리티가 먼저 재생했으므로 멀티캐스트를 무시해야
	 * 같은 소리가 두 번 겹치지 않는다. 서버가 어빌리티를 함께 돌린 경우도
	 * 마찬가지라, 로컬 재생 목록을 같이 본다.
	 */
	bool ShouldIgnoreMulticast(const FGameplayTag& EventTag) const;

	UMD_AudioSubsystem* GetAudio() const;

	/** 이 머신이 직접 시작한 루프. 중계로 받은 것과 구분한다 */
	TMap<FGameplayTag, FMD_AudioHandle> LocalLoops;

	/** PlayAttachedForDuration 이 건 자동 정지 타이머 */
	TMap<FGameplayTag, FTimerHandle> AutoStopTimers;

	/** 중계로 시작한 루프. 태그 하나당 하나만 유지한다 */
	TMap<FGameplayTag, FMD_AudioHandle> RelayedLoops;
};
