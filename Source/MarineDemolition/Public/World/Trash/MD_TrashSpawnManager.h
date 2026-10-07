#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/Trash/MD_TrashSpawnState.h"
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

private:
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
