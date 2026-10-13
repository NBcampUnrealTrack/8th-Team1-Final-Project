#pragma once

#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "Gameplay/Interaction/MD_Grabbable.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "Gameplay/Trash/MD_TrashContainer.h"
#include "Types/Trash/MD_TrashBinSpillTypes.h"
#include "MD_TrashBin.generated.h"

class UBoxComponent;
class UMD_TrashContentClaimSubsystem;
class UMD_TrashBinLiquidSloshVisualComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class APawn;
class AMD_TrashDispenser;
class AMD_WetContaminationPatch;


struct FMD_CarryFrozenContent
{
	TWeakObjectPtr<AActor> Actor;
	TEnumAsByte<ECollisionEnabled::Type> PreviousCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
};

/**
 * 고체 ContentCount와 별도로 복제되는 통의 액체 원장 요약.
 * LiquidUnits는 관리 부피, RewardUnits는 나중에 정분류할 경제 가치다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashBinLiquidState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash Bin|Liquid")
	double LiquidUnits = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash Bin|Liquid")
	double RewardUnits = 0.0;

	/** 같은 값 쌍을 한 번에 관찰하고 늦게 도착한 이전 알림을 구분하는 번호. */
	UPROPERTY(BlueprintReadOnly, Category = "MD|Trash Bin|Liquid")
	int32 Revision = 0;

	bool IsValid(double MaximumLiquidUnits) const;
};

/**
 * 성공한 한 spill transaction의 짧은 시청각 연출 입력.
 *
 * 이 값은 unreliable multicast 한 번에만 실리고 복제 상태로 저장되지 않는다.
 * 따라서 late client는 현재 Wet 상태만 받고 과거 one-shot은 재생하지 않는다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinSpillCosmeticResult
{
	GENERATED_BODY()

	/** Stable per-bin sequence used by every observing peer for stale/dedupe. */
	UPROPERTY()
	FMD_TrashBinSpillTransactionId TransactionId;

	/** Post-commit replicated state paired with this unreliable cosmetic. */
	UPROPERTY()
	int32 CommittedLiquidRevision = INDEX_NONE;

	UPROPERTY()
	double RemainingLiquidUnits = 0.0;

	/** 서버가 commit 순간 확정한 월드 입구점. RPC/RepMovement 도착 순서와 무관하다. */
	UPROPERTY()
	FVector_NetQuantize10 SourceWorldLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize10 ContactWorldLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal ContactWorldNormal = FVector::UpVector;

	UPROPERTY()
	float SpillUnits = 0.0f;

	UPROPERTY()
	EMD_TrashBinSpillTrigger Trigger = EMD_TrashBinSpillTrigger::None;

	bool IsValid() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDOnTrashBinContentsChanged, int32, ContentCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMDOnTrashBinLiquidChanged,
	double,
	LiquidUnits,
	double,
	RewardUnits);

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
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	/** ~Implements IMD_Interactable*/

	/** Implements IMD_TrashContainer */
	virtual TArray<AActor*> GetContainedActors_Implementation() const override;
	virtual bool TryDrainContentsForSorting_Implementation(TArray<AActor*>& OutContents) override;
	/** ~Implements IMD_TrashContainer */

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin")
	int32 GetContentCount() const { return ContentCount; }

	/** 통의 현재 관리 액체량. 0.0~5.0 범위다. */
	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin|Liquid")
	double GetLiquidUnits() const { return LiquidState.LiquidUnits; }

	/** 현재 액체가 가진 정분류 가능 가치. 보상 0 잔사가 섞이면 LiquidUnits보다 작다. */
	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin|Liquid")
	double GetLiquidRewardUnits() const { return LiquidState.RewardUnits; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin|Liquid")
	double GetMaximumLiquidUnits() const { return MaximumLiquidUnits; }

	UFUNCTION(BlueprintPure, Category = "MD|Trash Bin|Liquid")
	float GetLiquidFillRatio() const;

	/** 쏟김 transaction과 Day/세이브 복원에서 같은 통을 식별하는 서버 stable ID. */
	const FGuid& GetStableBinId() const { return StableBinId; }

	/** 이 동적 통을 만든 레벨 배치 보급기의 안정적인 이름. NAME_None이면 비보급 통이다. */
	FName GetSourceTrashDispenserName() const { return SourceTrashDispenserName; }

	// 재등록할 보급기의 원래 배치 구역 조회
	FORCEINLINE FName GetSourceTrashDispenserLevelPackage() { return SourceTrashDispenserLevelPackage; }

	/** 마지막으로 성공 commit한 쏟김 sequence. 신규/구버전은 0이다. */
	uint64 GetNextSpillSequence() const { return NextSpillSequence; }

	/** 상태를 바꾸지 않고 다음 sequence(current+1)를 준비한다. overflow면 false다. */
	bool TryPreviewNextSpillSequence(uint64& OutSequence) const;

	/** 성공한 spill transaction의 prepared sequence만 한 번 확정한다. */
	bool CommitNextSpillSequence(uint64 PreparedSequence);


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

	/** LiquidUnits/RewardUnits 쌍이 실제로 바뀐 경우에만 서버와 OnRep에서 한 번 방송한다. */
	UPROPERTY(BlueprintAssignable, Category = "MD|Trash Bin|Liquid")
	FMDOnTrashBinLiquidChanged OnLiquidChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_ReplicatedMovement() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 물리 운반 시 잡을 위치 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Interaction")
	TObjectPtr<USceneComponent> GrabPoint;

	/** 액체가 쏟아지는 통 입구 기준점. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin|Liquid")
	TObjectPtr<USceneComponent> SpillMouthOrigin;

	/** 통 내부 바닥 기준점. 사용자는 BP 뷰포트에서 이 위치만 실제 통 안쪽에 맞춘다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin|Liquid")
	TObjectPtr<USceneComponent> LiquidVisualRoot;

	/** 복제 상태를 읽어 높이만 바뀌는 무충돌 영속 채움 메시. 자체 상태는 소유하지 않는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Bin|Liquid")
	TObjectPtr<UStaticMeshComponent> LiquidFillMesh;


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

	/** 100cm 기준 채움 표면을 통 내부 폭에 맞추는 반폭(기존 설정명은 호환을 위해 유지). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Liquid|Visual",
		meta = (ClampMin = "1.0", Units = "cm"))
	float LiquidVisualRadiusCm = 34.0f;

	/** LiquidUnits 5.0일 때 통 내부 바닥에서 위로 차오르는 최대 높이. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Liquid|Visual",
		meta = (ClampMin = "1.0", Units = "cm"))
	float LiquidVisualFullHeightCm = 48.0f;

	/** 아주 적은 양도 얇은 젤 층으로 읽히게 하는 최소 표시 두께. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Liquid|Visual",
		meta = (ClampMin = "0.1", Units = "cm"))
	float LiquidVisualMinimumHeightCm = 1.5f;

	/** 서버 기울기·충돌·pulse·접점 판정이 함께 읽는 단일 쏟김 튜닝 묶음. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Spill")
	FMD_TrashBinSpillTuning SpillTuning;

	/** 유효 바닥에 원자 commit될 서버 권위 Wet 운반체. 비어 있거나 시각 설정이 불완전하면 원장을 바꾸지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Spill")
	TSubclassOf<AMD_WetContaminationPatch> WetContaminationPatchClass;

	/** 성공한 쏟김 pulse를 통 안쪽에서 접점까지 보여 주는 전용 one-shot Niagara. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Spill|Cosmetic")
	TSoftObjectPtr<UNiagaraSystem> SpillEffect;

	/** 성공한 쏟김 pulse마다 접점에서 한 번 재생하는 낙하음 event tag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin|Spill|Cosmetic")
	FGameplayTag SpillSoundTag;

	/** 쓰레기통이 수용할 내용물 분류 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Trash Bin", meta = (Categories = "Resource.Storage.Content"))
	FGameplayTagContainer AcceptedContentTags;

	/** 현재 이 통이 단독 소유한 내용물 수. 적재 상한 x */
	UPROPERTY(ReplicatedUsing = OnRep_ContentCount, VisibleInstanceOnly, BlueprintReadOnly,	Category = "MD|Trash Bin")
	int32 ContentCount = 0;

	/** Liquid/Reward를 한 RepNotify로 묶어 서로 다른 프레임의 값이 섞이지 않게 한다. */
	UPROPERTY(ReplicatedUsing = OnRep_LiquidState, VisibleInstanceOnly, BlueprintReadOnly,
		Category = "MD|Trash Bin|Liquid")
	FMD_TrashBinLiquidState LiquidState;

	/** 저장 복원에서 sequence가 되감겨도 원격 cosmetic dedupe를 다시 맞춘다. */
	UPROPERTY(ReplicatedUsing = OnRep_SpillCosmeticEpoch)
	uint32 SpillCosmeticEpoch = 0;

	/**
	 * 성공한 입력마다 각 관찰 머신에서 정확히 한 번 호출되는 선택적 Blueprint 확장 지점.
	 * 기본 Niagara는 C++가 이미 재생하므로 같은 효과를 여기서 다시 Spawn하지 않는다.
	 * 필요하면 추후 짧은 효과음이나 손 동작 같은 별도 cosmetic만 연결한다.
	 */

private:
	/** 성공 결과를 모든 관찰자에게 전달한다. 연출 전용이라 한 번 유실돼도 상태는 유지된다. */

	/** 성공 commit을 현재 관찰자에게만 전달한다. 과거 event를 보관하지 않는다. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCommittedSpillCosmetic(
		const FMD_TrashBinSpillCosmeticResult& Result);

	/** 설정된 감산량이 유한한 양수일 때만 최대 20으로 제한해 반환한다. */

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	UFUNCTION()
	void HandleContentDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnRep_ContentCount(int32 PreviousCount);

	UFUNCTION()
	void OnRep_LiquidState();

	UFUNCTION()
	void OnRep_SpillCosmeticEpoch();

	UFUNCTION()
	void OnRep_CarryFrozenContents();

	UFUNCTION()
	void HandleSpillMeshWake(UPrimitiveComponent* WakingComponent, FName BoneName);

	UFUNCTION()
	void HandleSpillMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName);

	/** TSet의 내용물을 TArray로 이동하는 함수 */
	void GetContentsSnapshot(TArray<AActor*>& OutContents) const;
	bool IsAcceptedContent(const AActor* Actor) const;
	bool TryAdmitContent(AActor* Actor);
	bool RemoveAdmittedContent(AActor* Actor);
	void RebuildContentsFromOverlaps();
	void SyncContentCount();
	void RefreshLiquidVisual();
	void ResetSpillCosmeticAfterRestoreEpoch();
	void NotifyLiquidStateChanged();

	enum class ESpillAttemptResult : uint8
	{
		Committed,
		NoLiquid,
		NoReceiver,
		Rejected
	};

	void EnsureSpillSampleTimer();
	void StopSpillSampleTimer();
	void RefreshSpillSamplingAfterLiquidMutation();
	void SampleSpillTilt();
	ESpillAttemptResult TryExecuteSpillPulse(
		EMD_TrashBinSpillTrigger Trigger);
	bool TryBuildSpillRemovalCandidate(
		EMD_TrashBinSpillTrigger Trigger,
		FMD_TrashBinSpillCandidate& OutCandidate,
		FMD_TrashBinLiquidState& OutRemainingState,
		TArray<FMD_ScrapeRecoveryLot>& OutRemainingLots) const;
	bool TryFindSpillContact(
		FMD_TrashBinSpillContact& OutContact,
		UPrimitiveComponent*& OutSupportComponent,
		FVector& OutSourceWorldLocation) const;
	FVector CalculateSpillMouthCenterWorldLocation() const;
	FVector CalculateSpillMouthSourceWorldLocation() const;
	bool CanCommitSpillRemoval(
		const FMD_TrashBinSpillCandidate& Candidate,
		const FMD_TrashBinLiquidState& RemainingState,
		TConstArrayView<FMD_ScrapeRecoveryLot> RemainingLots,
		const AMD_WetContaminationPatch* PreparedPatch,
		const FMD_TrashBinSpillPreparedSinkToken& SinkToken) const;
	void EnsureStableBinId();
	void SetSourceTrashDispenser(AMD_TrashDispenser* SourceDispenser);
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

	/** 서버만 보유하는 source별 전체 manifest. 클라이언트는 LiquidState 요약만 받는다. */
	TArray<FMD_ScrapeRecoveryLot> LiquidRecoveryLots;

	/** 생성 세션의 액터 이름과 무관하게 같은 물리 통을 식별한다. 서버/저장 전용이다. */
	UPROPERTY()
	FGuid StableBinId;

	/** 원래 TrashDispenser 추적 원장에 복원 후 재등록하기 위한 레벨 배치 이름. */
	UPROPERTY()
	FName SourceTrashDispenserName = NAME_None;

	// 운반으로 통의 위치가 바뀌어도 유지할 원래 보급기 구역
	UPROPERTY()
	FName SourceTrashDispenserLevelPackage = NAME_None;

	/** 마지막으로 성공한 쏟김 sequence high-water. 신규·필드 없는 구버전은 0이다. */
	UPROPERTY()
	uint64 NextSpillSequence = 0;

	bool bLiquidTransferInProgress = false;

	/** 렌더링 peer의 BeginPlay에서만 만드는 비복제·시각 전용 출렁임 드라이버. */
	UPROPERTY(Transient)
	TObjectPtr<UMD_TrashBinLiquidSloshVisualComponent> LiquidSloshVisualComponent;

	/** unreliable cosmetic RPC의 stale/duplicate를 시청각 연출 전에 차단한다. */
	FGuid LastConsumedSpillCosmeticStableBinId;
	uint64 LastConsumedSpillCosmeticSequence = 0;
	int32 LastRenderedLiquidRevision = INDEX_NONE;
	FTimerHandle SpillCosmeticRestoreTimerHandle;

	/** FRepMovement의 deg/s를 로컬 Chaos 시각 드라이버의 rad/s로 맞춘다. */
	static FVector ConvertReplicatedAngularVelocityToRadians(
		const FVector& AngularVelocityDegreesPerSecond);

	FTimerHandle SpillSampleTimerHandle;

	static constexpr double MaximumLiquidUnits = 5.0;

	bool bSortingTransactionActive = false;
	bool bDrainInProgress = false;
	bool bCarryAccelerationActive = false;
	TArray<FMD_CarryFrozenContent> CarryFrozenContents;
	TMap<TWeakObjectPtr<AActor>, TEnumAsByte<ECollisionEnabled::Type>> ClientCarryCollisionOverrides;


	friend class AMD_TrashDispenser;
};
