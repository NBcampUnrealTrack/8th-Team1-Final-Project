#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/Trash/MD_TrashSpawnState.h"
#include "MD_TrashSpawnManager.generated.h"

class AMD_TrashSpawnVolume;
class AMD_TrashFloorGenerator;
class AMD_TrashItem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnTotalTrashCountChanged, int32, RemainingCount);

/** Server-side collection and save boundary for the map's procedural trash generators. */
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
	/** Loading pumps this before applying file/day saves; false means failed, !ready means pending. */
	bool PrepareGenerators(bool& bReady);
	void RequestInitialization();

	/** Called after file and day state have been restored, before the initial auto-save. */
	bool InitializeUnrestoredGenerators();

	bool CaptureStates(TArray<FMD_TrashGeneratorState>& OutStates) const;
	bool CanRestoreStates(const TArray<FMD_TrashGeneratorState>& States) const;
	/** Reserve only obsolete authored trash while the actor snapshot is applied. */
	bool PrepareRestoreStates(const TArray<FMD_TrashGeneratorState>& States);
	bool ReserveAuthoredTrashForRestore(AMD_TrashItem* Item);
	void CancelPreparedRestore() { ReservedTrash.Reset(); bReserveAuthoredTrash = false; }
    bool CapturePlacements(TMap<FName, FMD_TrashGeneratorPlacement>& Out) const;
    bool CanRestorePlacements(const TArray<FMD_TrashGeneratorState>& States,
        const TMap<FName, FMD_TrashGeneratorPlacement>& Placements) const;
    bool RestoreStates(const TArray<FMD_TrashGeneratorState>& States,
        const TMap<FName, FMD_TrashGeneratorPlacement>* DayPlacements = nullptr);
	/** Called before save restoration; captures immutable authored identity and source positions. */
	bool EnableAuthoredFloorTransition();
	bool IsTransitionGenerator(const AMD_TrashSpawnVolume* Generator) const;

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
	bool bMigrateRestoredActors = false;
	bool bReserveAuthoredTrash = false;
	TArray<TWeakObjectPtr<AMD_TrashItem>> ReservedTrash;
	bool NormalizeStates(const TArray<FMD_TrashGeneratorState>& Input, TArray<FMD_TrashGeneratorState>& Out) const;
	bool ConvertAuthoredTrash(bool bPreserveCells);
	void FlushCountChanged();
	void InitializeWhenReady();

	TArray<TWeakObjectPtr<AMD_TrashSpawnVolume>> Generators;
	bool bWorldInitialized = false;
	int32 CachedRemainingCount = 0;
	bool bCountBroadcastPending = false;
	bool bInitializationScheduled = false;
};
