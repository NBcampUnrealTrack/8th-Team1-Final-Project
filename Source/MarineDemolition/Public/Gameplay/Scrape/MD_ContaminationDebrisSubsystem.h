#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Types/Scrape/MD_ContaminationPendingDrop.h"
#include "MD_ContaminationDebrisSubsystem.generated.h"

class AMD_ContaminationItem;

/** Authority-only bounded physical realization; every unspawned unit stays in the durable ledger. */
UCLASS()
class MARINEDEMOLITION_API UMD_ContaminationDebrisSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual bool IsTickable() const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    bool EnqueuePendingDrop(const FMD_ContaminationPendingDrop& Incoming, int32& OutSpawnedCount);
    void UpdateSourceConfiguration(const FMD_ContaminationPendingDrop& Configuration);
    void RegisterItem(AMD_ContaminationItem* Item);
    void UnregisterItem(AMD_ContaminationItem* Item);
    void CapturePendingDrops(TArray<FMD_ContaminationPendingDrop>& Out) const;
    bool RestorePendingDrops(TConstArrayView<FMD_ContaminationPendingDrop> Saved);
    uint64 GetAllocatedSize() const;

private:
    UPROPERTY() TArray<FMD_ContaminationPendingDrop> PendingDrops;
    TSet<TWeakObjectPtr<AMD_ContaminationItem>> ActiveItems;
    TMap<FGuid, double> RetryAfterSeconds;
    uint64 BudgetFrame = MAX_uint64;
    uint64 NextEnqueueOrder = 1;
    int32 SpawnAttemptsThisFrame = 0;
    int32 ReservedSlots = 0;
    FGuid SpawningBatchId; // Deferred actor-spawn callbacks may append other queue rows.
    bool bDraining = false;
    bool bRestoring = false;
    int32 DrainPendingDrops(uint64 FrameId, const FGuid& CountBatch = FGuid());
    void MergeResidualBatch(const FGuid& BatchId);
    void TraceMemory() const;
};
