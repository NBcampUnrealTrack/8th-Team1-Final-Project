#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Audio/MD_AudioSubsystem.h"
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

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAttached(const FGameplayTag EventTag);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_StopAttached(const FGameplayTag EventTag, const float FadeOutSeconds);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAtLocation(const FGameplayTag EventTag, const FVector_NetQuantize Location);

	/**
	 * 이 머신이 소리를 이미 직접 냈는지.
	 *
	 * 조작 중인 본인은 어빌리티가 먼저 재생했으므로 멀티캐스트를 무시해야
	 * 같은 소리가 두 번 겹치지 않는다.
	 */
	bool ShouldIgnoreMulticast() const;

	UMD_AudioSubsystem* GetAudio() const;

	/** 중계로 시작한 루프. 태그 하나당 하나만 유지한다 */
	TMap<FGameplayTag, FMD_AudioHandle> RelayedLoops;
};
