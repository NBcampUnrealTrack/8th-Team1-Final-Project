// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "MD_TrashTypes.generated.h"

class UStaticMesh;

/**
 * 분류 칸 한 개의 수치 (기획 02 §11 색, 07 §3.1 환급)
 * 점수 단가는 두지 않는다. DA_ScoreTable이 같은 태그로 들고 있어 이중 관리가 된다.
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

	/** 종류 가산을 뺀 칸 기준 환급 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
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
