// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/Wall/MD_GA_WallBase.h"
#include "MD_GA_WallScan.generated.h"

class UMaterialParameterCollection;

// 조준한 격벽 스캔. 타입별 하이라이트 표시. 즉발 어빌리티
UCLASS()
class MARINEDEMOLITION_API UMD_GA_WallScan : public UMD_GA_WallBase
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;
	
protected:
	// 하이라이트 지속시간
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	float ScanDuration = 3.0f;

	// 스캔 스윕 연출 구동용 컬렉션 (MPC_Scan 에디터 할당)
	UPROPERTY(EditDefaultsOnly, Category="Ability")
	TObjectPtr<UMaterialParameterCollection> ScanMPC;
};
