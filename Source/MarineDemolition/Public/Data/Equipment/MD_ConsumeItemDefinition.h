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

public:
	// Hotbar에서 선택한 소모품을 캐릭터 손에 표시할 메시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Visual")
	TObjectPtr<UStaticMesh> ConsumeMesh;

	// GAS 기반 소모 효과에 사용할 선택적 효과 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Use")
	TSubclassOf<UGameplayEffect> EffectClass;

public:
	// 대상과 상태를 변경하지 않고 사용 가능 여부를 확인할 확장 지점
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item|Use")
	bool CanUse(FMD_ItemUseContext Context);
	// 사용 시스템 연결 전 승인하지 않는 기본 검사
	virtual bool CanUse_Implementation(FMD_ItemUseContext Context);

	// 수량 차감과 분리하여 실제 사용 효과만 실행할 확장 지점
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly, Category = "Item|Use")
	bool ApplyItemEffect(FMD_ItemUseContext Context);
	// 사용 효과 구현 전 성공을 반환하지 않는 기본 처리
	virtual bool ApplyItemEffect_Implementation(FMD_ItemUseContext Context);
};
