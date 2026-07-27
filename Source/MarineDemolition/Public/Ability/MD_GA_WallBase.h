// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MD_GA_WallBase.generated.h"

class AMD_CuttableWall;

// 격벽 대상 어빌리티 공통 베이스. 아바타 시점 트레이스로 조준한 격벽을 찾는다
UCLASS()
class MARINEDEMOLITION_API UMD_GA_WallBase : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	UMD_GA_WallBase();
	
protected:
	UPROPERTY(EditDefaultsOnly, Category= "Ability")
	float TraceDistance = 200.0f;
	
	AMD_CuttableWall* GetAimedWall(const FGameplayAbilityActorInfo* ActorInfo) const;
};
