#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/MD_SampleFloatSettings.h"
#include "MD_QuestSampleSpawner.generated.h"

class UArrowComponent;
class UMD_QuestDefinition;
class UMD_MerchantComponent;
class UMD_MerchantSubsystem;
class UMD_QuestSampleSpawnerManager;
class AMD_QuestItem;
class AMD_SampleItem;
struct FMD_MainQuestState;
struct FMD_RepeatQuestState;

/**
 * 메인 퀘스트가 Active가 되거나 반복 퀘스트(회수)를 수락하면 샘플을 스폰한다. (`X-7/01` §2.0).
 * 활성 퀘스트 Phase와 같은 SpawnZoneTag의 스포너가 퀘스트 요구 등급을 자동으로 생성한다.
 * B×2처럼 여러 개가 필요하면 스폰 포인트(Arrow)를 추가하거나 액터를 여러 개 둔다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_QuestSampleSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMD_QuestSampleSpawner();

	/** Authored rack sets handle their quest explicitly instead of random selection. */
	virtual bool IsRandomSpawnCandidate() const { return true; }
	virtual bool ClaimsQuestPhase(const FPrimaryAssetId& DefinitionId, int32 PhaseIndex) const { return false; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void InitializeSpawner();

	UFUNCTION()
	void HandleMerchantStateChanged();

	void HandleSubsystemStateChanged();

	virtual void TrySpawnForQuestState();

	/** 연결된 메인 퀘스트의 샘플 생성 처리. */
	void TrySpawnForMainQuestState(const UMD_MerchantSubsystem* MerchantSubsystem);

	/** 수락한 회수 반복 퀘스트의 부족 샘플 생성 처리. */
	void TrySpawnForRepeatQuestStates(const UMD_MerchantSubsystem* MerchantSubsystem);

	/** 현재 월드에 존재하는 같은 요구 클래스·등급·퀘스트 Asset ID의 아이템 수 집계 함수 */
	int32 CountAvailableQuestItems(const FPrimaryAssetId& QuestDefinitionId, const UClass* RequiredItemClass, bool bRequiresSampleGrade, EMD_SampleGrade TargetGrade) const;

	/** 반복 퀘스트 페이즈와 스포너 Zone의 일치 여부 판정 */
	bool MatchesRepeatQuest(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const;

	/** 퀘스트가 지정한 생성 지역과 이 스포너의 배치 지역이 일치하는지 판정 */
	bool MatchesSpawnZone(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const;

	/** 퀘스트 요구 클래스를 우선하고 미지정이면 스포너 기본 클래스를 반환하는 함수 */
	TSubclassOf<AMD_QuestItem> ResolveSpawnItemClass(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const;

	/** 퀘스트 허용 등급에서 실제 생성에 사용할 샘플 등급을 결정하는 함수 */
	bool ResolveSpawnGrade(const UMD_QuestDefinition& Definition, int32 PhaseIndex, EMD_SampleGrade& OutSampleGrade) const;

	bool FindLinkedMainQuest(const UMD_MerchantSubsystem* MerchantSubsystem, FMD_MainQuestState& OutQuestState) const;

	/** 스폰 지점에 있는 요구 클래스·등급의 미지정 아이템을 현재 메인 퀘스트 목표로 연결하고 연결 수를 반환하는 함수 */
	int32 BindOccupyingQuestItemsToMainQuest(const FPrimaryAssetId& QuestDefinitionId, const UClass* RequiredItemClass, bool bRequiresSampleGrade, EMD_SampleGrade TargetGrade, int32 MaxAssignments) const;

	/** 현재 메인 퀘스트에 부족한 목표 아이템만 생성하는 함수 */
	void SpawnMainQuestItems(const FMD_MainQuestState& QuestState, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_QuestItem> SpawnItemClass, bool bRequiresSampleGrade, EMD_SampleGrade SpawnGrade, int32 SpawnCount);

	/** 회수 반복 퀘스트에 필요한 목표 아이템 생성 함수 */
	void SpawnRepeatQuestItems(const FMD_RepeatQuestState& QuestState, const UMD_QuestDefinition& Definition, TSubclassOf<AMD_QuestItem> SpawnItemClass, bool bRequiresSampleGrade, EMD_SampleGrade SpawnGrade, int32 SpawnCount);

	TArray<FTransform> CollectSpawnTransforms() const;

	UPROPERTY(VisibleAnywhere, Category = "MD|Quest|Spawn")
	TObjectPtr<USceneComponent> Root;

	/** 에디터에서 스폰 위치 확인용. 게임에서는 숨긴다. */
	UPROPERTY(VisibleAnywhere, Category = "MD|Quest|Spawn")
	TObjectPtr<UArrowComponent> DefaultSpawnPoint;

	/** 이 스포너가 배치된 Zone 태그이며 퀘스트 페이즈의 SpawnZoneTag와 연결되는 설정 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn")
	FGameplayTag SpawnZoneTag;

	/**
	 * 퀘스트 페이즈에 요구 클래스가 없을 때 사용할 기본 샘플 액터 클래스.
	 *
	 * 기본값은 `BP_QuestSampleSpawner` 블루프린트에서 지정한다. C++에 에셋 경로를 박아두면
	 * 에셋을 옮겼을 때 리다이렉터를 타지 못하고 조용히 끊기므로, 참조는 BP 기본값으로만 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn")
	TSubclassOf<AMD_SampleItem> SampleClass;

	/**
	 * 이 액터가 한 번에 스폰할 샘플 수.
	 * Arrow 자식이 여러 개면 순서대로 사용하고, 부족하면 마지막 포인트를 반복한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn", meta = (ClampMin = "1"))
	int32 SamplesToSpawn = 1;

	/** 스폰 지점의 미지정 샘플을 메인 퀘스트 목표로 연결할 판정 반경(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn", meta = (ClampMin = "1.0"))
	float OccupiedCheckRadius = 80.f;

	/** 스폰 직후 샘플 보호 시간(초). 손상·파괴 판정만 막고 물리 낙하는 유지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest|Spawn",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Spawn Protection Duration"))
	float SampleSpawnProtectionDuration = 3.f;

	/** When enabled, apply this water plane and bobbing to spawned samples. Disabled preserves sample BP defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest|Water Float", meta = (ShowOnlyInnerProperties))
	FMD_SampleFloatSettings WaterFloat;

	/** 메인 역할 퀘스트에서 마지막으로 스폰을 완료한 페이즈 인덱스 */
	int32 LastSpawnedMainPhaseIndex = INDEX_NONE;

	/** 같은 Phase 인덱스를 사용하는 다른 메인 퀘스트를 구분하기 위한 마지막 Definition ID */
	FPrimaryAssetId LastSpawnedMainQuestDefinitionId;

	bool bSpawnerInitialized = false;

	FDelegateHandle SubsystemChangedHandle;

	// 월드 매니저가 Zone 일치 여부를 사용해 무작위 스포너 후보를 구성할 수 있도록 허용한다.
	friend class UMD_QuestSampleSpawnerManager;
};
