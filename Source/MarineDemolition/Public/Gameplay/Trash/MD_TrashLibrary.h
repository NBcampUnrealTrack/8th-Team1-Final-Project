// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/Trash/MD_TrashTypes.h"
#include "MD_TrashLibrary.generated.h"

/**
 * 쓰레기 수치 테이블의 단일 조회 창구 (테이블 위치는 UMD_TrashSettings)
 * 테이블·행이 없으면 안전한 기본값(무효 태그, 자홍색)을 돌려주고 경고를 한 번 남긴다.
 * 점수 단가는 다루지 않는다. 칸 태그를 AddScoreEvent()에 넘기면 DA_ScoreTable이 준다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_TrashLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 종류 테이블의 전체 행 이름. 배치 드롭다운이 사용한다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static TArray<FName> GetTrashKindIds();

	/** 행이 없으면 false, OutRow는 기본값 */
	UFUNCTION(BlueprintCallable, Category = "MD|Trash")
	static bool FindTrashKindRow(FName TrashKindId, FMD_TrashKindRow& OutRow);

	/** 이 종류를 넣어야 하는 칸 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FGameplayTag GetTrashKindCategory(FName TrashKindId);

	/** 칸 기본 환급 + 품목 가산 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static int32 GetTrashKindRefundCredits(FName TrashKindId);

	/** 정분류 시 칸 점수에 더할 품목 보너스 이벤트. 보너스가 없으면 무효 태그 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FGameplayTag GetTrashKindScoreBonusTag(FName TrashKindId);

	/** 행이 없으면 false, OutRow는 기본값 */
	UFUNCTION(BlueprintCallable, Category = "MD|Trash")
	static bool FindTrashCategoryRow(FGameplayTag Category, FMD_TrashCategoryRow& OutRow);

	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FText GetCategoryDisplayName(FGameplayTag Category);

	/** 컨테이너·UI·현장 색 태그가 공유하는 칸 색 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FLinearColor GetCategoryColor(FGameplayTag Category);

	/** 품목 가산을 뺀 칸 기준 환급 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static int32 GetCategoryRefundCredits(FGameplayTag Category);

	/** 칸으로 쓸 수 있는 태그인지. Score.Sorting 밑의 Bonus.* 를 걸러낸다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static bool IsTrashCategory(FGameplayTag Category);

	/** 오분류 감점 태그. 기획 05 S−02가 오염계만 −24라 물건의 정답 칸으로 결정된다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FGameplayTag GetMisplacedSortScoreTag(FGameplayTag CorrectCategory);
};
