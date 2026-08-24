// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "Audio/MD_AudioSubsystem.h"
#include "MD_GA_WallCut.generated.h"

// 조준한 격벽 절단. 즉발 어빌리티
UCLASS()
class MARINEDEMOLITION_API UMD_GA_WallCut : public UMD_GA_WallBase
{
	GENERATED_BODY()

public:
	UMD_GA_WallCut();

	/** 태그 대상을 조준하고 있지 않으면 발동을 막는다. 몽타주도 이때 걸러진다 */
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                                const FGameplayAbilityActorInfo* ActorInfo,
	                                const FGameplayTagContainer* SourceTags = nullptr,
	                                const FGameplayTagContainer* TargetTags = nullptr,
	                                OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
							const FGameplayAbilityActivationInfo ActivationInfo,
							bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/** 절단 몽타주 재생·정지는 BP에서 처리한다 (기획 §5) */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Wall|Cut")
	void OnCutStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Wall|Cut")
	void OnCutStopped();

	/** 진행도 누적 주기. 매 프레임 돌 필요가 없다 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability",
		meta = (ClampMin = "0.02", Units = "s"))
	float ProgressInterval = 0.05f;

	/**
	 * 절단 시작음. 모터가 물리는 순간 한 번.
	 * 사운드 에셋이 아니라 태그다 — 어떤 소재가 나갈지는 DA_MD_Audio_Cut 이 정한다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|사운드")
	FGameplayTag CutStartSoundTag;

	/** 절단 중 지속음. 어빌리티가 끝날 때까지 캐릭터를 따라다닌다 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|사운드")
	FGameplayTag CutLoopSoundTag;

	/** 절단 정지음. 모터가 멎는 소리 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|사운드")
	FGameplayTag CutStopSoundTag;

	/** 루프 정지 페이드(초). 0 이면 즉시 끊긴다 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|사운드", meta = (ClampMin = "0.0"))
	float CutLoopFadeOutSeconds = 0.12f;

	/** 부상 중 절단 속도 (기획 §3) */
	UPROPERTY(EditDefaultsOnly, Category = "Ability",
		meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float InjuredCutSpeedMultiplier = 0.7f;

private:

	void TickCutProgress();

	/** 장비·부상 배율을 곱한 최종 속도 */
	float GetCutSpeedMultiplier() const;

	void StartCutAudio();
	void StopCutAudio();

	/** 아바타에 붙여 재생 중인 절단 루프 */
	FMD_AudioHandle CutLoopHandle;

	FTimerHandle ProgressTimerHandle;
};
