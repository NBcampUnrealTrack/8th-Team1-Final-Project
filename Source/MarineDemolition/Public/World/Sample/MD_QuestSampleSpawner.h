#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_QuestSampleSpawner.generated.h"

class UArrowComponent;
class UMD_MerchantComponent;
class UMD_MerchantSubsystem;
class UMD_SiteDefinition;
class AMD_SampleItem;
struct FMD_MainQuestState;

/**
 * 메인 퀘스트가 Active가 되면 샘플을 스폰한다 (`X-7/01` §2.0).
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

	bool FindLinkedMainQuest(const UMD_MerchantSubsystem* MerchantSubsystem, FMD_MainQuestState& OutQuestState) const;

	void RefreshLinkedQuestPreview();

	bool MatchesLinkedQuest(const FMD_MainQuestState& QuestState) const;

	bool HasOccupyingSamples() const;

	void SpawnSamples(const FMD_MainQuestState& QuestState);

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

	/** 이미 샘플이 있으면 중복 스폰하지 않는 반경(cm). Day 스냅샷 복원 대응. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Quest|Spawn", meta = (ClampMin = "1.0"))
	float OccupiedCheckRadius = 80.f;

	/** 스폰 직후 샘플 보호 시간(초). 손상·파괴 판정만 막고 물리 낙하는 유지. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Quest|Spawn",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "Spawn Protection Duration"))
	float SampleSpawnProtectionDuration = 3.f;

	/** 이번 Active 주기에 스폰을 완료했는지. 계약 재시작(Locked) 시 리셋된다. */
	bool bHasSpawnedForActiveQuest = false;

	bool bSpawnerInitialized = false;

	FDelegateHandle SubsystemChangedHandle;
};
