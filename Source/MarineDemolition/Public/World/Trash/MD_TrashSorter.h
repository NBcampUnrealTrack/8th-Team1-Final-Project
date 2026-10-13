#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "World/Trash/MD_TrashSorterAnimationComponent.h"
#include "Types/Facility/MD_FacilityTypes.h"
#include "MD_TrashSorter.generated.h"

class UMD_InteractionPromptComponent;

class UBoxComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UStateTreeComponent;
class AMD_GameStateBase;
class UMD_MerchantSubsystem;
class UMD_FacilityFaultComponent;
class UMD_FacilityRepairComponent;
struct FMD_TrashSortResult;

/** Keep the animation result and timing together when replicating to clients. */
USTRUCT()
struct FMD_TrashSorterRuntimeState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bReady = false;
	UPROPERTY()
	bool bSorting = false;
	UPROPERTY()
	bool bHasResult = false;
	UPROPERTY()
	bool bCorrectSort = false;
	UPROPERTY()
	double StartServerTime = 0.0;
	UPROPERTY()
	float Duration = 0.0f;
	UPROPERTY()
	FMD_SorterItemVisual ItemVisual;
};

/**
 * 한 품목이 실제로 소비된 뒤 서버에서 한 번 방송한다.
 * 액터가 이미 파괴된 뒤라 포인터 대신 클래스를 넘긴다.
 * SorterCategory는 이 분류기가 담당하는 칸이다 — bCorrectSort가 true면 품목의 칸과 같다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMD_OnTrashSorted,
	TSubclassOf<AActor>, ItemClass,
	bool, bCorrectSort,
	FGameplayTag, SorterCategory);

UCLASS()
class MARINEDEMOLITION_API AMD_TrashSorter
	: public AActor
	, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_TrashSorter();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Facility")
	TObjectPtr<UMD_FacilityRepairComponent> FacilityRepair;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Implements IMD_Interactable */
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual bool SupportsHeldActorInteraction_Implementation() const override { return true; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	/** ~Implements IMD_Interactable */

	/** 이 분류기가 정분류로 인정하는 칸 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	FGameplayTag GetAcceptedCategory() const { return AcceptedCategory; }

	/** 지금까지 삼킨 물건 수. 정분류·오분류를 가리지 않는다 */
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	int32 GetSortedCount() const { return SortedCount; }

	/** Safety shutdown is independent of this machine's repairable fault. */
	void SetSafetyInterlock(AActor* Source, bool bLocked);
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	bool IsSafetyInterlocked() const { return bSafetyInterlocked; }
	UFUNCTION(BlueprintPure, Category = "MD|Trash")
	bool IsOperationBlocked() const;

	/** 품목이 소비될 때마다 서버에서 방송. 튜토리얼 진행 판정이 구독한다 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Trash")
	FMD_OnTrashSorted OnTrashSorted;

	/** Pure item eligibility query; independent of the machine's Ready state. */
	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	bool CanAcceptHeldItem(AActor* Interactor) const;

	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	bool HasPendingSortRequest() const { return bHasPendingSortRequest; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	AActor* GetPendingInteractor() const { return PendingInteractor.Get(); }

	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	bool IsSorting() const { return RuntimeState.bSorting; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	bool HasSortResult() const { return RuntimeState.bHasResult; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	bool WasCorrectSort() const { return RuntimeState.bHasResult && RuntimeState.bCorrectSort; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash|StateTree")
	float GetSortingRemainingTime() const;

	/** StateTree adapters call these. All gameplay work stays inside the sorter. */
	bool EnterReadyState();
	void ExitReadyState();
	bool BeginPendingSort();
	void EndSortingState();
	bool BeginErrorState();

	/** Server intake, also called after a throw released inside the volume. Queues the same sort as Interact. */
	bool TryRequestIntake(AActor* Item);
	// 액터별 행동 유형을 공통 프롬프트 설정에 연결하는 구성
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<UMD_InteractionPromptComponent> InteractionPrompt;

protected:
	/** 배치 인스턴스에서 칸을 바꿔도 에디터에 바로 색이 반영되게 한다 */
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 삼킬 때 연출 훅. 소리 없이 사라지면 버그와 구분이 안 된다 */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash")
	void OnItemSwallowed(bool bCorrectSort, const FVector& Location);

	/** Legacy BP event signature only. Container intake is no longer supported or invoked. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash", meta = (DeprecatedFunction, DeprecationMessage = "Container intake is disabled. Use OnItemSwallowed."))
	void OnContainerSwallowed(
		int32 CorrectCount,
		int32 MisSortCount,
		int32 DiscardedCount,
		int32 RefundCredits,
		const FVector& Location);

	/** Runs on server/listen host and on clients via replication, including late relevancy. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash|Animation")
	void OnSortingStateChanged(bool bSorting, bool bCorrectSort, float RemainingSeconds);

	/** Optional cosmetic hook. An empty implementation is valid. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MD|Trash|StateTree")
	void OnSortErrorFeedback();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash|StateTree")
	TObjectPtr<UStateTreeComponent> SorterStateTree;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash|Animation")
	TObjectPtr<UMD_TrashSorterAnimationComponent> SortingAnimation;

	/** Shared timing input: bind the Sorting state's Delay Duration to this value. Also drives replicated presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash|Animation", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float SortingDuration = 1.0f;

	/** Bind the Error state's Delay Duration to this value. The actor does not run a timer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash|StateTree", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float ErrorDuration = 0.5f;

	/** Legacy data only; play sound from OnSortErrorFeedback in Blueprint. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use OnSortErrorFeedback."))
	FGameplayTag ErrorSoundTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 손에서 놓인 쓰레기가 실제 투입구 안에 들어왔는지 서버에서 감지한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	TObjectPtr<UBoxComponent> IntakeVolume;

	/** 담당 칸. 드롭다운에 Bonus.* 도 뜨지만 그건 칸이 아니다 — BeginPlay에서 걸러 경고한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_AcceptedCategory, Category = "MD|Trash",
		meta = (Categories = "Score.Sorting"))
	FGameplayTag AcceptedCategory;

	/** 그레이박스에서 칸을 구분하기 위한 색 적용. 최종 아트로 교체하면 끈다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash")
	bool bTintPlaceholderMeshByCategory = true;

	/** 색을 넣을 벡터 파라미터 이름. 기본값은 배치용 M_MD_TrashTint의 Tint (M_Master_Opaque는 Color다) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash",
		meta = (EditCondition = "bTintPlaceholderMeshByCategory"))
	FName TintParameterName = TEXT("Tint");

	/** Legacy data only; play sound from OnItemSwallowed in Blueprint. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use OnItemSwallowed."))
	FGameplayTag InsertSoundTag;

	/** 지금까지 삼킨 물건 수. UI 표시용이라 복제한다 */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "MD|Trash")
	int32 SortedCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SorterMID;

private:
	UPROPERTY(Replicated)
	bool bSafetyInterlocked = false;
	TArray<TWeakObjectPtr<AActor>> SafetyInterlockSources;
	void RefreshOperationBlock();

	UFUNCTION()
	void OnRep_AcceptedCategory();
	UFUNCTION()
	void HandleFacilityState(UMD_FacilityFaultComponent* Facility, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current);
	/** 액터는 서버에서 파괴되므로 위치를 실어 보낸다 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastItemSwallowed(bool bCorrectSort, FVector Location);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastErrorFeedback();

	UFUNCTION()
	void OnRep_RuntimeState(const FMD_TrashSorterRuntimeState& PreviousState);

	UPROPERTY(ReplicatedUsing = OnRep_RuntimeState)
	FMD_TrashSorterRuntimeState RuntimeState;

	TWeakObjectPtr<AActor> PendingInteractor;
	TWeakObjectPtr<AActor> PendingItem;
	bool bHasPendingSortRequest = false;
	bool bPendingIntake = false;

	UFUNCTION()
	void HandleIntakeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	bool CanAcceptIntakeItem(AActor* Item) const;
	void RequestOverlappingIntake();
	void QueueSortRequest(AActor* Item, AActor* Interactor, bool bFromIntake);
	void ClearPendingSortRequest();
	void RefreshSortingPresentation();

	bool ResolveSortResult(AActor* Item, FMD_TrashSortResult& OutResult) const;
	bool ConsumeSortable(
		AActor* Item,
		AMD_GameStateBase* MDGameState,
		const FMD_TrashSortResult& Result,
		int32& OutRefundCredits);
	bool GetServerLedgers(
		AMD_GameStateBase*& OutGameState,
		UMD_MerchantSubsystem*& OutMerchant) const;
	AActor* GetHeldSortableForInteractor(AActor* Interactor) const;

	/** 담당 칸 색을 메시에 반영 */
	void ApplyCategoryTint();
};
