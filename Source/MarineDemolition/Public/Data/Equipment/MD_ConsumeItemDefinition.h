// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "MD_ConsumeItemDefinition.generated.h"

class UGameplayEffect;
class UStaticMesh;

// 지급 시 보관하고 별도의 사용 요청에서 효과를 실행할 소모품 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_ConsumeItemDefinition : public UMD_ItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;
	virtual FORCEINLINE TSubclassOf<UGameplayEffect> GetAbilityGrantEffect() override { return AbilityGrantEffect; }
	virtual FORCEINLINE FGameplayTag GetUseAbilityTag() override { return UseAbilityTag; }

public:
	// 구매 또는 장비 복구 시 소유자의 ASC에 적용하여 GA를 부여할 GE
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Ability")
	TSubclassOf<UGameplayEffect> AbilityGrantEffect;

	// Hotbar에서 선택한 소모품을 캐릭터 손에 표시할 메시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Visual")
	TObjectPtr<UStaticMesh> ConsumeMesh;

	// 선택한 소모품의 사용 입력으로 실행할 구매 GE의 GA 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Ability", meta = (Categories = "Ability"))
	FGameplayTag UseAbilityTag;

};
