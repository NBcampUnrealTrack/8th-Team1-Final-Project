#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Component/Scrape/MD_ScrapeMeshUVSyncComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FMD_ScrapeMeshUVJournal::NeedsFullSnapshot(const FGuid& KnownEpoch, const uint64 KnownSerial) const
{
    return KnownEpoch != Epoch || KnownSerial == 0 || KnownSerial > Serial || KnownSerial < RetainedAfterSerial;
}

void FMD_ScrapeMeshUVJournal::DiscardHistory()
{
    RetainedAfterSerial = Serial;
    ChangedCells.Empty();
}

void FMD_ScrapeMeshUVJournal::RecordCell(const int32 CellIndex, const uint64 MaximumBytes)
{
    ChangedCells.FindOrAdd(CellIndex) = Serial;
    // Discard only reconstructible history, never source state. The floor also covers
    // a partially recorded batch, so a checkpoint cannot mistake it for a complete delta.
    if (ChangedCells.GetAllocatedSize() > MaximumBytes) { DiscardHistory(); }
}

bool UMD_ScrapeMeshUVSyncSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

TStatId UMD_ScrapeMeshUVSyncSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMD_ScrapeMeshUVSyncSubsystem, STATGROUP_Tickables);
}

void UMD_ScrapeMeshUVSyncSubsystem::PruneExpiredOwners()
{
    for (auto It = Journals.CreateIterator(); It; ++It)
    { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
    for (auto It = ClientLiveStates.CreateIterator(); It; ++It)
    { if (!It.Key().IsValid() || !It.Value().Surface.IsValid()) { It.RemoveCurrent(); } }
    for (auto It = TransferReservations.CreateIterator(); It; ++It)
    { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
    if (Journals.IsEmpty()) { Journals.Empty(); }
    if (ClientLiveStates.IsEmpty()) { ClientLiveStates.Empty(); }
    if (TransferReservations.IsEmpty()) { TransferReservations.Empty(); }
}

bool UMD_ScrapeMeshUVSyncSubsystem::QueuePredictionRebuild(UMD_ScrapeSurfaceComponent& Surface)
{
    if (Surface.GetWorld() != GetWorld() || !Surface.IsSurfaceInitialized() || !Surface.UsesMeshUVSurface()) return false;
    if (!PendingPredictionRebuilds.Contains(&Surface))
    {
        if (PendingPredictionRebuilds.Num() == 32) return false;
        PendingPredictionRebuilds.Add(&Surface);
    }
    Surface.bLocalScrapePredictionRebuildPending = true;
    return true;
}

void UMD_ScrapeMeshUVSyncSubsystem::Deinitialize()
{
    for (auto WeakSurface : PendingPredictionRebuilds)
        if (auto* Surface = WeakSurface.Get()) Surface->bLocalScrapePredictionRebuildPending = false;
    PendingPredictionRebuilds.Reset();
    Super::Deinitialize();
}

void UMD_ScrapeMeshUVSyncSubsystem::Tick(const float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World) { return; }
    // UWorld ticks world subsystems after actor/network work, before viewport drawing.
    // New local input can flush its own surface earlier; the queued entry then does no work.
    auto Pending = MoveTemp(PendingPredictionRebuilds);
    PendingPredictionRebuilds.Reset();
    for (auto WeakSurface : Pending)
        if (auto* Surface = WeakSurface.Get()) Surface->FlushLocalScrapePredictionRebuild();
    ScanAccumulator += DeltaTime;
    if (ScanAccumulator < 1.0f) { return; }
    ScanAccumulator = 0.0f;
    PruneExpiredOwners();
    if (World->GetNetMode() == NM_Client || World->GetNetMode() == NM_Standalone) { return; }
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC) || PC->IsLocalController() || !PC->GetNetConnection()
            || PC->FindComponentByClass<UMD_ScrapeMeshUVSyncComponent>()) { continue; }
        UMD_ScrapeMeshUVSyncComponent* Sync = NewObject<UMD_ScrapeMeshUVSyncComponent>(PC);
        PC->AddInstanceComponent(Sync);
        Sync->RegisterComponent();
        PC->ForceNetUpdate();
    }
}

uint64 UMD_ScrapeMeshUVSyncSubsystem::GetJournalAllocatedBytes() const
{
    uint64 Bytes = Journals.GetAllocatedSize();
    for (const auto& Pair : Journals) { Bytes += Pair.Value.ChangedCells.GetAllocatedSize(); }
    return Bytes;
}

uint64 UMD_ScrapeMeshUVSyncSubsystem::GetClientHistoryAllocatedBytes() const
{
    uint64 Bytes = ClientLiveStates.GetAllocatedSize();
    for (const auto& Pair : ClientLiveStates) { Bytes += Pair.Value.Revisions.GetAllocatedSize(); }
    return Bytes;
}

uint64 UMD_ScrapeMeshUVSyncSubsystem::GetTransferReservedBytes() const
{
    uint64 Bytes = 0;
    for (const auto& Pair : TransferReservations) { if (Pair.Key.IsValid()) { Bytes += Pair.Value; } }
    return Bytes;
}

bool UMD_ScrapeMeshUVSyncSubsystem::TryReserveTransfer(const UObject* Owner, const uint64 Bytes)
{
    if (!IsValid(Owner) || Bytes > MaximumConnectionTransferBytes) { return false; }
    PruneExpiredOwners();
    const uint64 Existing = TransferReservations.FindRef(Owner);
    if (GetTransferReservedBytes() - Existing > MaximumWorldTransferBytes - Bytes) { return false; }
    TransferReservations.FindOrAdd(Owner) = Bytes;
    return true;
}

void UMD_ScrapeMeshUVSyncSubsystem::ReleaseTransfer(const UObject* Owner)
{
    TransferReservations.Remove(Owner);
    if (TransferReservations.IsEmpty()) { TransferReservations.Empty(); }
}

FMD_ScrapeMeshUVJournal& UMD_ScrapeMeshUVSyncSubsystem::FindJournal(UMD_ScrapeSurfaceComponent& Surface)
{
    if (!Journals.Contains(&Surface)
        && GetJournalAllocatedBytes() + sizeof(FMD_ScrapeMeshUVJournal) + 256 > MaximumWorldJournalBytes)
    {
        // Even metadata is bounded across arbitrarily many transient surfaces.
        // Removing an epoch guarantees old checkpoints fall back to full state.
        Journals.Empty();
    }
    FMD_ScrapeMeshUVJournal* Entry = &Journals.FindOrAdd(&Surface);
    // A hash-table capacity jump can exceed the estimate even for a capture-only
    // surface. Bound actual metadata allocation here, not only after recording edits.
    if (GetJournalAllocatedBytes() > MaximumWorldJournalBytes)
    {
        Journals.Empty();
        Entry = &Journals.FindOrAdd(&Surface);
    }
    FMD_ScrapeMeshUVJournal& Journal = *Entry;
    if (Journal.LayoutId != Surface.GetContaminationLayoutId()
        || Journal.Grid != Surface.GetContaminationGridDimensions())
    {
        Journal = FMD_ScrapeMeshUVJournal();
        Journal.LayoutId = Surface.GetContaminationLayoutId();
        Journal.Grid = Surface.GetContaminationGridDimensions();
    }
    return Journal;
}

void UMD_ScrapeMeshUVSyncSubsystem::InvalidateSurface(UMD_ScrapeSurfaceComponent& Surface)
{
    if (!Surface.HasAuthority() || !Surface.UsesMeshUVSurface()) { return; }
    const bool bRetainJournal = GetWorld()->GetNetMode() != NM_Standalone || Journals.Contains(&Surface);
    Journals.Remove(&Surface);
    if (bRetainJournal) { FindJournal(Surface); }
    Surface.AdvanceContaminationVisualRevision();
    if (AActor* Owner = Surface.GetOwner()) { Owner->ForceNetUpdate(); }
}

void UMD_ScrapeMeshUVSyncSubsystem::RecordSurfaceChanges(
    UMD_ScrapeSurfaceComponent& Surface, const TConstArrayView<FMD_ScrapeCellChange> Changes, bool bBoundaryChanged)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_RecordBoundedJournal);
    if (!Surface.HasAuthority() || !Surface.UsesMeshUVSurface() || (Changes.IsEmpty() && !bBoundaryChanged)
        || (GetWorld()->GetNetMode() == NM_Standalone && !Journals.Contains(&Surface))) { return; }
    FMD_ScrapeMeshUVJournal& Journal = FindJournal(Surface);
    if (Journal.Serial == MAX_uint64)
    {
        Journal.Epoch = FGuid::NewGuid(); Journal.Serial = 1; Journal.DiscardHistory();
    }
    ++Journal.Serial;
    for (const FMD_ScrapeCellChange& Change : Changes)
    {
        if (Surface.IsContaminationCellActive(Change.CellIndex))
        { Journal.RecordCell(Change.CellIndex, MaximumSurfaceJournalBytes); }
    }
    uint64 Bytes = GetJournalAllocatedBytes();
    // Prefer keeping the just-written surface's recent history. Other histories
    // are disposable and their checkpoints automatically fall back to full.
    for (auto& Pair : Journals)
    {
        if (Bytes <= MaximumWorldJournalBytes) { break; }
        if (Pair.Key.Get() == &Surface) { continue; }
        Bytes -= Pair.Value.ChangedCells.GetAllocatedSize();
        Pair.Value.DiscardHistory();
    }
    if (Bytes > MaximumWorldJournalBytes) { Journal.DiscardHistory(); }
    if (GetJournalAllocatedBytes() > MaximumWorldJournalBytes) { Journals.Empty(); }
}

bool UMD_ScrapeMeshUVSyncSubsystem::CaptureSnapshot(
    UMD_ScrapeSurfaceComponent& Surface, const FGuid& KnownEpoch, const uint64 KnownSerial,
    FMD_ScrapeMeshUVSnapshotDescriptor& OutDescriptor, TArray<FMD_ReplicatedScrapeCellState>& OutStates)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_CaptureBoundedSnapshot);
    OutStates.Reset();
    OutDescriptor = FMD_ScrapeMeshUVSnapshotDescriptor();
    if (!Surface.HasAuthority() || !Surface.UsesMeshUVSurface() || !Surface.IsSurfaceInitialized()
        || !Surface.AreContaminationCarrierWeightsCurrent()
        || Surface.GetContaminationActiveCellCount() > FMD_ScrapeMeshUVSyncProtocol::MaximumSnapshotStates) { return false; }
    FMD_ScrapeMeshUVJournal& Journal = FindJournal(Surface);
    OutDescriptor.LayoutId = Journal.LayoutId; OutDescriptor.Grid = Journal.Grid;
    OutDescriptor.CarrierScale = Surface.GetContaminationCarrierWeightScale();
    OutDescriptor.Generation = Surface.GetContaminationGeneration();
    OutDescriptor.Epoch = Journal.Epoch; OutDescriptor.Serial = Journal.Serial;
    OutDescriptor.VisualRevision = Surface.ReplicatedContaminationVisualRevision;
    OutDescriptor.bFullSnapshot = Journal.NeedsFullSnapshot(KnownEpoch, KnownSerial);
    const auto AppendCurrentCell = [&](const int32 Index)
    {
        FMD_ReplicatedScrapeCellState State;
        bool bGenerated = false;
        if (!Surface.TryBuildReplicatedCellState(Index, State, bGenerated)) { return false; }
        OutStates.Add(State);
        return true;
    };
    if (OutDescriptor.bFullSnapshot)
    {
        OutStates.Reserve(Surface.GetContaminationActiveCellCount());
        for (int32 Index = 0; Index < Surface.GetContaminationAtlasCellCount(); ++Index)
        {
            if (Surface.IsContaminationCellActive(Index) && !AppendCurrentCell(Index)) { return false; }
        }
    }
    else
    {
        OutStates.Reserve(Journal.ChangedCells.Num());
        for (const auto& Pair : Journal.ChangedCells)
        { if (Pair.Value > KnownSerial && !AppendCurrentCell(Pair.Key)) { return false; } }
        OutStates.Sort([](const FMD_ReplicatedScrapeCellState& A, const FMD_ReplicatedScrapeCellState& B)
            { return A.EncodedCellIndex < B.EncodedCellIndex; });
    }
    OutDescriptor.TotalStates = OutStates.Num();
    return true;
}

bool UMD_ScrapeMeshUVSyncSubsystem::BuildGeneratedCell(const UMD_ScrapeSurfaceComponent& Surface,
    const int32 Index, FMD_ReplicatedScrapeCellState& State) const
{
    const int32 Storage = Surface.GetContaminationStorageIndex(Index);
    if (!Surface.IsContaminationCellActive(Index) || !Surface.HasGeneratedInitialContamination(Storage)) return false;
    const float Initial = FMath::Clamp(Surface.GetGeneratedInitialContamination(Storage), 0.0f, 1.0f);
    State = FMD_ReplicatedScrapeCellState(Index,
        FMD_ScrapeReplicationCodec::QuantizeRemaining(Initial, 0.0f),
        FMD_ScrapeReplicationCodec::QuantizeRemaining(Initial, Surface.ScrapeSettings.CleanTolerance));
    return true;
}

bool UMD_ScrapeMeshUVSyncSubsystem::CompactGeneratedSnapshot(UMD_ScrapeSurfaceComponent& Surface,
    FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor, TArray<FMD_ReplicatedScrapeCellState>& States)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_CompactGeneratedSnapshot);
    if (!Descriptor.bFullSnapshot || !Descriptor.Generation.IsValid()
        || Descriptor.Generation != Surface.GetContaminationGeneration()
        || States.Num() != Surface.GetContaminationActiveCellCount()) return false;
    uint32 BaseHash = 2166136261u, FullHash = 2166136261u;
    const auto IsException = [](const FMD_ReplicatedScrapeCellState& State, const FMD_ReplicatedScrapeCellState& Base)
    {
        return State.QuantizedInitialContamination != Base.QuantizedInitialContamination
            || State.QuantizedRemainingContamination != Base.QuantizedRemainingContamination
            || State.QuantizedTransferredContamination != 0 || State.QuantizedTransferredPatternDensity != 0
            || State.QuantizedCoveredFraction != 0;
    };
    int32 Exceptions = 0;
    // Validate before compacting, so failure leaves the original dense snapshot usable.
    int32 Read = 0;
    for (int32 Index = 0; Index < Surface.GetContaminationAtlasCellCount(); ++Index)
    {
        if (!Surface.IsContaminationCellActive(Index)) continue;
        FMD_ReplicatedScrapeCellState Base;
        if (!States.IsValidIndex(Read) || States[Read].GetCellIndex() != Index || !BuildGeneratedCell(Surface, Index, Base)) return false;
        BaseHash = FMD_ScrapeMeshUVSyncProtocol::AccumulateStateHash(BaseHash, Base);
        Exceptions += IsException(States[Read], Base) ? 1 : 0;
        FullHash = FMD_ScrapeMeshUVSyncProtocol::AccumulateStateHash(FullHash, States[Read++]);
    }
    // Keep expansion/reallocation inside the existing full-snapshot memory reservation.
    // Dense changes already compress efficiently with the original byte-plane codec.
    if (Exceptions > States.Num() / 2) return false;
    int32 Write = 0;
    for (const FMD_ReplicatedScrapeCellState& State : States)
    {
        FMD_ReplicatedScrapeCellState Base;
        BuildGeneratedCell(Surface, State.GetCellIndex(), Base);
        if (IsException(State, Base)) States[Write++] = State;
    }
    States.SetNum(Write, EAllowShrinking::No);
    Descriptor.TotalStates = Write; Descriptor.bGeneratedBase = true;
    Descriptor.GeneratedBaseHash = BaseHash; Descriptor.FullStateHash = FullHash;
    return true;
}

bool UMD_ScrapeMeshUVSyncSubsystem::ExpandGeneratedSnapshot(UMD_ScrapeSurfaceComponent& Surface,
    const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor, TArray<FMD_ReplicatedScrapeCellState>& States)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_ExpandGeneratedSnapshot);
    if (!Descriptor.bGeneratedBase) return true;
    if (!Descriptor.bFullSnapshot || Descriptor.Generation != Surface.GetContaminationGeneration()
        || Descriptor.LayoutId != Surface.GetContaminationLayoutId()
        || States.Num() > Surface.GetContaminationActiveCellCount() / 2) return false;
    int32 Previous = INDEX_NONE;
    for (const auto& State : States)
    {
        if (State.GetCellIndex() <= Previous || !Surface.IsContaminationCellActive(State.GetCellIndex())) return false;
        Previous = State.GetCellIndex();
    }
    uint32 BaseHash = 2166136261u;
    for (int32 Index = 0; Index < Surface.GetContaminationAtlasCellCount(); ++Index)
    {
        if (!Surface.IsContaminationCellActive(Index)) continue;
        FMD_ReplicatedScrapeCellState Base;
        if (!BuildGeneratedCell(Surface, Index, Base)) return false;
        BaseHash = FMD_ScrapeMeshUVSyncProtocol::AccumulateStateHash(BaseHash, Base);
    }
    if (BaseHash != Descriptor.GeneratedBaseHash) return false;
    // Grow the reserved staging array backwards; no second full cell array overlaps it.
    int32 Exception = States.Num() - 1;
    States.Reserve(Surface.GetContaminationActiveCellCount());
    States.SetNum(Surface.GetContaminationActiveCellCount(), EAllowShrinking::No);
    int32 Write = States.Num() - 1;
    for (int32 Index = Surface.GetContaminationAtlasCellCount() - 1; Index >= 0; --Index)
    {
        if (!Surface.IsContaminationCellActive(Index)) continue;
        if (Exception >= 0 && States[Exception].GetCellIndex() == Index) States[Write--] = States[Exception--];
        else BuildGeneratedCell(Surface, Index, States[Write--]);
    }
    uint32 FullHash = 2166136261u;
    for (const auto& State : States) FullHash = FMD_ScrapeMeshUVSyncProtocol::AccumulateStateHash(FullHash, State);
    return FullHash == Descriptor.FullStateHash;
}

bool UMD_ScrapeMeshUVSyncSubsystem::BeginClientSnapshot(UMD_ScrapeSurfaceComponent& Surface, const UObject* Owner)
{
    Owner = Owner ? Owner : &Surface;
    PruneExpiredOwners();
    ClientLiveStates.Remove(Owner);
    const uint64 Bytes = static_cast<uint64>(Surface.GetContaminationStorageCellCount()) * sizeof(uint32);
    if (Bytes > MaximumSurfaceLiveHistoryBytes
        || GetClientHistoryAllocatedBytes() + Bytes + sizeof(FMD_ScrapeMeshUVLiveHistory) > MaximumWorldLiveHistoryBytes)
    { return false; }
    FMD_ScrapeMeshUVLiveHistory& History = ClientLiveStates.FindOrAdd(Owner);
    History.Surface = &Surface;
    History.LayoutId = Surface.GetContaminationLayoutId();
    History.Revisions.Init(0, Surface.GetContaminationStorageCellCount());
    if (History.Revisions.GetAllocatedSize() > MaximumSurfaceLiveHistoryBytes
        || GetClientHistoryAllocatedBytes() > MaximumWorldLiveHistoryBytes)
    { ClientLiveStates.Remove(Owner); return false; }
    return true;
}

void UMD_ScrapeMeshUVSyncSubsystem::EndClientSnapshot(UMD_ScrapeSurfaceComponent& Surface, const UObject* Owner)
{
    Owner = Owner ? Owner : &Surface;
    ClientLiveStates.Remove(Owner);
    if (ClientLiveStates.IsEmpty()) { ClientLiveStates.Empty(); }
}

void UMD_ScrapeMeshUVSyncSubsystem::RecordClientVisualDelta(const FMD_ScrapeAuthoritativeVisualDelta& Delta)
{
    UMD_ScrapeSurfaceComponent* Surface = Delta.Surface.Get();
    if (!IsValid(Surface) || Surface->HasAuthority() || !Surface->UsesMeshUVSurface()) { return; }
    // Only an in-flight snapshot can overwrite newer live data. Outside that
    // interval no duplicate per-cell history is necessary.
    for (auto& Pair : ClientLiveStates)
    {
        FMD_ScrapeMeshUVLiveHistory& History = Pair.Value;
        if (History.Surface.Get() != Surface || History.LayoutId != Surface->GetContaminationLayoutId()) { continue; }
        for (const FMD_ReplicatedScrapeCellState& State : Delta.CellStates)
        {
            const int32 StorageIndex = Surface->GetContaminationStorageIndex(State.GetCellIndex());
            if (!History.Revisions.IsValidIndex(StorageIndex)) { continue; }
            uint32& Revision = History.Revisions[StorageIndex];
            if (Revision == 0 || Surface->IsVisualRevisionNewer(Delta.SurfaceVisualRevision, Revision))
            { Revision = Delta.SurfaceVisualRevision; }
        }
    }
}

bool UMD_ScrapeMeshUVSyncSubsystem::MergeClientVisualStates(
    UMD_ScrapeSurfaceComponent& Surface, const uint32 SnapshotRevision,
    TArray<FMD_ReplicatedScrapeCellState>& States, const UObject* Owner)
{
    Owner = Owner ? Owner : &Surface;
    const FMD_ScrapeMeshUVLiveHistory* History = ClientLiveStates.Find(Owner);
    if (!History || History->Surface.Get() != &Surface || History->LayoutId != Surface.GetContaminationLayoutId()
        || History->Revisions.Num() != Surface.GetContaminationStorageCellCount()) { return false; }
    for (FMD_ReplicatedScrapeCellState& State : States)
    {
        const int32 StorageIndex = Surface.GetContaminationStorageIndex(State.GetCellIndex());
        if (!History->Revisions.IsValidIndex(StorageIndex)) { return false; }
        const uint32 Revision = History->Revisions[StorageIndex];
        if (Revision != 0 && Surface.IsVisualRevisionNewer(Revision, SnapshotRevision))
        {
            bool bGenerated = false;
            // Read authoritative cells, never the locally predicted visual shadow.
            if (!Surface.TryBuildReplicatedCellState(State.GetCellIndex(), State, bGenerated)) { return false; }
        }
    }
    return true;
}
