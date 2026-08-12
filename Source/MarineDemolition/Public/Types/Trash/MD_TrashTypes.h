// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "MD_TrashTypes.generated.h"

class UStaticMesh;

/**
 * Sorter가 구체 액터 타입을 몰라도 한 품목의 판정·점수·환급 정책을 실행할 수 있는 값 객체.
 * RefundCredits가 INDEX_NONE이면 카테고리 행의 기본 환급을 사용한다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashSortDescriptor
{
	GENERATED_BODY()

	/** 이 품목이 들어가야 하는 분류 칸 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag Category;

	/** 정분류일 때 Score 원장에 기록할 이벤트. 비어 있으면 카테고리 행에서 보완한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag CorrectSortScoreTag;

	/** 오분류일 때 Score 원장에 기록할 이벤트. 비어 있으면 카테고리 행에서 보완한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash",
		meta = (Categories = "Score.Penalty"))
	FGameplayTag MisSortScoreTag;

	/** 정분류 기본 점수에 추가할 품목별 이벤트 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting.Bonus"))
	FGameplayTag BonusScoreTag;

	/** 정분류 시 지급할 총 환급. INDEX_NONE이면 카테고리 기본값을 사용한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash",
		meta = (ClampMin = "-1"))
	int32 RefundCredits = INDEX_NONE;

	bool IsResolved() const
	{
		return Category.IsValid()
			&& CorrectSortScoreTag.IsValid()
			&& MisSortScoreTag.IsValid()
			&& RefundCredits >= 0;
	}
};

/** 순수 판정 결과. 실제 Score/Credit 원장 변경과 Actor 파괴는 Sorter가 담당한다. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashSortResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash")
	bool bCanSort = false;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash")
	bool bCorrectSort = false;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash")
	FGameplayTag PrimaryScoreTag;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash")
	FGameplayTag BonusScoreTag;

	/** 오분류 또는 판정 불가이면 0 */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash")
	int32 RefundCredits = 0;
};

/**
 * 분류 칸 한 개의 수치 (기획 02 §11 색, 07 §3.1 환급)
 * 점수 단가는 두지 않고 Score 이벤트 태그만 연결한다. 실제 단가는 DA_ScoreTable이 가진다.
 * 조회는 행 이름이 아니라 Category 컬럼으로 한다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashCategoryRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 이 행이 정의하는 칸. 조회 키 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	FText DisplayName;

	/** 기획 표기 그대로의 sRGB 색. 선형 변환은 UMD_TrashLibrary에서 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	FColor Color = FColor::White;

	/** 정분류 점수 이벤트. 기존 테이블 호환을 위해 비어 있으면 Category를 사용한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag CorrectSortScoreTag;

	/** 이 카테고리 품목의 오분류 감점 이벤트. 기존 테이블은 런타임 기본값으로 마이그레이션한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (Categories = "Score.Penalty"))
	FGameplayTag MisSortScoreTag;

	/** 종류 가산을 뺀 칸 기준 환급 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash", meta = (ClampMin = "0"))
	int32 RefundCredits = 0;
};

/**
 * 산재 쓰레기 한 종류 (기획 02 §1)
 * 행 이름이 종류 ID다. 종류 추가·삭제는 이 테이블의 행만 고치면 된다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashKindRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 모선에서 이 쓰레기를 넣어야 하는 칸 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	FText DisplayName;

	/** 칸 기본 환급에 더하는 품목 가산 (크레딧). 기획 07 §3.1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash", meta = (ClampMin = "0"))
	int32 RefundBonusCredits = 0;

	/**
	 * 정분류 시 얹을 품목 보너스 (기획 05 §1.1 S+06~09: 공구 +5, 작업복 +4, 유리 +4, 전지 +8).
	 * 위 RefundBonusCredits와 별개다 — 이쪽은 평가 점수, 저쪽은 크레딧
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting.Bonus"))
	FGameplayTag ScoreBonusTag;

	/** 운반 무게감. 메시 볼륨 자동 계산 대신 이 값으로 고정한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (ClampMin = "0.01", UIMin = "0.05", Units = "kg"))
	float MassKg = 1.0f;

	/** 비워 두면 배치한 Blueprint의 메시를 그대로 쓴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	TSoftObjectPtr<UStaticMesh> Mesh;
};
