// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "GameplayTagContainer.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
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

	/** ScrapeLoad 1단계에서 ToolMesh를 대체할 오염 포함 전체 도구 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape Load")
	TObjectPtr<UStaticMesh> ScrapeLoadStage1Mesh;

	/** ScrapeLoad 2단계에서 ToolMesh를 대체할 오염 포함 전체 도구 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape Load")
	TObjectPtr<UStaticMesh> ScrapeLoadStage2Mesh;

	/** ScrapeLoad 3단계에서 ToolMesh를 대체할 오염 포함 전체 도구 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape Load")
	TObjectPtr<UStaticMesh> ScrapeLoadStage3Mesh;

	// 이 도구를 든 상태에서 좌클릭 시 발동될 어빌리티
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool", meta = (Categories = "Ability"))
	FGameplayTag ToolAbilityTag;

	/**
	 * 헤라 도구가 장착될 때 서버가 ScrapeToolComponent에 확정하는 단일 성능 정본.
	 * 비헤라 도구에서는 사용하지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape")
	FMD_ScrapeToolSpec ScrapeProfile;

	/** Authoritative amount removed from a surface stain by one completed swing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 StainRemovalAmount = 100;

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
