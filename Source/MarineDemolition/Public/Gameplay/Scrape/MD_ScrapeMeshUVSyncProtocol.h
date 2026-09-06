#pragma once

#include "CoreMinimal.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "MD_ScrapeMeshUVSyncProtocol.generated.h"

/** Fixed identity for one immutable, bounded-page transfer. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVSnapshotDescriptor
{
    GENERATED_BODY()
    UPROPERTY() FGuid LayoutId;
    UPROPERTY() FIntPoint Grid = FIntPoint::ZeroValue;
    UPROPERTY() FVector CarrierScale = FVector::OneVector;
    UPROPERTY() FGuid Epoch;
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() uint32 VisualRevision = 0;
    UPROPERTY() uint32 TransferId = 0;
    UPROPERTY() int32 TotalStates = 0;
    UPROPERTY() uint32 ContentHash = 0;
    UPROPERTY() int32 PayloadBytes = 0;
    UPROPERTY() uint32 PayloadHash = 0;
    // Compressed payload is zlib over nine byte planes; raw cell encoding remains unchanged.
    UPROPERTY() bool bCompressed = false;
    UPROPERTY() bool bFullSnapshot = false;
    bool operator==(const FMD_ScrapeMeshUVSnapshotDescriptor& Other) const;
};

/** States remain 9 bytes/cell; optional lossless transport compression never changes cell addressing. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVSyncProtocol
{
    static constexpr int32 BytesPerCell = 9;
    static constexpr int32 MaximumPayloadBytes = 512;
    static constexpr int32 MaximumPagesInFlight = 4;
    static constexpr int32 MaximumSnapshotStates = 1024 * 1024;
    static constexpr int32 MaximumDecodedBytes = MaximumSnapshotStates * BytesPerCell;
    // Budget for descriptor, RPC/bunch and packet headers in addition to each page's payload.
    static constexpr int32 PageOverheadBudgetBytes = 512;
    static constexpr double MaximumConnectionBudgetFraction = 0.25;
    static bool IsDescriptorValid(const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor);
    static int32 GetPageCount(int32 TotalStates);
    static int32 GetPageCount(const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor);
    static bool EncodePayload(TConstArrayView<FMD_ReplicatedScrapeCellState> States,
        FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor, TArray<uint8>& OutPayload);
    static bool DecodePayload(const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor,
        TConstArrayView<uint8> Payload, TArray<FMD_ReplicatedScrapeCellState>& OutStates);
    static uint32 HashBytes(TConstArrayView<uint8> Bytes);
    static void EncodeStates(TConstArrayView<FMD_ReplicatedScrapeCellState> States, TArray<uint8>& OutBytes);
    static bool DecodeStates(TConstArrayView<uint8> Bytes, const FIntPoint& Grid, TArray<FMD_ReplicatedScrapeCellState>& OutStates);
};

/** One-page token capacity reserves at least three quarters of the nominal connection rate. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVSendBudget
{
    double LastUpdateSeconds = -1.0;
    double AvailableBytes = 0.0;
    void Refill(double NowSeconds, int32 ConnectionBytesPerSecond);
    bool TrySpend(int32 PayloadBytes);
};

/** Reliable RPCs preserve order; only an already-sent, consecutive ACK opens the window. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVSendWindow
{
    int32 NextPageToSend = 0;
    int32 AcknowledgedPages = 0;
    int32 PeakPagesInFlight = 0;
    int32 GetPagesInFlight() const { return NextPageToSend - AcknowledgedPages; }
    int32 TakeNextPage(int32 TotalPages);
    bool Acknowledge(int32 PageIndex);
    bool IsComplete(int32 TotalPages) const;
};

/** Staging never changes the live surface until every page and checksum is valid. */
struct MARINEDEMOLITION_API FMD_ScrapeMeshUVSnapshotAssembler
{
    FMD_ScrapeMeshUVSnapshotDescriptor Descriptor;
    TArray<uint8> Bytes;
    int32 NextPage = 0;
    bool bStarted = false;
    void Reset();
    bool Append(const FMD_ScrapeMeshUVSnapshotDescriptor& InDescriptor, int32 PageIndex, TConstArrayView<uint8> Payload);
    bool Finish(TArray<FMD_ReplicatedScrapeCellState>& OutStates) const;
};
