#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "Types/Tutorial/MD_TutorialTypes.h"
#include "MD_TutorialWallSpawner.generated.h"

class AMD_CuttableWall;
class AMD_TrashSorter;
class AMD_WallPiece;
class AMD_WallUtility;
class UMD_TutorialScriptData;
struct FHitResult;

/** 표시할 안내가 바뀔 때마다. 안내가 없으면 둘 다 빈 FText다 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMD_OnTutorialGuidanceChanged,
	const FText&, Title,
	const FText&, Body);

/** 잘못된 조작에 대한 일회성 경고 (자를 수 없는 자리 등) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMD_OnTutorialNotice,
	const FText&, Message);

/**
 * 격벽 튜토리얼 진행기. 슬롯을 하나씩 열어 벽을 스폰하고, 그 슬롯의 단계를 다 소화하면
 * 다음 슬롯으로 넘어간다.
 *
 * 진행 판정은 전부 기존 시스템의 신호를 구독해서 한다 — 이 액터는 아무것도 새로 판정하지
 * 않는다. 단 하나의 예외가 절단 지점 제한이며, 그건 AMD_CuttableWall::CutLocationFilter에
 * 함수를 꽂아 구현한다.
 *
 * 서버가 진행을 소유하고 클라이언트는 인덱스만 받는다. 안내 문구는 복제하지 않는다 —
 * 레벨 배치 액터라 Slots 값이 양쪽에 동일하게 직렬화되므로 인덱스로 조회하면 된다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_TutorialWallSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMD_TutorialWallSpawner();

	virtual void PostLoad() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 지금 띄워야 할 안내. 진행이 끝났거나 시작 전이면 false를 돌려주고 출력은 비운다 */
	UFUNCTION(BlueprintPure, Category = "MD|Tutorial")
	bool GetCurrentGuidance(FText& OutTitle, FText& OutBody) const;

	/** 화자와 서술 여부까지 필요한 위젯용. 안내가 없으면 false */
	UFUNCTION(BlueprintPure, Category = "MD|Tutorial")
	bool GetCurrentSpeaker(FText& OutSpeaker, bool& bOutIsNarrative) const;

	/** 도입부 서술이 진행 중인지 */
	UFUNCTION(BlueprintPure, Category = "MD|Tutorial")
	bool IsPlayingIntro() const;

	/** 지금 넘기기가 가능하면 안내 문구를 담아 true. 조작 안내 단계에서는 false */
	UFUNCTION(BlueprintPure, Category = "MD|Tutorial")
	bool GetSkipHint(FText& OutHint) const;

	/** 위젯이 붙는 지점. 늦게 만들어진 위젯은 GetCurrentGuidance로 현재 상태를 먼저 읽는다 */
	UPROPERTY(BlueprintAssignable, Category = "MD|Tutorial")
	FMD_OnTutorialGuidanceChanged OnGuidanceChanged;

	UPROPERTY(BlueprintAssignable, Category = "MD|Tutorial")
	FMD_OnTutorialNotice OnNotice;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 도입부 대본. 문구의 유일한 원본이며 여기 없으면 도입부는 흐르지 않는다.
	 *
	 * 액터에 문구를 직접 두지 않는 이유: 그러면 문구가 BP·레벨 인스턴스의 델타로만
	 * 존재해서, 튜토리얼 구조체를 바꾸는 핫리로드 빌드에 델타가 밀리면 통째로 날아간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|도입부")
	TObjectPtr<UMD_TutorialScriptData> ScriptData;

	/** 도입부가 흐르는 동안 이동을 막는다. 시선은 별도 항목이다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|도입부")
	bool bLockMovementDuringIntro = true;

	/** 도입부 동안 시선까지 고정할지. 기본은 풀어둔다 — 주변을 둘러볼 수 있어야 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|도입부")
	bool bLockLookDuringIntro = false;

	/** 서술을 넘기는 키. 생성자에서 스페이스바로 둔다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|도입부")
	FKey SkipIntroKey;

	/** 넘기기 안내 문구. 비우면 힌트를 숨긴다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial|도입부")
	FText SkipHintText;

	/** 순서대로 소화한다. 기본값은 생성자가 채운다 (기획 요청 5스폰) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Tutorial")
	TArray<FMD_TutorialWallSlot> Slots;

	/** Collect·Repair처럼 델리게이트가 없는 단계를 확인하는 주기 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Tutorial",
		meta = (ClampMin = "0.05", Units = "s"))
	float PollInterval = 0.25f;

	/** 같은 경고를 연달아 띄우지 않는 최소 간격. 절단은 매 틱 들어온다 */
	UPROPERTY(EditDefaultsOnly, Category = "MD|Tutorial",
		meta = (ClampMin = "0.0", Units = "s"))
	float NoticeCooldown = 1.0f;

private:
	/** Strip retired cleaning lessons from existing Blueprint/level slot data. */
	void RemoveRetiredCleaningLessons();

	//~ 대본 조회 — 전부 ScriptData 에서 읽는다.
	//  캐시하지 않는 이유: OnRep 이 BeginPlay 보다 먼저 올 수 있어 초기화 순서에 기대면 위험하다.
	const TArray<FMD_TutorialGuideStep>& GetIntroPages() const;
	FText ResolveSkipHint() const;

	/** 장에 화자가 비어 있으면 대본의 기본 화자로 메운다 */
	FText ResolveSpeaker(const FMD_TutorialGuideStep& Step) const;

	/**
	 * 현재 단계의 안내 문구를 대본 에셋에서 찾는다. 없으면 nullptr.
	 * 도입부는 대본이 원본이므로 이 경로를 타지 않는다 — 슬롯 단계만 해당한다.
	 */
	const struct FMD_TutorialGuideText* FindActiveGuideText() const;

	//~ 도입부 (서버)
	/** 서술 한 장을 띄우고 DisplaySeconds 뒤에 다음 장을 예약한다 */
	void EnterIntroPage(int32 PageIndex);
	void AdvanceIntro();

	/**
	 * 넘기기 키의 눌린 순간을 잡는다. WasInputKeyJustPressed 는 한 프레임만 참이라
	 * 타이머로 보면 놓친다 — 눌림 상태를 직접 비교해 엣지를 만든다.
	 * 서버에서만 돈다 (진행 소유자). 원격 클라이언트는 시간이 지나 저절로 넘어간다.
	 */
	void PollIntroSkip();

	//~ 이동 잠금 (각 머신 로컬)
	/**
	 * 도입부 여부에 맞춰 로컬 플레이어의 이동 입력을 잠그거나 푼다.
	 * SetIgnoreMoveInput 은 불리언이 아니라 카운터라서 잠금·해제를 정확히 짝지어야 한다 —
	 * bMovementLockApplied 가 그 짝을 보증한다.
	 */
	void ApplyMovementLock();

	//~ 진행 제어 (서버)
	/** Index부터 앞으로 훑어 활성 슬롯을 연다. 없으면 진행을 끝낸다 */
	void BeginSlotFrom(int32 SearchStart);
	void EnterStep(int32 StepIndex);
	void AdvanceStep();
	void FinishCurrentSlot();
	void FinishAll();

	/** 현재 슬롯의 벽·인프라를 스폰하고 신호를 연결한다. 실패하면 false */
	bool SpawnSlotActors(const FMD_TutorialWallSlot& Slot);
	void ReleaseSlotActors();

	/** 이 슬롯에 벽이 있어야 하는 단계가 하나라도 있는지. 이동 안내 슬롯은 false */
	static bool SlotRequiresWall(const FMD_TutorialWallSlot& Slot);

	/** 벽 BP 가 ChildActorComponent 로 품고 있는 인프라를 ActiveUtilities 에 모은다 */
	void CollectUtilitiesFromWall(AActor* Wall);

	/** 이 슬롯이 세운 인프라를 모두 파괴한다 (수리 완료 시, 그리고 슬롯 종료 시) */
	void DestroyActiveUtilities();

	/** 태그로 박스 존 액터를 찾는다. 못 찾으면 nullptr */
	AActor* FindZoneByTag(FName ZoneTag) const;

	/** 플레이어 폰 중 하나라도 존 안에 있는지 (서버에서 모든 플레이어를 본다) */
	bool IsAnyPlayerInsideZone(const AActor& Zone) const;

	/** 델리게이트가 없는 단계를 위한 틱. 필요한 단계에서만 돌린다 */
	void PollActivePhase();
	void SetPollingEnabled(bool bEnabled);

	//~ 기존 시스템 신호 구독
	UFUNCTION()
	void HandleWallDemolished(AMD_CuttableWall* Wall, AActor* Cutter);

	UFUNCTION()
	void HandleWallScanned(AMD_CuttableWall* Wall, float Duration, AActor* Scanner);

	UFUNCTION()
	void HandleTrashSorted(TSubclassOf<AActor> ItemClass, bool bCorrectSort, FGameplayTag SorterCategory);

	/** AMD_CuttableWall::CutLocationFilter에 꽂는다. 서버에서 절단 틱마다 불린다 */
	bool IsCutAllowedAtHit(const FHitResult* WallHit);

	/** 철거 직후 다음 틱에 이 벽에서 떨어진 조각을 모은다 (OnWallDemolished는 조각 스폰 전이다) */
	void CaptureSpawnedPieces();
	void DestroyCapturedPieces();

	/** 현재 단계가 Phase면 true. 슬롯·단계 범위 검사를 겸한다 */
	bool IsActivePhase(EMD_TutorialWallPhase Phase) const;
	const FMD_TutorialWallSlot* GetActiveSlot() const;
	const FMD_TutorialGuideStep* GetActiveStep() const;

	void BroadcastGuidance() const;
	void ShowNotice(const FText& Message);

	UFUNCTION()
	void OnRep_Progress();

	UFUNCTION()
	void OnRep_Notice();

	/** 진행 전이면 INDEX_NONE, 다 끝나면 Slots.Num() */
	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	int32 CurrentSlotIndex = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_Progress)
	int32 CurrentStepIndex = INDEX_NONE;

	/**
	 * 값이 바뀐 것 자체가 "지금 띄워라" 신호다. 같은 문구를 다시 띄울 수 있어야 하므로
	 * 문구가 아니라 이 카운터에 RepNotify를 건다.
	 * 두 속성이 같은 번치에 실리면 RepNotify 전에 문구가 먼저 적용된다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Notice)
	uint8 NoticeSerial = 0;

	UPROPERTY(Replicated)
	FText PendingNoticeMessage;

	UPROPERTY(Transient)
	TWeakObjectPtr<AMD_CuttableWall> ActiveWall;

	/**
	 * 이 슬롯이 다루는 인프라들. 두 경로에서 채워진다 —
	 * 슬롯의 UtilityClass 로 따로 스폰한 것, 그리고 벽 BP 가 ChildActorComponent 로
	 * 품고 있는 것(BP_MD_CuttableWall_InPipe 는 배관 4개를 품는다).
	 * 배관이 여러 개라 하나로는 부족하다.
	 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMD_WallUtility>> ActiveUtilities;

	/** 현재 ReachZone 단계가 기다리는 존. 단계 진입 때 태그로 한 번 찾아 캐시한다 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ActiveZone;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMD_WallPiece>> CapturedPieces;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMD_TrashSorter>> BoundSorters;

	/** 벽이 사라진 뒤에도 조각을 찾을 수 있게 철거 시점에 기록해 둔다 */
	FVector PieceSearchOrigin = FVector::ZeroVector;
	float PieceSearchRadius = 0.0f;

	FTimerHandle PollTimerHandle;
	FTimerHandle IntroPageTimerHandle;
	FTimerHandle SkipPollTimerHandle;

	/** 직전 폴링 때 넘기기 키가 눌려 있었는지. 누른 채로 여러 장이 넘어가지 않게 한다 */
	bool bSkipKeyWasDown = false;

	/** 폰·컨트롤러가 아직 없을 때 잠금을 다시 시도하는 타이머 */
	FTimerHandle MovementLockRetryHandle;

	/** SetIgnoreMoveInput 카운터를 우리가 몇 번 올렸는지 (0 또는 1) */
	bool bMovementLockApplied = false;

	/** 첫 경고가 쿨다운에 걸리지 않도록 충분히 과거로 초기화한다 */
	double LastNoticeTime = TNumericLimits<double>::Lowest();
};
