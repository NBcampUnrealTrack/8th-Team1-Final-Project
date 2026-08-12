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

	/** 종류 행과 카테고리 행을 결합한 완성된 분류 정책 */
	UFUNCTION(BlueprintCallable, Category = "MD|Trash")
	static bool GetTrashKindSortDescriptor(FName TrashKindId, FMD_TrashSortDescriptor& OutDescriptor);

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

	/** 카테고리 DataTable에 실제 행으로 정의된 칸인지 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static bool IsTrashCategory(FGameplayTag Category);

	/** 카테고리의 정분류 점수 이벤트. 미설정인 기존 행은 Category로 호환한다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FGameplayTag GetCorrectSortScoreTag(FGameplayTag Category);

	/** 카테고리의 오분류 점수 이벤트. 미설정인 기존 행은 기존 규칙으로 호환한다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FGameplayTag GetMisplacedSortScoreTag(FGameplayTag CorrectCategory);

	/** 신규 descriptor 또는 기존 인터페이스 함수를 DataTable 정책으로 보완한다 */
	UFUNCTION(BlueprintCallable, Category = "MD|Trash")
	static bool ResolveSortDescriptor(UObject* SortableObject, FMD_TrashSortDescriptor& OutDescriptor);

	/** 원장 변경 없이 정분류 여부·점수 이벤트·환급을 계산하는 순수 정책 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	static FMD_TrashSortResult EvaluateSort(
		const FMD_TrashSortDescriptor& Descriptor,
		FGameplayTag AcceptedCategory);
};
