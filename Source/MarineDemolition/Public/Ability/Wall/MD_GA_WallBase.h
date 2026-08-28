// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "GameplayTagContainer.h"
#include "MD_GA_WallBase.generated.h"

class AMD_CuttableWall;
struct FHitResult;

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

	/**
	 * 조준 대상이 가져야 하는 태그. 하나라도 있으면 유효한 대상으로 본다.
	 * 비워 두면 태그 검사를 하지 않는다 (기존 동작).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|대상")
	FGameplayTagContainer RequiredTargetTags;

	/** 시선 트레이스로 맞은 액터를 그대로 돌려준다. 태그 검사는 하지 않는다 */
	bool TryGetAimedHit(const FGameplayAbilityActorInfo* ActorInfo, FHitResult& OutHit) const;

	/**
	 * RequiredTargetTags 를 만족하는 대상을 조준 중인지.
	 * 격벽·퀘스트 오브젝트를 가리지 않고 태그만으로 판정한다.
	 */
	bool TryGetAimedTargetHit(
		const FGameplayAbilityActorInfo* ActorInfo,
		AActor*& OutActor,
		FHitResult& OutHit) const;

	/**
	 * 액터가 대상 태그를 가지고 있는지.
	 * GameplayTagAssetInterface(ASC 포함) → AActor::Tags 순으로 확인하므로,
	 * C++ 액터와 블루프린트 액터 어느 쪽이든 태그를 붙일 수 있다.
	 */
	static bool ActorHasAnyTargetTag(const AActor* Actor, const FGameplayTagContainer& Tags);

	AMD_CuttableWall* GetAimedWall(const FGameplayAbilityActorInfo* ActorInfo) const;
	bool TryGetAimedWallHit(
		const FGameplayAbilityActorInfo* ActorInfo,
		AMD_CuttableWall*& OutWall,
		FHitResult& OutHit) const;
};
