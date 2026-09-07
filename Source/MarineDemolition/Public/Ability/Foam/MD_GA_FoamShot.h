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

	static UMD_FoamGunComponent* GetFoamGun(const FGameplayAbilityActorInfo* ActorInfo);
};
