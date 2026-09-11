// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "ActiveGameplayEffectHandle.h"
#include "MD_ConsumeItemDefinition.generated.h"

// 지급 시 보관하고 별도의 사용 요청에서 효과를 실행할 소모품 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_ConsumeItemDefinition : public UMD_ItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;
	virtual FORCEINLINE FGameplayTag GetUseAbilityTag() override { return UseAbilityTag; }

public:
	// 한 품목 구매 시 보관할 사용 가능 수량이며 완충 패드 묶음은 세 개
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Grant", meta = (ClampMin = "1"))
	int32 QuantityPerPurchase = 1;

	// 선택한 소모품의 사용 입력으로 실행할 구매 GE의 GA 태그
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Ability", meta = (Categories = "Ability"))
	FGameplayTag UseAbilityTag;

public:
	// 즉시 효과를 포함한 실제 적용 성공 여부를 사용 GA에 전달
	UFUNCTION(BlueprintPure, Category = "Item|Ability")
	static bool IsUseEffectApplied(FActiveGameplayEffectHandle EffectHandle) { return EffectHandle.WasSuccessfullyApplied(); }

protected:
	// 구매 묶음 수를 실제 사용 수량으로 변환하고 범위 오류 시 영 반환
	int32 GetGrantedQuantity(int32 PurchaseQuantity);
};
