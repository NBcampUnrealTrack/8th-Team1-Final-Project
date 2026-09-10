// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/Equipment/MD_ItemDefinition.h"
#include "GameplayTagContainer.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_ToolDefinition.generated.h"

class UGameplayAbility;
class UStaticMesh;
class UTexture2D;

// 메시와 GA 태그 및 도구별 성능을 제공하는 공통 아이템 파생 정의
UCLASS(BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_ToolDefinition : public UMD_ItemDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context) override;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FText ToolName;

	/** 기본 전체 도구 메시. ScrapeLoad가 0이거나 단계 메시가 비어 있으면 사용한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	TObjectPtr<UStaticMesh> ToolMesh;

	/** ScrapeLoad 1단계에서 ToolMesh를 대체할 오염 포함 전체 도구 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape Load")
	TObjectPtr<UStaticMesh> ScrapeLoadStage1Mesh;

	/** ScrapeLoad 2단계에서 ToolMesh를 대체할 오염 포함 전체 도구 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape Load")
	TObjectPtr<UStaticMesh> ScrapeLoadStage2Mesh;

	/** ScrapeLoad 3단계에서 ToolMesh를 대체할 오염 포함 전체 도구 메시 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape Load")
	TObjectPtr<UStaticMesh> ScrapeLoadStage3Mesh;

	// 이 도구가 부착될 소켓 이름. 비우면 캐릭터 기본 소켓(ToolSocket) 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FName AttachSocketName;

	//소켓 오프셋 미세 조정
	//도구 매쉬 마다 손잡이 위치가 달라 미세 조정 필요.
	//소켓에 스냅한 뒤 이 값을 트랜스폼으로 얹는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	FTransform AttachOffset;

	// 이 도구를 든 상태에서 좌클릭 시 발동될 어빌리티
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool", meta = (Categories = "Ability"))
	FGameplayTag ToolAbilityTag;

	/**
	 * 헤라 도구가 장착될 때 서버가 ScrapeToolComponent에 확정하는 단일 성능 정본.
	 * 비헤라 도구에서는 사용하지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Scrape")
	FMD_ScrapeToolSpec ScrapeProfile;

	// 임시 UI용 아이콘 (없으면 이름만 표시)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool")
	TObjectPtr<UTexture2D> ToolIcon;

	// 절단 진행 속도 배율. 기획 08 §3의 "절단 시간 −35%"(IT-C1)는 속도 배율 약 1.54에 해당한다 (1 / 0.65)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool",
		meta = (ClampMin = "0.1"))
	float CutSpeedMultiplier = 1.0f;

	// 충전 도구의 최대 충전량이며 충전 기능이 없는 도구는 영
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Charge", meta = (ClampMin = "0.0"))
	float MaxCharge = 0.0f;

	// 실제 도구 지급 시 설정할 초기 충전량
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tool|Charge", meta = (ClampMin = "0.0"))
	float InitialCharge = 0.0f;
};
