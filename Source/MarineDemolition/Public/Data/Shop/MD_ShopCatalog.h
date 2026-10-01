// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StreamableManager.h"
#include "MD_ShopCatalog.generated.h"

class UMD_ItemDefinition;

/** 서버 구매 검증 결과를 클라이언트 UI에 전달하기 위한 상태 */
UENUM(BlueprintType)
enum class EMD_ShopPurchaseResult : uint8
{
	Success             UMETA(DisplayName = "구매 성공"),
	InvalidRequest      UMETA(DisplayName = "잘못된 요청"),
	InvalidItem         UMETA(DisplayName = "유효하지 않은 품목"),
	Locked              UMETA(DisplayName = "잠긴 품목"),
	InsufficientCredits UMETA(DisplayName = "크레딧 부족"),
	FullyStocked        UMETA(DisplayName = "이미 보유한 품목")
};

// 서버가 확정한 판매 결과와 거부 원인
UENUM(BlueprintType)
enum class EMD_ShopSaleResult : uint8
{
	// 아이템 회수와 대금 적립 완료
	Success UMETA(DisplayName = "판매 성공"),
	// 권한 또는 거리 및 수량이 잘못된 요청
	InvalidRequest UMETA(DisplayName = "잘못된 요청"),
	// 서버 카탈로그에 없거나 로드되지 않은 품목
	InvalidItem UMETA(DisplayName = "유효하지 않은 품목"),
	// 판매 불가 유형 또는 잘못된 가격 설정
	NotSellable UMETA(DisplayName = "판매 불가 품목"),
	// 슬롯 변경 또는 판매 수량 부족
	NotOwned UMETA(DisplayName = "보유 수량 부족"),
	// 대금 적립으로 크레딧 정수 범위 초과
	CreditLimit UMETA(DisplayName = "크레딧 한도 초과")
};

/**
 * 상점 카테고리 (`04` §3 = `08` §1, 5탭)
 *
 * 비매품(쓰레기통 IT-B0)은 카테고리에 없다 — 상점에 노출하지 않고
 * 기지 폐기물 대기장에서만 무한 보급한다 (`07` §4, `08` §5.3).
 */
UENUM(BlueprintType)
enum class EMD_ShopCategory : uint8
{
	Tool			UMETA(DisplayName = "도구"),
	Safety			UMETA(DisplayName = "안전"),
	UtilityRepair	UMETA(DisplayName = "유틸 수리"),
	Survival		UMETA(DisplayName = "생존"),
	Recovery		UMETA(DisplayName = "회수")
};

/**
 * 상점 품목 1건.
 *
 * 가격·해금은 `07` §4 표가 정본이고, 품목 설명·효과는 `08`이 정본이다.
 * 해금 조건을 문자열로 적어두지 않고 필드로 분해한 이유: 표시 문구와 실제 판정이
 * 따로 관리되면 반드시 어긋난다. GetUnlockConditionText()가 필드에서 문구를 생성한다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ShopItem
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	EMD_ShopCategory Category = EMD_ShopCategory::Tool;

	/** ₡. 0은 "지급" 품목 (기본 헤라·절단기·스캐너) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 Price = 0;

	/** 소모품처럼 개당 가격이면 true → `35₡/개` */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	bool bPricePerUnit = false;

	// 판매할 공통 정의의 PrimaryAssetId이며 별도 판매 식별자 없이 사용
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Item", meta = (AllowedTypes = "Item"))
	FPrimaryAssetId ItemAssetId;

	// ───── 해금 조건 (`07` §4). 아무것도 설정하지 않으면 "상점 상시". ─────

	/** 예: 열선 헤라는 호감 ≥0, 고속 절단기는 호감 ≥+1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock")
	bool bRequiresAffinity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock", meta = (EditCondition = "bRequiresAffinity", ClampMin = "-4.0", ClampMax = "5.0"))
	float AffinityRequired = 0.f;

	/** 예: 열선 헤라는 퀘스트 1회 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock", meta = (ClampMin = "0"))
	int32 QuestCompletionsRequired = 0;

	/** 예: 열선 배터리는 IT-H2(열선 헤라) 보유, 폼 카트리지는 IT-FG0(폼건) 보유 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock", meta = (AllowedTypes = "Item"))
	FPrimaryAssetId PrerequisiteItemAssetId;

	// 카탈로그에 지정한 정의의 전체 식별자 제공
	FORCEINLINE FPrimaryAssetId GetItemAssetId() const { return ItemAssetId; }

	// 에셋 로드 없이 이미 준비된 공통 정의만 조회
	UMD_ItemDefinition* GetDefinition() const;
	// 카탈로그 재검색 없이 판매 및 선행 정의의 로드 상태 확인
	bool IsDefinitionLoaded();

	// 호감 및 퀘스트 조건과 전체 AssetId가 일치하는 보유 이력으로 해금 판정
	bool IsUnlocked(float Affinity, int32 QuestCompletions, const TArray<FPrimaryAssetId>& OwnedItemAssetIds) const;

	// 공통 정의의 표시 이름을 사용한 해금 문구 생성 (예: 열선 헤라 보유)
	FText GetUnlockConditionText() const;

	// 구매 이력 복사 없이 도구의 무료 재구매 가격 계산
	int32 GetPurchasePrice(TArray<FPrimaryAssetId>& PurchasedItemIds) const;

	// 도구와 개당 소모품 구매가의 절반 계산이며 판매 불가는 음수
	int32 GetSalePrice();

	// 일반 가격 450₡ 또는 개당 가격 35₡/개 표기이며 무료 가격은 0
	FText GetPriceText() const;
};

/**
 * 상점 전체 카탈로그.
 *
 * 가격을 C++에 박지 않고 에셋으로 빼둔 이유: `07` §4는 밸런스 문서라 자주 바뀐다.
 * 프로젝트 기존 관례(UMD_ToolDefinition)도 DataAsset이다.
 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_ShopCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 표시 순서는 배열 순서를 따른다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	TArray<FMD_ShopItem> Items;

	UFUNCTION(BlueprintCallable, Category = "Shop")
	TArray<FMD_ShopItem> GetItemsInCategory(EMD_ShopCategory InCategory) const;

	// BP 구조체 사용처에 판매 정의의 전체 식별자 제공
	UFUNCTION(BlueprintPure, Category = "Shop")
	static FPrimaryAssetId GetItemAssetId(FMD_ShopItem Item);

	// BP UI가 공통 이름과 설명 및 아이콘을 조회할 로드된 정의 제공
	UFUNCTION(BlueprintPure, Category = "Shop")
	static UMD_ItemDefinition* GetItemDefinition(FMD_ShopItem Item);

	// 지정 품목이 유일하게 등록되고 판매 및 선행 정의가 준비되었는지 확인
	UFUNCTION(BlueprintPure, Category = "Shop")
	bool IsItemDefinitionLoaded(FPrimaryAssetId ItemAssetId);

	// 중복 집계 후 준비된 품목만 카탈로그 순서대로 수집
	TArray<FMD_ShopItem> GetAvailableItems();

	// 호출자가 반환 핸들을 유지하는 동안 판매 정의를 비동기로 로드 및 보관
	TSharedPtr<FStreamableHandle> LoadItemDefinitions(FStreamableDelegate OnLoaded = FStreamableDelegate());

	// AssetType과 품목명 전체가 일치하는 판매 항목 조회
	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool FindItemByAssetId(FPrimaryAssetId ItemAssetId, FMD_ShopItem& OutItem);

};
