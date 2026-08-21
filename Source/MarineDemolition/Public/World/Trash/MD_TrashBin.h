#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashContainer.h"
#include "MD_TrashBin.generated.h"

class UBoxComponent;
class UMD_TrashContentClaimSubsystem;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

struct FMD_CarryFrozenContent
{
	TWeakObjectPtr<AActor> Actor;
	TEnumAsByte<ECollisionEnabled::Type> PreviousCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnTrashBinContentsChanged, int32, ContentCount);

UCLASS()
class MARINEDEMOLITION_API AMD_TrashBin	: public AActor, public IGameplayTagAssetInterface, public IMD_Grabbable, public IMD_Interactable, public IMD_TrashContainer
{
	GENERATED_BODY()

public:
	AMD_TrashBin();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	/** Implements IMD_Grabbable*/
	virtual UPrimitiveComponent* GetGrabComponent_Implementation() const override;
	virtual USceneComponent* GetGrabPoint_Implementation() const override;
	virtual FVector GetHoldOffset_Implementation() const override;
	virtual FMD_HoldProfile GetHoldProfile_Implementation() const override;
	virtual void OnCarryStarted_Implementation() override;
	virtual void OnCarrySettled_Implementation() override;
	virtual void OnCarryEnded_Implementation() override;
	/** ~Implements IMD_Grabbable*/

	/** Implements IMD_Interactable*/
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual bool ShouldPrioritizeInteractionOverPhysicsPickup_Implementation(
		AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	/** ~Implements IMD_Interactable*/

	/** Implements IMD_TrashContainer */
	virtual TArray<AActor*> GetContainedActors_Implementation() const override;
	virtual bool TryDrainContentsForSorting_Implementation(TArray<AActor*>& OutContents) override;
	/** ~Implements IMD_TrashContainer */

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin")
	int32 GetContentCount() const { return ContentCount; }

	/** ClaimSubsystem이 이 통을 후보 소유자로 판단할 때 사용하는 수용 가능 여부 조회. */
	bool CanAcceptContentClaim(AActor* Content) const;

	/** ClaimSubsystem에서 이 통의 소유권이 확인된 내용물만 실제 입고 상태에 반영*/
	bool TryApplyGrantedContentClaim(AActor* Content);

	/** 현재 물리 overlap을 기준으로 서버의 내용물 목록을 다시 맞춘다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Bin")
	void RefreshContentsFromOverlaps();

	/** 서버 입고와 클라이언트 ContentCount OnRep 양쪽에서 한 번씩 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Trash Bin")
	FMDOnTrashBinContentsChanged OnContentsChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 물리 운반 시 잡을 위치 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<USceneComponent> GrabPoint;

	/** 캐릭터 HoldPoint의 로컬 축 기준 운반 위치 오프셋 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction", meta = (Units = "cm"))
	FVector HoldOffset = FVector::ZeroVector;

	/** 이 쓰레기통을 운반하는 동안 캐릭터 PhysicsHandle에 적용할 설정 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction")
	FMD_HoldProfile HoldProfile;

	/** 내부 판정 영역 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UBoxComponent> ContentsVolume;

	/** 픽업 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",	meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 개별 상호작용 한 번이 현재 장착한 헤라에서 덜어내는 최대 적재량. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Scrape Unload",
		meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "20.0"))
	float ScrapeLoadUnloadAmount = 20.0f;

	/** 쓰레기통이 수용할 내용물 분류 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin", meta = (Categories = "Resource.Storage.Content"))
	FGameplayTagContainer AcceptedContentTags;

	/** 현재 이 통이 단독 소유한 내용물 수. 적재 상한 x */
	UPROPERTY(ReplicatedUsing = OnRep_ContentCount, VisibleInstanceOnly, BlueprintReadOnly,	Category = "MD|Trash Bin")
	int32 ContentCount = 0;

private:
	/** 설정된 감산량이 유한한 양수일 때만 최대 20으로 제한해 반환한다. */
	bool TryGetScrapeLoadUnloadAmount(float& OutRequestedAmount) const;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	UFUNCTION()
	void HandleContentDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnRep_ContentCount();

	UFUNCTION()
	void OnRep_CarryFrozenContents();

	/** TSet의 내용물을 TArray로 이동하는 함수 */
	void GetContentsSnapshot(TArray<AActor*>& OutContents) const;
	bool IsAcceptedContent(const AActor* Actor) const;
	bool TryAdmitContent(AActor* Actor);
	bool RemoveAdmittedContent(AActor* Actor);
	void RebuildContentsFromOverlaps();
	void SyncContentCount();
	UMD_TrashContentClaimSubsystem* GetContentClaimSubsystem() const;

	/** 운반 시 동결 */
	void FreezeContentsForCarry();
	bool FreezeContentForCarry(AActor* Content);

	/** 붙여둔 내용물을 떼고 물리·충돌을 되돌린다 */
	void ReleaseContentsAfterCarry(bool bRefreshContents = true);
	void UnfreezeContentForCarry(AActor* Content);
	void ApplyReplicatedCarryCollisionState();
	void RemoveFrozenContentReferences(AActor* Content);

	UPROPERTY(ReplicatedUsing = OnRep_CarryFrozenContents)
	TArray<TObjectPtr<AActor>> ReplicatedCarryFrozenContents;

	TSet<TWeakObjectPtr<AActor>> Contents;
	TMap<TWeakObjectPtr<AActor>, int32> ContentOverlapCounts;

	bool bSortingTransactionActive = false;
	bool bDrainInProgress = false;
	bool bCarryAccelerationActive = false;
	TArray<FMD_CarryFrozenContent> CarryFrozenContents;
	TMap<TWeakObjectPtr<AActor>, TEnumAsByte<ECollisionEnabled::Type>> ClientCarryCollisionOverrides;
};
