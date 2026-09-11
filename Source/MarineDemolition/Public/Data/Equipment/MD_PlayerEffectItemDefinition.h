// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_InstantItemDefinition.h"
#include "MD_PlayerEffectItemDefinition.generated.h"

class UGameplayEffect;

// 산소 탱크 등 플레이어에게 구매 즉시 GameplayEffect를 적용할 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_PlayerEffectItemDefinition : public UMD_InstantItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;

public:
	// 구매자의 속성과 태그를 변경할 GameplayEffect 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Effect")
	TSubclassOf<UGameplayEffect> EffectClass;

	// 지급 문맥의 구매 이력에 같은 정의가 있으면 중복 적용을 제한할 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Effect")
	bool bUniqueUpgrade = false;
};
