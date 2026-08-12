// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "MD_GA_WallCut.generated.h"

// 조준한 격벽 절단. 즉발 어빌리티
UCLASS()
class MARINEDEMOLITION_API UMD_GA_WallCut : public UMD_GA_WallBase
{
	GENERATED_BODY()

public:
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

	/** 부상 중 절단 속도 (기획 §3) */
	UPROPERTY(EditDefaultsOnly, Category = "Ability",
		meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float InjuredCutSpeedMultiplier = 0.7f;

private:

	void TickCutProgress();

	/** 장비·부상 배율을 곱한 최종 속도 */
	float GetCutSpeedMultiplier() const;

	FTimerHandle ProgressTimerHandle;
};
