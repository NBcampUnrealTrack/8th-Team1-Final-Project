#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncProtocol.h"
#include "MD_ScrapeMeshUVSyncSubsystem.generated.h"

struct FMD_ScrapeMeshUVJournalEntry
{
    uint64 Serial = 0;
    FMD_ReplicatedScrapeCellState State;
};
struct FMD_ScrapeMeshUVJournal
{
    FGuid Epoch = FGuid::NewGuid();
    FGuid LayoutId;
    FIntPoint Grid = FIntPoint::ZeroValue;
    uint64 Serial = 1;
    TMap<int32, FMD_ScrapeMeshUVJournalEntry> ChangedCells;
};
struct FMD_ScrapeMeshUVLiveState
{
    FGuid LayoutId;
    uint32 VisualRevision = 0;
    FMD_ReplicatedScrapeCellState State;
};

/** Owns shared cell journals; transport state is private to each connection. */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeMeshUVSyncSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    void InvalidateSurface(UMD_ScrapeSurfaceComponent& Surface);
    void RecordSurfaceChanges(UMD_ScrapeSurfaceComponent& Surface, TConstArrayView<FMD_ScrapeCellChange> Changes);
    bool CaptureSnapshot(UMD_ScrapeSurfaceComponent& Surface, const FGuid& KnownEpoch,
        uint64 KnownSerial, FMD_ScrapeMeshUVSnapshotDescriptor& OutDescriptor,
        TArray<FMD_ReplicatedScrapeCellState>& OutStates);
    void RecordClientVisualDelta(const FMD_ScrapeAuthoritativeVisualDelta& Delta);
    void MergeClientVisualStates(UMD_ScrapeSurfaceComponent& Surface, uint32 SnapshotRevision,
        TArray<FMD_ReplicatedScrapeCellState>& States);
private:
    FMD_ScrapeMeshUVJournal& FindJournal(UMD_ScrapeSurfaceComponent& Surface);
    float ScanAccumulator = 1.0f;
    TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, FMD_ScrapeMeshUVJournal> Journals;
    TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, TMap<int32, FMD_ScrapeMeshUVLiveState>> ClientLiveStates;
};
