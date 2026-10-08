#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/Trash/MD_TrashSpawnState.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_TrashSpawnManager.generated.h"

class AMD_TrashSpawnVolume;
class AMD_TrashFloorGenerator;
class AMD_TrashItem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnTotalTrashCountChanged, int32, RemainingCount);

/** Server-side collection of the map's procedural trash generators. */
UCLASS(Config = Game)
class MARINEDEMOLITION_API UMD_TrashSpawnManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "MD|Trash Spawn")
	FMD_OnTotalTrashCountChanged OnTotalTrashCountChanged;

	void NotifyGeneratorStateChanged();
    /** Shared spacing between base and bonus populations, only during spawn/load planning. */
    void AppendOccupiedTrash(const AMD_TrashSpawnVolume* Requester, TArray<FVector>& Positions,
        TArray<float>& Radii, float& MinimumSpacing) const;
	void RegisterGenerator(AMD_TrashSpawnVolume* Generator);
	void UnregisterGenerator(AMD_TrashSpawnVolume* Generator);
	/** 월드 초기화가 매 틱 호출; false는 실패, !ready는 준비 대기 */
	bool PrepareGenerators(bool& bReady);
	void RequestInitialization();

	/** 준비가 끝난 뒤 아직 초기화되지 않은 생성기에 첫 배치를 실행 */
	bool InitializeUnrestoredGenerators();

	/** AutoFloorMaps 맵에서 배치된 쓰레기를 바닥 생성기로 전환하기 위한 원본 위치 수집 */
	bool EnableAuthoredFloorTransition();

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 GetGeneratorCount() const;

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	TArray<AMD_TrashSpawnVolume*> GetGenerators() const;

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 GetRemainingTrashCount() const;

	// 마지막 비율 분배에서 계산한 맵 전체 목표 개수. 분배 전이면 0
	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 GetDistributedTrashTotal() const { return DistributedTrashTotal; }

	// 마지막 비율 분배에 쓴 인원수
	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 GetDistributedPlayerCount() const { return DistributedPlayerCount; }

	// 마지막 비율 분배에 쓴 계약 난이도
	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	EMD_Difficulty GetDistributedDifficulty() const { return DistributedDifficulty; }

private:
	// 기준 개수 × 인원 배율 × 난이도 배율. 인원은 관전자가 아닌 현재 PlayerState 수
	int32 ResolveTargetTrashTotal(int32& OutPlayerCount, EMD_Difficulty& OutDifficulty) const;
	// SpawnShare > 0인 미초기화 생성기에 총량을 가중치 비율로 나눠 SpawnCount를 덮어쓴다. 바닥 셀 후보 수를 상한으로 두고 넘치는 양은 다른 볼륨으로 넘긴다
	void DistributeSpawnShares();
	int32 DistributedTrashTotal = 0;
	int32 DistributedPlayerCount = 0;
	EMD_Difficulty DistributedDifficulty = EMD_Difficulty::Standard;

	UPROPERTY(Config)
	TArray<FName> AutoFloorMaps;
	TWeakObjectPtr<AMD_TrashFloorGenerator> TransitionGenerator;
	TMap<FName, FVector> AuthoredPositions;
	bool ConvertAuthoredTrash();
	void FlushCountChanged();
	void InitializeWhenReady();

	TArray<TWeakObjectPtr<AMD_TrashSpawnVolume>> Generators;
	bool bWorldInitialized = false;
	int32 CachedRemainingCount = 0;
	bool bCountBroadcastPending = false;
	bool bInitializationScheduled = false;
};
