#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncProtocol.h"
#include "MD_ScrapeMeshUVSyncSubsystem.generated.h"

struct MARINEDEMOLITION_API FMD_ScrapeMeshUVJournal
{
    FGuid Epoch = FGuid::NewGuid();
    FGuid LayoutId;
    FIntPoint Grid = FIntPoint::ZeroValue;
    uint64 Serial = 1;
    // Checkpoints below this floor cannot reconstruct an incremental snapshot.
    uint64 RetainedAfterSerial = 1;
    TMap<int32, uint64> ChangedCells;
    bool NeedsFullSnapshot(const FGuid& KnownEpoch, uint64 KnownSerial) const;
    void DiscardHistory();
    void RecordCell(int32 CellIndex, uint64 MaximumBytes);
};
struct FMD_ScrapeMeshUVLiveHistory
{
    TWeakObjectPtr<UMD_ScrapeSurfaceComponent> Surface;
    FGuid LayoutId;
    // Only active cells, four bytes each; current values already live on the surface.
    TArray<uint32> Revisions;
};

/** Bounded disposable history; the surface remains the authoritative source of truth. */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeMeshUVSyncSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    void InvalidateSurface(UMD_ScrapeSurfaceComponent& Surface);
    void RecordSurfaceChanges(UMD_ScrapeSurfaceComponent& Surface, TConstArrayView<FMD_ScrapeCellChange> Changes, bool bBoundaryChanged=false);
    bool CaptureSnapshot(UMD_ScrapeSurfaceComponent& Surface, const FGuid& KnownEpoch,
        uint64 KnownSerial, FMD_ScrapeMeshUVSnapshotDescriptor& OutDescriptor,
        TArray<FMD_ReplicatedScrapeCellState>& OutStates);
    void RecordClientVisualDelta(const FMD_ScrapeAuthoritativeVisualDelta& Delta);
    bool BeginClientSnapshot(UMD_ScrapeSurfaceComponent& Surface, const UObject* Owner = nullptr);
    void EndClientSnapshot(UMD_ScrapeSurfaceComponent& Surface, const UObject* Owner = nullptr);
    bool MergeClientVisualStates(UMD_ScrapeSurfaceComponent& Surface, uint32 SnapshotRevision,
        TArray<FMD_ReplicatedScrapeCellState>& States, const UObject* Owner = nullptr);
    bool TryReserveTransfer(const UObject* Owner, uint64 Bytes);
    void ReleaseTransfer(const UObject* Owner);
    uint64 GetJournalAllocatedBytes() const;
    uint64 GetClientHistoryAllocatedBytes() const;
    uint64 GetTransferReservedBytes() const;
    static constexpr uint64 MaximumSurfaceJournalBytes = 256 * 1024;
    static constexpr uint64 MaximumWorldJournalBytes = 4 * 1024 * 1024;
    static constexpr uint64 MaximumSurfaceLiveHistoryBytes = 4 * 1024 * 1024;
    static constexpr uint64 MaximumWorldLiveHistoryBytes = 8 * 1024 * 1024;
    // A maximum wall now reserves 54.125 MiB including bounded SDF codec scratch.
    // The shared 64 MiB gate still permits only one such capture at a time.
    static constexpr uint64 MaximumConnectionTransferBytes = 56 * 1024 * 1024;
    static constexpr uint64 MaximumWorldTransferBytes = 64 * 1024 * 1024;
private:
    void PruneExpiredOwners();
    FMD_ScrapeMeshUVJournal& FindJournal(UMD_ScrapeSurfaceComponent& Surface);
    float ScanAccumulator = 1.0f;
    TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, FMD_ScrapeMeshUVJournal> Journals;
    TMap<TWeakObjectPtr<const UObject>, FMD_ScrapeMeshUVLiveHistory> ClientLiveStates;
    TMap<TWeakObjectPtr<const UObject>, uint64> TransferReservations;
};
