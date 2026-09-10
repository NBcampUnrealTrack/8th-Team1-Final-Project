// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "MD_FoamToolDefinition.generated.h"

// 사격 GA와 같은 폼건 컴포넌트의 충전 상태를 사용하는 도구 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_FoamToolDefinition : public UMD_ToolDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanReceiveCharge_Implementation(AActor* Recipient, float Amount) override;
	virtual bool ReceiveCharge_Implementation(AActor* Recipient, float Amount) override;
};
