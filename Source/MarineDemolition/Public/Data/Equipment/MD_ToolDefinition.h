// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "GameplayTagContainer.h"
#include "MD_ToolDefinition.generated.h"

class UStaticMesh;
class UMD_EquipHotbarComponent;

// 메시와 GA 태그 및 도구별 성능을 제공하는 공통 아이템 파생 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_ToolDefinition : public UMD_ItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context) override;
	virtual FORCEINLINE FGameplayTag GetUseAbilityTag() override { return ToolAbilityTag; }

public:
	// 현재 Hotbar에 보유해야 할 업그레이드 선행 도구이며 미지정 시 종류 제한 없음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Upgrade", meta = (AllowedTypes = "Item"))
	FPrimaryAssetId UpgradeSourceAssetId;

	// 이 도구를 든 상태에서 좌클릭 시 발동될 어빌리티
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool", meta = (Categories = "Ability"))
	FGameplayTag ToolAbilityTag;

	/** Durability removed by one completed hit on a placed barnacle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Contamination", meta = (ClampMin = "0.01"))
	float BarnacleDamagePerClick = 1.0f;

	/** Cleaning strength per completed swing: 100 removes 1000 stain-size units (one basic step); 200/300 remove two/three steps. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Contamination", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 StainRemovalAmount = 100;

	/** Charged scrapers keep working at basic strength after their heat runs out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Contamination", meta = (ClampMin = "0.01"))
	float UnchargedBarnacleDamagePerClick = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Contamination", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 UnchargedStainRemovalAmount = 100;

	UFUNCTION(BlueprintPure, Category = "Tool|Contamination")
	float GetScrapeBarnacleDamage(float RemainingCharge) const;

	UFUNCTION(BlueprintPure, Category = "Tool|Contamination")
	int32 GetScrapeStainRemovalAmount(float RemainingCharge) const;

	// 절단 진행 속도 배율. 기획 08 §3의 "절단 시간 −35%"(IT-C1)는 속도 배율 약 1.54에 해당한다 (1 / 0.65)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool",
		meta = (ClampMin = "0.1"))
	float CutSpeedMultiplier = 1.0f;

	// 구매 시 채울 최대 충전량이며 영이면 충전 조건 없이 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Charge", meta = (ClampMin = "0.0"))
	float MaxCharge = 0.0f;

	// 한 번 사용에 차감할 충전량이며 구매 시 보관 슬롯에 적용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Charge", meta = (ClampMin = "0.0"))
	float CostPerOnce = 1.0f;

public:
	// 구매와 기본 지급에서 공통으로 사용할 최대 충전량 및 사용 비용 검사
	UFUNCTION(BlueprintPure, Category = "Tool|Charge")
	bool IsChargeSettingsValid();

	// 이 도구의 실제 잔량 저장소에 대한 충전 가능 검사
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tool|Charge")
	bool CanReceiveCharge(AActor* Recipient, float Amount);
	// 공통 Hotbar 충전 상태를 사용하는 기본 도구 검사
	virtual bool CanReceiveCharge_Implementation(AActor* Recipient, float Amount);

	// 이 도구의 실제 잔량 저장소에 대한 서버 충전 실행
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly, Category = "Tool|Charge")
	bool ReceiveCharge(AActor* Recipient, float Amount);
	// 공통 Hotbar 충전 상태를 사용하는 기본 도구 실행
	virtual bool ReceiveCharge_Implementation(AActor* Recipient, float Amount);

protected:
	// 업그레이드 검사와 지급이 공통으로 사용할 현재 선행 도구 위치 조회
	int32 FindUpgradeSourceSlot(UMD_EquipHotbarComponent* Hotbar);
};
