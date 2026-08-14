#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MD_TrashContentClaimSubsystem.generated.h"

class AMD_TrashBin;

struct FContentClaim
{
	/** 현재 이 내용물을 입고한 단독 소유 통. */
	TWeakObjectPtr<AMD_TrashBin> Owner;

	/** 현재 물리적으로 겹쳐 있어 다음 소유자가 될 수 있는 통 목록. */
	TArray<TWeakObjectPtr<AMD_TrashBin>> OverlappingBins;
};

UCLASS()
class UMD_TrashContentClaimSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool RegisterOverlap(AActor* Content, AMD_TrashBin* Bin);
	void UnregisterOverlap(AActor* Content, AMD_TrashBin* Bin);
	/** Sorter로 소유권을 넘기고 이 내용물의 claim을 종료 */
	bool ConsumeForSorting(AActor* Content, AMD_TrashBin* Bin);
	bool IsOwnedBy(AActor* Content, const AMD_TrashBin* Bin) const;

	virtual void Deinitialize() override;	
private:

	UFUNCTION()
	void HandleContentDestroyed(AActor* DestroyedActor);
	
	void PromoteNextOwner(AActor* Content);
	void RemoveClaimIfUnused(AActor* Content);

	TMap<TWeakObjectPtr<AActor>, FContentClaim> Claims;
};
