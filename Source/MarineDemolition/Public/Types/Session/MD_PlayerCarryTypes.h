// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MD_PlayerCarryTypes.generated.h"

class UMD_ItemDefinition;

// 슬롯 위치별 아이템 소유권과 사용 후 남은 상태의 세션 내 운반 기록
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_HotbarItemData
{
	GENERATED_BODY()

public:
	// 에셋 경로와 파일명 변경에도 유지할 아이템 식별자
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carry Data")
	FPrimaryAssetId ItemId;

	// 빈 슬롯을 포함한 실제 보관 수량
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carry Data")
	int32 Count = 0;

	// 수집 직전 도구의 남은 충전량
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carry Data")
	float Charge = 0.0f;

	// 지급 당시 확정된 도구의 사용 비용
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carry Data")
	float CostPerOnce = 0.0f;

public:
	// 자산을 로드하지 않고 식별자의 현재 로딩 위치 조회
	FSoftObjectPath GetItemAssetPath() const;

	// 식별자를 등록된 아이템 정의로 검증하여 해석
	UMD_ItemDefinition* ResolveItemDefinition() const;
};

/**
 * 맵 이동과 재접속 이후에도 동일한 장비 구성을 적용하기 위한 플레이어 운반 데이터
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_PlayerData
{
	GENERATED_BODY()

public:
	// 빈 칸과 순서를 보존하는 네 슬롯의 상세 기록
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carry Data")
	TArray<FMD_HotbarItemData> HotbarItems;

	// 복원 직후 장착과 입력 대상으로 사용할 슬롯
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Carry Data")
	int32 SelectedHotbarSlot = 0;

	// 구매 이력이 수집되지 않은 기록과 명시적으로 비어 있는 이력 구분
	UPROPERTY()
	bool bHasShopPurchaseState = false;

	// 선행 장비 해금과 무료 재구매에 사용할 플레이어별 구매 이력
	UPROPERTY()
	TArray<FPrimaryAssetId> PurchasedShopItemIds;

	// 업그레이드로 대체된 기본 및 중간 도구의 구매 금지 기록
	UPROPERTY()
	TArray<FPrimaryAssetId> BlockedShopItemIds;

public:
	// 슬롯 범위와 수량 및 충전 값 검사
	bool IsStructurallyValid() const;
};
