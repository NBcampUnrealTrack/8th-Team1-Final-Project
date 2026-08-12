// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_ShopCatalog.generated.h"

class UTexture2D;

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

	/** `07` §4의 ID를 그대로 쓴다 (IT-H1, IT-S1, IT-FG0 ...) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	EMD_ShopCategory Category = EMD_ShopCategory::Tool;

	/** ₡. 0은 "지급" 품목 (기본 헤라·절단기·스캐너) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 Price = 0;

	/** 소모품처럼 개당 가격이면 true → `35₡/개` */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	bool bPricePerUnit = false;

	/** `04` §3: 아이템 카드에 "한 줄 기능 설명"이 필수다. 상세는 `08`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (MultiLine = "false"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	TSoftObjectPtr<UTexture2D> Icon;

	// ───── 해금 조건 (`07` §4). 아무것도 설정하지 않으면 "상점 상시". ─────

	/** 예: 열선 헤라는 호감 ≥0, 고속 절단기는 호감 ≥+1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock")
	bool bRequiresAffinity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock", meta = (EditCondition = "bRequiresAffinity", ClampMin = "-4.0", ClampMax = "5.0"))
	float AffinityRequired = 0.f;

	/** 예: 열선 헤라는 퀘스트 1회 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock", meta = (ClampMin = "0"))
	int32 QuestCompletionsRequired = 0;

	/** 예: 열선 배터리는 IT-H2 보유, 폼 카트리지는 IT-FG0 보유 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Unlock")
	FName PrerequisiteItemId;

	/** 해금 조건을 실제로 판정한다. 런타임 시스템 없이도 호출 가능한 순수 함수다. */
	bool IsUnlocked(float Affinity, int32 QuestCompletions, const TArray<FName>& OwnedItemIds) const;

	/** `호감 ≥0 · 퀘스트 1회` / `IT-H2 보유` / `상점 상시` 형태로 문구를 만든다. */
	FText GetUnlockConditionText() const;

	/** `450₡` 또는 `35₡/개`, 0이면 `지급` */
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

	UFUNCTION(BlueprintCallable, Category = "Shop")
	bool FindItem(FName ItemId, FMD_ShopItem& OutItem) const;
};
