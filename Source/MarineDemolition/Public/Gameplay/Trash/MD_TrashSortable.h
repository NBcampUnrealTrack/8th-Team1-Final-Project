#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "MD_TrashSortable.generated.h"

/**
 * 분류기에 넣어 점수를 받는 물건 (기획 02 §11)
 * 산재 쓰레기 외에 조각벽·오염물·따개비도 칸을 가진다. 분류기는 타입을 모른 채 이것만 호출한다.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UMD_TrashSortable : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_TrashSortable
{
	GENERATED_BODY()

public:
	/** 정답 칸. Score.Sorting.* 이며 그대로 점수 이벤트 태그로 쓰인다 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Trash")
	FGameplayTag GetTrashCategory() const;

	/** 정분류 시 얹을 품목 보너스 (기획 05 §1.1 S+06~09). 없으면 무효 태그 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Trash")
	FGameplayTag GetSortScoreBonusTag() const;
};
