// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_InstantItemDefinition.h"
#include "MD_ToolChargeItemDefinition.generated.h"

class UMD_ToolDefinition;

// 특정 정의의 도구에 구매 즉시 충전을 제공할 아이템
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_ToolChargeItemDefinition : public UMD_InstantItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;

public:
	// 충전 가능한 도구를 정확히 식별할 Primary Asset ID
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Charge")
	FPrimaryAssetId TargetItemAssetId;

	// 한 번의 구매에서 대상 도구에 추가할 충전량
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Charge", meta = (ClampMin = "0.0"))
	float ChargeAmount = 0.0f;

protected:
	// 전체 AssetId가 일치하는 실제 보유 도구 조회
	UMD_ToolDefinition* FindTargetTool(AActor* Recipient);
};
