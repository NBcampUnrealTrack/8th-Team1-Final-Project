#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncSubsystem.h"
#include "Component/Scrape/MD_ScrapeMeshUVSyncComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UMD_ScrapeMeshUVSyncSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

TStatId UMD_ScrapeMeshUVSyncSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UMD_ScrapeMeshUVSyncSubsystem, STATGROUP_Tickables);
}

void UMD_ScrapeMeshUVSyncSubsystem::Tick(const float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || World->GetNetMode() == NM_Client) { return; }
    ScanAccumulator += DeltaTime;
    if (ScanAccumulator < 1.0f) { return; }
    ScanAccumulator = 0.0f;
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
    for (auto It = Journals.CreateIterator(); It; ++It)
    { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
}

FMD_ScrapeMeshUVJournal& UMD_ScrapeMeshUVSyncSubsystem::FindJournal(UMD_ScrapeSurfaceComponent& Surface)
{
    FMD_ScrapeMeshUVJournal& Journal = Journals.FindOrAdd(&Surface);
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
    Journals.Remove(&Surface);
    FindJournal(Surface);
    // Epoch changes retain a monotonic visual revision, including same-layout resets.
    Surface.AdvanceContaminationVisualRevision();
    if (AActor* Owner = Surface.GetOwner()) { Owner->ForceNetUpdate(); }
}

void UMD_ScrapeMeshUVSyncSubsystem::RecordSurfaceChanges(
    UMD_ScrapeSurfaceComponent& Surface, const TConstArrayView<FMD_ScrapeCellChange> Changes)
{
    if (!Surface.HasAuthority() || !Surface.UsesMeshUVSurface()) { return; }
    FMD_ScrapeMeshUVJournal& Journal = FindJournal(Surface);
    if (Journal.Serial == MAX_uint64)
    {
        Journal.Epoch = FGuid::NewGuid(); Journal.Serial = 1; Journal.ChangedCells.Reset();
    }
    ++Journal.Serial;
    for (const FMD_ScrapeCellChange& Change : Changes)
    {
        FMD_ReplicatedScrapeCellState State;
        bool bGenerated = false;
        if (Surface.TryBuildReplicatedCellState(Change.CellIndex, State, bGenerated))
        {
            FMD_ScrapeMeshUVJournalEntry& Entry = Journal.ChangedCells.FindOrAdd(Change.CellIndex);
            Entry.Serial = Journal.Serial; Entry.State = State;
        }
    }
}

bool UMD_ScrapeMeshUVSyncSubsystem::CaptureSnapshot(
    UMD_ScrapeSurfaceComponent& Surface, const FGuid& KnownEpoch, const uint64 KnownSerial,
    FMD_ScrapeMeshUVSnapshotDescriptor& OutDescriptor,
    TArray<FMD_ReplicatedScrapeCellState>& OutStates)
{
    OutStates.Reset();
    if (!Surface.HasAuthority() || !Surface.UsesMeshUVSurface() || !Surface.IsSurfaceInitialized()
        || !Surface.AreContaminationCarrierWeightsCurrent()) { return false; }
    FMD_ScrapeMeshUVJournal& Journal = FindJournal(Surface);
    OutDescriptor.LayoutId = Journal.LayoutId;
    OutDescriptor.Grid = Journal.Grid;
    OutDescriptor.CarrierScale = Surface.GetContaminationCarrierWeightScale();
    OutDescriptor.Epoch = Journal.Epoch;
    OutDescriptor.Serial = Journal.Serial;
    OutDescriptor.VisualRevision = Surface.ReplicatedContaminationVisualRevision;
    OutDescriptor.bFullSnapshot = KnownEpoch != Journal.Epoch || KnownSerial == 0 || KnownSerial > Journal.Serial;
    if (OutDescriptor.bFullSnapshot)
    {
        OutStates.Reserve(Surface.GetContaminationActiveCellCount());
        for (int32 Index = 0; Index < Surface.ContaminationCells.Num(); ++Index)
        {
            if (!Surface.IsContaminationCellActive(Index)) { continue; }
            FMD_ReplicatedScrapeCellState State;
            bool bGenerated = false;
            if (!Surface.TryBuildReplicatedCellState(Index, State, bGenerated)) { return false; }
            OutStates.Add(State);
        }
    }
    else
    {
        for (const auto& Pair : Journal.ChangedCells)
        { if (Pair.Value.Serial > KnownSerial) { OutStates.Add(Pair.Value.State); } }
        OutStates.Sort([](const FMD_ReplicatedScrapeCellState& A, const FMD_ReplicatedScrapeCellState& B)
            { return A.EncodedCellIndex < B.EncodedCellIndex; });
    }
    OutDescriptor.TotalStates = OutStates.Num();
    return true;
}

void UMD_ScrapeMeshUVSyncSubsystem::RecordClientVisualDelta(const FMD_ScrapeAuthoritativeVisualDelta& Delta)
{
    UMD_ScrapeSurfaceComponent* Surface = Delta.Surface.Get();
    if (!IsValid(Surface) || Surface->HasAuthority() || !Surface->UsesMeshUVSurface()) { return; }
    TMap<int32, FMD_ScrapeMeshUVLiveState>& Live = ClientLiveStates.FindOrAdd(Surface);
    for (const FMD_ReplicatedScrapeCellState& State : Delta.CellStates)
    {
        FMD_ScrapeMeshUVLiveState& Entry = Live.FindOrAdd(State.GetCellIndex());
        if (Entry.LayoutId != Surface->GetContaminationLayoutId()
            || Entry.VisualRevision == 0 || Entry.VisualRevision == Delta.SurfaceVisualRevision
            || Surface->IsVisualRevisionNewer(Delta.SurfaceVisualRevision, Entry.VisualRevision))
        { Entry.LayoutId = Surface->GetContaminationLayoutId();
            Entry.VisualRevision = Delta.SurfaceVisualRevision; Entry.State = State; }
    }
}

void UMD_ScrapeMeshUVSyncSubsystem::MergeClientVisualStates(
    UMD_ScrapeSurfaceComponent& Surface, const uint32 SnapshotRevision,
    TArray<FMD_ReplicatedScrapeCellState>& States)
{
    TMap<int32, FMD_ScrapeMeshUVLiveState>* Live = ClientLiveStates.Find(&Surface);
    if (!Live) { return; }
    for (FMD_ReplicatedScrapeCellState& State : States)
    {
        const FMD_ScrapeMeshUVLiveState* Entry = Live->Find(State.GetCellIndex());
        if (Entry && Entry->LayoutId == Surface.GetContaminationLayoutId()
            && Surface.IsVisualRevisionNewer(Entry->VisualRevision, SnapshotRevision))
        { State = Entry->State; }
    }
    for (auto It = Live->CreateIterator(); It; ++It)
    { if (It.Value().LayoutId != Surface.GetContaminationLayoutId()
        || !Surface.IsVisualRevisionNewer(It.Value().VisualRevision, SnapshotRevision)) { It.RemoveCurrent(); } }
    for (auto It = ClientLiveStates.CreateIterator(); It; ++It)
    { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
}
