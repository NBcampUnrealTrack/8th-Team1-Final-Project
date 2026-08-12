#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/Trash/MD_TrashTypes.h"
#include "UObject/Interface.h"
#include "MD_TrashSortable.generated.h"

/**
 * 분류기에 넣어 점수를 받는 물건
 * 산재 쓰레기 외에 조각벽·오염물·따개비도 칸을 가짐. 분류기는 타입을 모른 채 이것만 호출
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
	/**
	 * 분류 정책 전체를 한 번에 제공하는 확장 계약.
	 * 기본 구현이 비어 있으면 TrashLibrary가 아래의 레거시 두 함수를 데이터 테이블과 결합해 보완한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Trash")
	FMD_TrashSortDescriptor GetTrashSortDescriptor() const;
	virtual FMD_TrashSortDescriptor GetTrashSortDescriptor_Implementation() const
	{
		return FMD_TrashSortDescriptor();
	}

	/** 정답 칸. 기존 구현의 하위 호환용이며 신규 타입은 descriptor 하나만 구현해도 된다 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Trash")
	FGameplayTag GetTrashCategory() const;
	virtual FGameplayTag GetTrashCategory_Implementation() const { return FGameplayTag(); }

	/** 정분류 시 얹을 품목 보너스 (기획 05 §1.1 S+06~09). 없으면 무효 태그 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Trash")
	FGameplayTag GetSortScoreBonusTag() const;
	virtual FGameplayTag GetSortScoreBonusTag_Implementation() const { return FGameplayTag(); }
};
