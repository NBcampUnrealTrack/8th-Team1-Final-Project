#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncProtocol.h"
#include "MD_ScrapeMeshUVSyncComponent.generated.h"

struct FMD_ScrapeMeshUVSyncCheckpoint
{
    FGuid Epoch;
    FGuid LayoutId;
    uint64 Serial = 0;
    uint32 VisualRevision = 0;
    double LastChecked = -100.0;
};

/** Player-controller-owned transport: one immutable transfer with a bounded page window. */
UCLASS(Transient, NotBlueprintable)
class MARINEDEMOLITION_API UMD_ScrapeMeshUVSyncComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMD_ScrapeMeshUVSyncComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    uint64 GetAllocatedTransferBytes() const
    {
        return ServerBytes.GetAllocatedSize() + ClientAssembler.Bytes.GetAllocatedSize()
            + ServerCheckpoints.GetAllocatedSize() + ClientCheckpoints.GetAllocatedSize();
    }
private:
    void Pump();
    void PruneCheckpoints();
    double LastCheckpointPrune = -100.0;
    static constexpr uint64 MaximumCheckpointBytes = 256 * 1024;
    void PollClientSurfaces();
    bool IsSurfaceAllowed(const UMD_ScrapeSurfaceComponent* Surface) const;
    void SendPages();
    void ClearServerTransfer();
    void ClearClientTransfer();
    UFUNCTION(Server, Reliable)
    void ServerRequestSurface(UMD_ScrapeSurfaceComponent* Surface, FGuid LayoutId, FIntPoint Grid, FVector CarrierScale, bool bNeedFullSnapshot);
    UFUNCTION(Client, Reliable)
    void ClientReceivePage(UMD_ScrapeSurfaceComponent* Surface,
        const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor, int32 PageIndex, const TArray<uint8>& Payload);
    UFUNCTION(Server, Reliable)
    void ServerAcknowledgePage(uint32 TransferId, int32 PageIndex, bool bAccepted);
    UFUNCTION(Server, Reliable)
    void ServerCancelTransfer(uint32 TransferId);
    UFUNCTION(Client, Reliable)
    void ClientAbortTransfer(UMD_ScrapeSurfaceComponent* Surface, uint32 TransferId);

    FTimerHandle PumpTimer;
    TWeakObjectPtr<UMD_ScrapeSurfaceComponent> ServerSurface;
    FMD_ScrapeMeshUVSnapshotDescriptor ServerDescriptor;
    TArray<uint8> ServerBytes;
    FMD_ScrapeMeshUVSendWindow ServerWindow;
    FMD_ScrapeMeshUVSendBudget ServerSendBudget;
    double ServerTransferStartedSeconds = 0.0;
    double LastServerSendSeconds = -100.0;
    int32 ServerSendBatches = 0;
    int32 ServerCongestedPumps = 0;
    int32 ServerBudgetLimitedPumps = 0;
    uint32 NextTransferId = 0;
    double LastServerActivity = 0.0;
    double LastServerRequest = -100.0;
    double LastFullSnapshotCapture = -100.0;
    TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, FMD_ScrapeMeshUVSyncCheckpoint> ServerCheckpoints;

    TWeakObjectPtr<UMD_ScrapeSurfaceComponent> ClientSurface;
    FMD_ScrapeMeshUVSnapshotAssembler ClientAssembler;
    double LastClientActivity = 0.0;
    double ClientTransferStartedSeconds = 0.0;
    uint32 LastClosedClientTransferId = 0;
    double LastClientPoll = -100.0;
    TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, FMD_ScrapeMeshUVSyncCheckpoint> ClientCheckpoints;
};
