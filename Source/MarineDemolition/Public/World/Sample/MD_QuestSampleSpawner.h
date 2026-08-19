#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFramework/Actor.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_QuestSampleSpawner.generated.h"

class UArrowComponent;
class UMD_QuestDefinition;
class UMD_MerchantComponent;
class UMD_MerchantSubsystem;
class UMD_SiteDefinition;
class AMD_SampleItem;
struct FMD_MainQuestState;
struct FMD_RepeatQuestState;

/**
 * 메인 퀘스트가 Active가 되거나 반복 퀘스트(회수)를 수락하면 샘플을 스폰한다. (`X-7/01` §2.0).
 * `SourceQuest`에 메인 퀘스트 DA를 지정하고, 스폰 설정은 `MD|Quest|Spawn`에 둔다.
 * B×2처럼 여러 개가 필요하면 스폰 포인트(Arrow)를 추가하거나 액터를 여러 개 둔다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_QuestSampleSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMD_QuestSampleSpawner();

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void InitializeSpawner();

	UFUNCTION()
	void HandleMerchantStateChanged();

	void HandleSubsystemStateChanged();

	void TrySpawnForQuestState();

	/** 연결된 메인 퀘스트의 샘플 생성 처리. */
	void TrySpawnForMainQuestState(const UMD_MerchantSubsystem* MerchantSubsystem);

	/** 수락한 회수 반복 퀘스트의 부족 샘플 생성 처리. */
	void TrySpawnForRepeatQuestStates(const UMD_MerchantSubsystem* MerchantSubsystem);

	/** 같은 등급 스포너가 여러 개일 때 반복 퀘스트를 담당할 대표 스포너 판정. */
	bool IsPrimaryRepeatQuestSpawner() const;

	/** 현재 월드에 존재하는 같은 등급과 퀘스트 Asset ID의 샘플 수 집계 */
	int32 CountAvailableSamplesForQuest(const FPrimaryAssetId& QuestDefinitionId) const;

	/** 반복 퀘스트 페이즈와 스포너 등급의 일치 여부 판정 */
	bool MatchesRepeatQuest(const UMD_QuestDefinition& Definition, int32 PhaseIndex) const;

	bool FindLinkedMainQuest(const UMD_MerchantSubsystem* MerchantSubsystem, FMD_MainQuestState& OutQuestState) const;

	void RefreshLinkedQuestPreview();

	/** 스폰 지점에 있는 미지정 샘플을 현재 메인 퀘스트 목표로 연결하고 연결 수를 반환하는 함수 */
	int32 BindOccupyingSamplesToMainQuest(const FPrimaryAssetId& QuestDefinitionId, int32 MaxAssignments) const;

	/** 현재 메인 퀘스트에 부족한 목표 샘플만 생성하는 함수 */
	void SpawnSamples(const FMD_MainQuestState& QuestState, const UMD_QuestDefinition& Definition, int32 SpawnCount);

	/** 회수 반복 퀘스트에 필요한 샘플 생성 처리 */
	void SpawnRepeatQuestSamples(const FMD_RepeatQuestState& QuestState, int32 SpawnCount);

	TArray<FTransform> CollectSpawnTransforms() const;

	UPROPERTY(VisibleAnywhere, Category = "MD|Quest|Spawn")
	TObjectPtr<USceneComponent> Root;

	/** 에디터에서 스폰 위치 확인용. 게임에서는 숨긴다. */
	UPROPERTY(VisibleAnywhere, Category = "MD|Quest|Spawn")
	TObjectPtr<UArrowComponent> DefaultSpawnPoint;

	/** Site의 MainQuestLine 인덱스 (0=Day1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest", meta = (ClampMin = "0"))
	int32 MainQuestIndex = 0;

	/** LinkedQuestName 미리보기용 Site DA. 기본 X-7. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest")
	TSoftObjectPtr<UMD_SiteDefinition> QuestCatalogSite;

	/** MainQuestLine[MainQuestIndex] 표시 이름. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Quest",
		meta = (EditCondition = "MainQuestIndex >= 0", EditConditionHides))
	FText LinkedQuestName;

	/** MainQuestIndex 와 동기화되는 Day. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn", meta = (ClampMin = "1"))
	int32 UnlockDay = 1;

	/** 스폰할 샘플 등급. 연결된 메인퀘 TargetGradeTag와 일치해야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn")
	EMD_SampleGrade SampleGrade = EMD_SampleGrade::C;

	/**
	 * 스폰할 샘플 액터 클래스.
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

	/** 메인 역할 퀘스트에서 마지막으로 스폰을 완료한 페이즈 인덱스 */
	int32 LastSpawnedMainPhaseIndex = INDEX_NONE;

	bool bSpawnerInitialized = false;

	FDelegateHandle SubsystemChangedHandle;
};
