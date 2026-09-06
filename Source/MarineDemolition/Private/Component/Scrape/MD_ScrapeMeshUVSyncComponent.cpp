#include "Component/Scrape/MD_ScrapeMeshUVSyncComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncSubsystem.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDScrapeMeshUVSync, Log, All);

namespace
{
    constexpr double TransferTimeoutSeconds = 15.0;
    constexpr double MaximumSyncDistanceCm = 20000.0;
    constexpr float PumpIntervalSeconds = 0.01f;
    constexpr int32 MaximumPagesPerPump = 1;
}

UMD_ScrapeMeshUVSyncComponent::UMD_ScrapeMeshUVSyncComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UMD_ScrapeMeshUVSyncComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(PumpTimer, this,
            &UMD_ScrapeMeshUVSyncComponent::Pump, PumpIntervalSeconds, true);
    }
}

void UMD_ScrapeMeshUVSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(PumpTimer); }
    ClearServerTransfer(); ClearClientTransfer();
    Super::EndPlay(EndPlayReason);
}

bool UMD_ScrapeMeshUVSyncComponent::IsSurfaceAllowed(const UMD_ScrapeSurfaceComponent* Surface) const
{
    const APlayerController* PC = Cast<APlayerController>(GetOwner());
    if (!PC || !IsValid(Surface) || Surface->GetWorld() != GetWorld()
        || !Surface->IsRegistered() || !Surface->IsSurfaceInitialized()
        || !Surface->AreContaminationCarrierWeightsCurrent()
        || !Surface->bContaminationEnabled || !Surface->UsesMeshUVSurface()
        || !Surface->GetContaminationLayoutId().IsValid() || !IsValid(Surface->GetOwner())
        || !Surface->SurfaceMesh) { return false; }
    const UMD_ScrapeMeshUVData* Data = Surface->GetContaminationMeshUVData();
    if (!Data || !Data->IsValidForMesh(Surface->SurfaceMesh->GetStaticMesh())) { return false; }
    FVector ViewOrigin; FRotator ViewRotation;
    PC->GetPlayerViewPoint(ViewOrigin, ViewRotation);
    if (ViewOrigin.ContainsNaN() || Surface->SurfaceMesh->Bounds.GetBox().ComputeSquaredDistanceToPoint(ViewOrigin)
        > FMath::Square(MaximumSyncDistanceCm)) { return false; }
    return !PC->HasAuthority() || (Surface->HasAuthority()
        && Surface->GetOwner()->IsNetRelevantFor(PC, PC->GetViewTarget(), ViewOrigin));
}

void UMD_ScrapeMeshUVSyncComponent::Pump()
{
    APlayerController* PC = Cast<APlayerController>(GetOwner());
    if (!PC || !GetWorld()) { return; }
    const double Now = GetWorld()->GetTimeSeconds();
    if (PC->HasAuthority())
    {
        if (ServerDescriptor.TransferId != 0)
        {
            if (!IsSurfaceAllowed(ServerSurface.Get()) || Now - LastServerActivity > TransferTimeoutSeconds)
            {
                ClientAbortTransfer(ServerSurface.Get(), ServerDescriptor.TransferId);
                ClearServerTransfer();
            }
            else { SendPages(); }
        }
        return;
    }
    if (!PC->IsLocalController()) { return; }
    if (ClientSurface.IsValid())
    {
        if (Now - LastClientActivity > TransferTimeoutSeconds)
        {
            ServerCancelTransfer(ClientAssembler.Descriptor.TransferId);
            ClearClientTransfer();
        }
        return;
    }
    if (Now - LastClientPoll >= 0.25)
    { LastClientPoll = Now; PollClientSurfaces(); }
}

void UMD_ScrapeMeshUVSyncComponent::PollClientSurfaces()
{
    const double Now = GetWorld()->GetTimeSeconds();
    UMD_ScrapeSurfaceComponent* Candidate = nullptr;
    double OldestCheck = Now;
    for (TObjectIterator<UMD_ScrapeSurfaceComponent> It; It; ++It)
    {
        UMD_ScrapeSurfaceComponent* Surface = *It;
        if (!IsSurfaceAllowed(Surface)) { continue; }
        FMD_ScrapeMeshUVSyncCheckpoint& Checkpoint = ClientCheckpoints.FindOrAdd(Surface);
        const bool bChanged = Checkpoint.LayoutId != Surface->GetContaminationLayoutId()
            || Checkpoint.VisualRevision != Surface->ReplicatedContaminationVisualRevision;
        // Periodic probes also detect resets that retain the same visual revision.
        if (Now - Checkpoint.LastChecked < (bChanged ? 0.5 : 5.0)) { continue; }
        if (Checkpoint.LastChecked < OldestCheck)
        { Candidate = Surface; OldestCheck = Checkpoint.LastChecked; }
    }
    for (auto It = ClientCheckpoints.CreateIterator(); It; ++It)
    { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
    if (!Candidate) { return; }
    FMD_ScrapeMeshUVSyncCheckpoint& Checkpoint = ClientCheckpoints.FindOrAdd(Candidate);
    const bool bNeedFull = !Checkpoint.Epoch.IsValid()
        || Checkpoint.LayoutId != Candidate->GetContaminationLayoutId();
    Checkpoint.LastChecked = Now;
    ClientSurface = Candidate; ClientAssembler.Reset(); LastClientActivity = Now;
    ClientTransferStartedSeconds = FPlatformTime::Seconds();
    ServerRequestSurface(Candidate, Candidate->GetContaminationLayoutId(),
        Candidate->GetContaminationGridDimensions(), Candidate->GetContaminationCarrierWeightScale(), bNeedFull);
}

void UMD_ScrapeMeshUVSyncComponent::ServerRequestSurface_Implementation(
    UMD_ScrapeSurfaceComponent* Surface, const FGuid LayoutId, const FIntPoint Grid, const FVector CarrierScale, const bool bNeedFullSnapshot)
{
    const double RequestStartedSeconds = FPlatformTime::Seconds();
    const double Now = GetWorld()->GetTimeSeconds();
    // Client parameters select read-only state; no authoritative gameplay mutation is accepted.
    if (ServerDescriptor.TransferId != 0 || Now - LastServerRequest < 0.2) { return; }
    LastServerRequest = Now;
    if (Now - LastFullSnapshotCapture < 1.0) { ClientAbortTransfer(Surface, 0); return; }
    if (!IsSurfaceAllowed(Surface) || LayoutId != Surface->GetContaminationLayoutId()
        || Grid != Surface->GetContaminationGridDimensions()
        || !FMD_ScrapeSurfaceLayout::AreCarrierScalesCompatible(CarrierScale, Surface->GetContaminationCarrierWeightScale()))
    { ClientAbortTransfer(Surface, 0); return; }
    UMD_ScrapeMeshUVSyncSubsystem* Subsystem = GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>();
    if (!Subsystem) { ClientAbortTransfer(Surface, 0); return; }
    const FMD_ScrapeMeshUVSyncCheckpoint* Checkpoint = ServerCheckpoints.Find(Surface);
    const FGuid KnownEpoch = Checkpoint && !bNeedFullSnapshot ? Checkpoint->Epoch : FGuid();
    const uint64 KnownSerial = Checkpoint && !bNeedFullSnapshot ? Checkpoint->Serial : 0;
    TArray<FMD_ReplicatedScrapeCellState> States;
    if (!Subsystem->CaptureSnapshot(*Surface, KnownEpoch, KnownSerial, ServerDescriptor, States))
    { ClearServerTransfer(); ClientAbortTransfer(Surface, 0); return; }
    if (ServerDescriptor.bFullSnapshot) { LastFullSnapshotCapture = Now; }
    ++NextTransferId;
    if (NextTransferId == 0) { ++NextTransferId; }
    ServerDescriptor.TransferId = NextTransferId;
    const bool bEncoded = FMD_ScrapeMeshUVSyncProtocol::EncodePayload(States, ServerDescriptor, ServerBytes);
    States.Empty();
    if (!bEncoded)
    { ClearServerTransfer(); ClientAbortTransfer(Surface, 0); return; }
    ServerSurface = Surface; ServerWindow = FMD_ScrapeMeshUVSendWindow(); LastServerActivity = Now;
    ServerTransferStartedSeconds = RequestStartedSeconds;
    UE_LOG(LogMDScrapeMeshUVSync, Log,
        TEXT("MeshUVSync Begin owner=%s surface=%s transfer=%u full=%d states=%d pages=%d payload_bytes=%d decoded_bytes=%d compressed=%d window=%d burst=%d capture_seconds=%.6f"),
        *GetNameSafe(GetOwner()), *GetNameSafe(Surface), ServerDescriptor.TransferId, ServerDescriptor.bFullSnapshot,
        ServerDescriptor.TotalStates, FMD_ScrapeMeshUVSyncProtocol::GetPageCount(ServerDescriptor),
        ServerBytes.Num(), ServerDescriptor.TotalStates * FMD_ScrapeMeshUVSyncProtocol::BytesPerCell,
        ServerDescriptor.bCompressed, FMD_ScrapeMeshUVSyncProtocol::MaximumPagesInFlight, MaximumPagesPerPump,
        FPlatformTime::Seconds() - RequestStartedSeconds);
    SendPages();
}

void UMD_ScrapeMeshUVSyncComponent::SendPages()
{
    if (!ServerSurface.IsValid() || ServerDescriptor.TransferId == 0) { return; }
    const double NowSeconds = FPlatformTime::Seconds();
    // A slow frame can dispatch a repeating timer several times; do not catch up with an RPC burst.
    if (NowSeconds - LastServerSendSeconds < PumpIntervalSeconds) { return; }
    const APlayerController* PC = Cast<APlayerController>(GetOwner());
    UNetConnection* Connection = PC ? PC->GetNetConnection() : nullptr;
    const int32 TotalPages = FMD_ScrapeMeshUVSyncProtocol::GetPageCount(ServerDescriptor);
    ServerSendBudget.Refill(NowSeconds, Connection ? Connection->CurrentNetSpeed : 0);
    bool bSentBatch = false;
    for (int32 Sent = 0; Sent < MaximumPagesPerPump; ++Sent)
    {
        // Respect the engine bandwidth budget; never use the saturating/force overload.
        if (Connection && !Connection->IsNetReady()) { ++ServerCongestedPumps; break; }
        if (ServerWindow.NextPageToSend >= TotalPages
            || ServerWindow.GetPagesInFlight() >= FMD_ScrapeMeshUVSyncProtocol::MaximumPagesInFlight) { break; }
        const int32 Offset = ServerWindow.NextPageToSend * FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes;
        const int32 Count = FMath::Min(FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes, ServerBytes.Num() - Offset);
        if (Count < 0) { ClientAbortTransfer(ServerSurface.Get(), ServerDescriptor.TransferId); ClearServerTransfer(); return; }
        if (!ServerSendBudget.TrySpend(Count)) { ++ServerBudgetLimitedPumps; break; }
        const int32 PageIndex = ServerWindow.TakeNextPage(TotalPages);
        if (PageIndex == INDEX_NONE) { break; }
        TArray<uint8> Payload;
        if (Count > 0) { Payload.Append(ServerBytes.GetData() + Offset, Count); }
        LastServerActivity = GetWorld()->GetTimeSeconds();
        ClientReceivePage(ServerSurface.Get(), ServerDescriptor, PageIndex, Payload);
        bSentBatch = true;
    }
    if (bSentBatch) { LastServerSendSeconds = NowSeconds; ++ServerSendBatches; }
}

void UMD_ScrapeMeshUVSyncComponent::ClientReceivePage_Implementation(
    UMD_ScrapeSurfaceComponent* Surface, const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor,
    const int32 PageIndex, const TArray<uint8>& Payload)
{
    // Pages already queued before cancellation must not clear a newer request/transfer.
    if (Descriptor.TransferId == LastClosedClientTransferId || ClientSurface.Get() != Surface
        || (ClientAssembler.bStarted && Descriptor.TransferId != ClientAssembler.Descriptor.TransferId))
    { ServerAcknowledgePage(Descriptor.TransferId, PageIndex, false); return; }
    const bool bValid = ClientSurface.Get() == Surface && IsSurfaceAllowed(Surface)
        && Descriptor.LayoutId == Surface->GetContaminationLayoutId()
        && Descriptor.Grid == Surface->GetContaminationGridDimensions()
        && FMD_ScrapeSurfaceLayout::AreCarrierScalesCompatible(Descriptor.CarrierScale, Surface->GetContaminationCarrierWeightScale())
        && ClientAssembler.Append(Descriptor, PageIndex, Payload);
    if (!bValid)
    {
        LastClosedClientTransferId = Descriptor.TransferId;
        ServerAcknowledgePage(Descriptor.TransferId, PageIndex, false); ClearClientTransfer(); return;
    }
    LastClientActivity = GetWorld()->GetTimeSeconds();
    const bool bLast = ClientAssembler.NextPage == FMD_ScrapeMeshUVSyncProtocol::GetPageCount(Descriptor);
    if (bLast)
    {
        TArray<FMD_ReplicatedScrapeCellState> States;
        bool bComplete = ClientAssembler.Finish(States)
            && (!Descriptor.bFullSnapshot || States.Num() == Surface->GetContaminationActiveCellCount());
        for (const FMD_ReplicatedScrapeCellState& State : States)
        { if (!Surface->IsContaminationCellActive(State.GetCellIndex())) { bComplete = false; break; } }
        if (!bComplete)
        { ServerAcknowledgePage(Descriptor.TransferId, PageIndex, false); ClearClientTransfer(); return; }
        if (UMD_ScrapeMeshUVSyncSubsystem* Subsystem = GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>())
        { Subsystem->MergeClientVisualStates(*Surface, Descriptor.VisualRevision, States); }
        // One commit after complete validation; prediction reconciliation remains on the existing path.
        Surface->ApplyReplicatedContaminationStates(States, false);
        if (Surface->LastAppliedContaminationVisualRevision == 0
            || Surface->IsVisualRevisionNewer(Descriptor.VisualRevision, Surface->LastAppliedContaminationVisualRevision))
        { Surface->LastAppliedContaminationVisualRevision = Descriptor.VisualRevision; }
        FMD_ScrapeMeshUVSyncCheckpoint& Checkpoint = ClientCheckpoints.FindOrAdd(Surface);
        Checkpoint.Epoch = Descriptor.Epoch; Checkpoint.LayoutId = Descriptor.LayoutId;
        Checkpoint.Serial = Descriptor.Serial; Checkpoint.VisualRevision = Descriptor.VisualRevision;
        Checkpoint.LastChecked = GetWorld()->GetTimeSeconds();
        UE_LOG(LogMDScrapeMeshUVSync, Log,
            TEXT("MeshUVSync ClientCommit owner=%s surface=%s transfer=%u full=%d states=%d pages=%d payload_bytes=%d decoded_bytes=%d compressed=%d elapsed_seconds=%.6f"),
            *GetNameSafe(GetOwner()), *GetNameSafe(Surface), Descriptor.TransferId, Descriptor.bFullSnapshot,
            Descriptor.TotalStates, ClientAssembler.NextPage, ClientAssembler.Bytes.Num(),
            Descriptor.TotalStates * FMD_ScrapeMeshUVSyncProtocol::BytesPerCell, Descriptor.bCompressed,
            FPlatformTime::Seconds() - ClientTransferStartedSeconds);
        ClearClientTransfer();
    }
    ServerAcknowledgePage(Descriptor.TransferId, PageIndex, true);
}

void UMD_ScrapeMeshUVSyncComponent::ServerAcknowledgePage_Implementation(
    const uint32 TransferId, const int32 PageIndex, const bool bAccepted)
{
    if (TransferId != ServerDescriptor.TransferId || !ServerWindow.Acknowledge(PageIndex)) { return; }
    if (!bAccepted) { ClearServerTransfer(); return; }
    LastServerActivity = GetWorld()->GetTimeSeconds();
    if (ServerWindow.IsComplete(FMD_ScrapeMeshUVSyncProtocol::GetPageCount(ServerDescriptor)))
    {
        FMD_ScrapeMeshUVSyncCheckpoint& Checkpoint = ServerCheckpoints.FindOrAdd(ServerSurface);
        Checkpoint.Epoch = ServerDescriptor.Epoch; Checkpoint.LayoutId = ServerDescriptor.LayoutId;
        Checkpoint.Serial = ServerDescriptor.Serial; Checkpoint.VisualRevision = ServerDescriptor.VisualRevision;
        UE_LOG(LogMDScrapeMeshUVSync, Log,
            TEXT("MeshUVSync ServerComplete owner=%s surface=%s transfer=%u full=%d states=%d pages=%d payload_bytes=%d decoded_bytes=%d compressed=%d elapsed_seconds=%.6f peak_in_flight=%d send_batches=%d congested_pumps=%d budget_limited_pumps=%d accounted_wire_bytes=%d budget_fraction=0.25"),
            *GetNameSafe(GetOwner()), *GetNameSafe(ServerSurface.Get()), TransferId, ServerDescriptor.bFullSnapshot,
            ServerDescriptor.TotalStates, ServerWindow.AcknowledgedPages, ServerBytes.Num(),
            ServerDescriptor.TotalStates * FMD_ScrapeMeshUVSyncProtocol::BytesPerCell, ServerDescriptor.bCompressed,
            FPlatformTime::Seconds() - ServerTransferStartedSeconds, ServerWindow.PeakPagesInFlight,
            ServerSendBatches, ServerCongestedPumps, ServerBudgetLimitedPumps,
            ServerBytes.Num() + ServerWindow.AcknowledgedPages * FMD_ScrapeMeshUVSyncProtocol::PageOverheadBudgetBytes);
        ClearServerTransfer();
    }
}

void UMD_ScrapeMeshUVSyncComponent::ServerCancelTransfer_Implementation(const uint32 TransferId)
{
    if (TransferId == 0 || TransferId == ServerDescriptor.TransferId) { ClearServerTransfer(); }
}

void UMD_ScrapeMeshUVSyncComponent::ClientAbortTransfer_Implementation(
    UMD_ScrapeSurfaceComponent* Surface, const uint32 TransferId)
{
    if (ClientSurface.Get() == Surface
        && (TransferId == 0 || ClientAssembler.Descriptor.TransferId == TransferId))
    { ClearClientTransfer(); }
}

void UMD_ScrapeMeshUVSyncComponent::ClearServerTransfer()
{
    ServerSurface.Reset(); ServerDescriptor = FMD_ScrapeMeshUVSnapshotDescriptor();
    if (ServerBytes.Max() > FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes) { ServerBytes.Empty(); }
    else { ServerBytes.Reset(); }
    ServerWindow = FMD_ScrapeMeshUVSendWindow(); ServerTransferStartedSeconds = 0.0;
    LastServerSendSeconds = -100.0; ServerSendBatches = 0; ServerCongestedPumps = 0; ServerBudgetLimitedPumps = 0;
}

void UMD_ScrapeMeshUVSyncComponent::ClearClientTransfer()
{
    if (ClientAssembler.bStarted) { LastClosedClientTransferId = ClientAssembler.Descriptor.TransferId; }
    ClientSurface.Reset(); ClientAssembler.Reset(); ClientTransferStartedSeconds = 0.0;
}
