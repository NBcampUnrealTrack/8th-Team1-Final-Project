#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashContainer.h"
#include "MD_TrashBin.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMDOnTrashBinContentsChanged,
	int32, ContentCount,
	int32, MaxCapacity);

UCLASS()
class MARINEDEMOLITION_API AMD_TrashBin
	: public AActor
	, public IGameplayTagAssetInterface
	, public IMD_Grabbable
	, public IMD_Interactable
	, public IMD_TrashContainer
{
	GENERATED_BODY()

public:
	AMD_TrashBin();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;
	virtual UPrimitiveComponent* GetGrabComponent_Implementation() const override;
	virtual USceneComponent* GetGrabPoint_Implementation() const override;
	virtual FVector GetHoldOffset_Implementation() const override;
	virtual FMD_HoldProfile GetHoldProfile_Implementation() const override;
	virtual void OnCarryStarted_Implementation() override;
	virtual void OnCarrySettled_Implementation() override;
	virtual void OnCarryEnded_Implementation() override;

	/** Implements IMD_Interactable*/
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	/** ~Implements IMD_Interactable*/

	/** Implements IMD_TrashContainer */
	virtual TArray<AActor*> GetContainedActors_Implementation() const override;
	virtual bool TryDrainContentsForSorting_Implementation(TArray<AActor*>& OutContents) override;
	virtual bool TryRestoreDrainedContentsAfterFailedSort_Implementation(
		const TArray<AActor*>& Contents) override;
	/** ~Implements IMD_TrashContainer */

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin")
	int32 GetContentCount() const { return ContentCount; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin")
	int32 GetMaxCapacity() const { return FMath::Max(1, MaxCapacity); }

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin")
	bool IsFull() const { return ContentCount >= GetMaxCapacity(); }

	/** 분류 판정용 비파괴 C++ snapshot. 내부 collection 참조는 노출하지 않는다. */
	void GetContentsSnapshot(TArray<AActor*>& OutContents) const;

	/** 현재 물리 overlap을 기준으로 서버의 내용물 목록을 다시 맞춘다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Bin")
	void RefreshContentsFromOverlaps();

	/** 서버 입고와 클라이언트 ContentCount OnRep 양쪽에서 한 번씩 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Trash Bin")
	FMDOnTrashBinContentsChanged OnContentsChanged;

protected:
	/** 서버에서 내부 내용물의 진입과 이탈 추적을 시작 */
	virtual void BeginPlay() override;
	/** 오버랩 및 내용물 파괴 이벤트 연결을 정리 */
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

	/** 내부로 판정할 영역 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UBoxComponent> ContentsVolume;

	/** 이 액터를 집을 때 사용할 물리 운반 방식 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Interaction",	meta = (Categories = "Interaction.Pickup.Physics"))
	FGameplayTag PickupMethodTag;

	/** 이 보관함이 수용할 내용물 분류 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin", meta = (Categories = "Resource.Storage.Content"))
	FGameplayTagContainer AcceptedContentTags;

	/** 고체 Actor 기준 최대 적재 수. 0은 허용하지 않는다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxCapacity = 12;

	UPROPERTY(ReplicatedUsing = OnRep_ContentCount, VisibleInstanceOnly, BlueprintReadOnly,
		Category = "MD|Trash Bin")
	int32 ContentCount = 0;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	UFUNCTION()
	void HandleContentDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnRep_ContentCount();

	bool IsAcceptedContent(const AActor* Actor) const;
	bool TryAdmitContent(AActor* Actor);
	bool RemoveAdmittedContent(AActor* Actor);
	void AdmitWaitingContents();
	void RebuildContentsFromOverlaps(bool bPreserveAdmissionOrder);
	void RefreshContentsFromOverlapsPreservingAdmissions();
	void SyncContentCount();

	/** 끌려가는 동안 내용물을 통에 붙인다. 안 그러면 통 벽이 때려 대부분 튀어나간다 */
	void FreezeContentsForCarry();

	/** 붙여둔 내용물을 떼고 물리·충돌을 되돌린다 */
	void ReleaseContentsAfterCarry();

	TSet<TWeakObjectPtr<AActor>> Contents;

	/** Begin/End overlap을 actor 단위 ref-count로 합쳐 매 이벤트마다 전체 volume을 재조회하지 않는다. */
	TMap<TWeakObjectPtr<AActor>, int32> ContentOverlapCounts;
	TArray<TWeakObjectPtr<AActor>> ContentAdmissionOrder;

	/** 통째 분류 drain은 서버 game thread에서 단 한 번만 성공한다. */
	bool bSortingTransactionActive = false;

	struct FMD_CarryFrozenContent
	{
		TWeakObjectPtr<AActor> Actor;
		TEnumAsByte<ECollisionEnabled::Type> PreviousCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	};

	/** Contents와 따로 둔다. 붙어 있는 동안 오버랩이 흔들려도 되돌릴 대상은 남아야 한다 */
	TArray<FMD_CarryFrozenContent> CarryFrozenContents;
};
