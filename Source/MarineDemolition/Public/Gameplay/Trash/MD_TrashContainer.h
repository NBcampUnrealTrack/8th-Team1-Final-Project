#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MD_TrashContainer.generated.h"

class AActor;

/**
 * 여러 월드 액터를 담아 한 번에 분류할 수 있는 컨테이너 계약.
 * 조회와 소비를 분리해 저장 시스템은 비파괴 snapshot만, Sorter는 일회성 drain만 사용한다.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UMD_TrashContainer : public UInterface
{
	GENERATED_BODY()
};

class MARINEDEMOLITION_API IMD_TrashContainer
{
	GENERATED_BODY()

public:
	/** 현재 컨테이너가 소유한 유효 액터의 비파괴 snapshot */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MD|Trash|Container")
	TArray<AActor*> GetContainedActors() const;
	virtual TArray<AActor*> GetContainedActors_Implementation() const { return {}; }

	/**
	 * 서버에서 단 한 번만 성공하는 분류용 소비 트랜잭션.
	 * 성공하면 컨테이너는 더 이상 입고를 받지 않고 OutContents의 소유권을 Sorter에 넘긴다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Trash|Container")
	bool TryDrainContentsForSorting(TArray<AActor*>& OutContents);
	virtual bool TryDrainContentsForSorting_Implementation(TArray<AActor*>& OutContents)
	{
		OutContents.Reset();
		return false;
	}

	/**
	 * Drain 이후 Sorter의 검증 또는 컨테이너 파괴가 실패했을 때 소유권을 되돌린다.
	 * 성공은 컨테이너가 다시 입고를 추적할 수 있는 정상 상태로 복구되었음을 뜻한다.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
		Category = "MD|Trash|Container")
	bool TryRestoreDrainedContentsAfterFailedSort(const TArray<AActor*>& Contents);
	virtual bool TryRestoreDrainedContentsAfterFailedSort_Implementation(
		const TArray<AActor*>& Contents)
	{
		return false;
	}
};
