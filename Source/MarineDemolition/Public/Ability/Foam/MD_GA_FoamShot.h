#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "MD_GA_FoamShot.generated.h"

class UMD_FoamGunComponent;

/**
 * 폼건 발사 (기획 08 §4.2). Ability.UtilityRepair.Foam 태그로 핫바 LMB에 연결된다.
 *
 * 즉발형: 발동 즉시 충전 −25를 소모하고 발사체를 스폰한 뒤 끝난다.
 * 탄도·충전은 UMD_FoamGunComponent가 정본이고, 이 어빌리티는 절차만 담당한다.
 * 조준 궤적 프리뷰는 어빌리티와 무관하게 폼건을 든 동안 컴포넌트가 그린다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_GA_FoamShot : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMD_GA_FoamShot();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                                const FGameplayAbilityActorInfo* ActorInfo,
	                                const FGameplayTagContainer* SourceTags = nullptr,
	                                const FGameplayTagContainer* TargetTags = nullptr,
	                                FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

protected:
	// 발사음 태그 (SFX.Hazard.FoamGun.Fire). 비워 두면 무음 — 소재는 오디오 카탈로그가 정한다
	UPROPERTY(EditDefaultsOnly, Category = "Foam|Audio", meta = (Categories = "SFX"))
	FGameplayTag FireSoundTag;

	// 충전 부족 사격의 헛방 소리 (SFX.Hazard.FoamGun.Empty). 대응 정의는 DA_MD_Audio_Hazard
	UPROPERTY(EditDefaultsOnly, Category = "Foam|Audio", meta = (Categories = "SFX"))
	FGameplayTag EmptySoundTag;

	static UMD_FoamGunComponent* GetFoamGun(const FGameplayAbilityActorInfo* ActorInfo);

private:
	/**
	 * 조작 중인 본인 머신에서 바로 재생한다.
	 *
	 * 중계(UMD_AudioRelayComponent)는 로컬 조작 폰의 멀티캐스트를 스스로 걸러내므로,
	 * 여기서 직접 내주지 않으면 정작 쏜 사람만 소리를 못 듣는다.
	 */
	void PlayLocalSound(const FGameplayTag& EventTag, const AActor* Avatar) const;
};
