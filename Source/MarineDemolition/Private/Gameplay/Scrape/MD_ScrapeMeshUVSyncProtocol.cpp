#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncProtocol.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Misc/Compression.h"

bool FMD_ScrapeMeshUVSnapshotDescriptor::operator==(const FMD_ScrapeMeshUVSnapshotDescriptor& Other) const
{
    return LayoutId == Other.LayoutId && Grid == Other.Grid && CarrierScale == Other.CarrierScale && Epoch == Other.Epoch
        && Serial == Other.Serial && VisualRevision == Other.VisualRevision
        && TransferId == Other.TransferId && TotalStates == Other.TotalStates
        && ContentHash == Other.ContentHash && PayloadBytes == Other.PayloadBytes
        && PayloadHash == Other.PayloadHash && bCompressed == Other.bCompressed
        && bFullSnapshot == Other.bFullSnapshot;
}

bool FMD_ScrapeMeshUVSyncProtocol::IsDescriptorValid(const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor)
{
    int32 PerFace = 0, Total = 0;
    if (!Descriptor.LayoutId.IsValid() || !Descriptor.Epoch.IsValid()
        || !FMD_ScrapeSurfaceLayout::IsCarrierScaleValid(Descriptor.CarrierScale)
        || Descriptor.Serial == 0 || Descriptor.TransferId == 0
        || !FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(Descriptor.Grid, PerFace, Total)
        || Descriptor.TotalStates < 0 || Descriptor.TotalStates > PerFace
        || Descriptor.TotalStates > MaximumSnapshotStates) { return false; }
    const int32 DecodedBytes = Descriptor.TotalStates * BytesPerCell;
    return Descriptor.bCompressed
        ? Descriptor.PayloadBytes > 0 && Descriptor.PayloadBytes < DecodedBytes
        : Descriptor.PayloadBytes == DecodedBytes;
}

int32 FMD_ScrapeMeshUVSyncProtocol::GetPageCount(const int32 TotalStates)
{
    return TotalStates >= 0 && TotalStates <= MaximumSnapshotStates
        ? FMath::Max(1, FMath::DivideAndRoundUp(TotalStates * BytesPerCell, MaximumPayloadBytes)) : 0;
}

int32 FMD_ScrapeMeshUVSyncProtocol::GetPageCount(const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor)
{
    return FMath::Max(1, FMath::DivideAndRoundUp(Descriptor.PayloadBytes, MaximumPayloadBytes));
}

bool FMD_ScrapeMeshUVSyncProtocol::EncodePayload(
    const TConstArrayView<FMD_ReplicatedScrapeCellState> States,
    FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor, TArray<uint8>& OutPayload)
{
    OutPayload.Reset();
    if (States.Num() > MaximumSnapshotStates) { return false; }
    Descriptor.TotalStates = States.Num(); Descriptor.bCompressed = false;
    EncodeStates(States, OutPayload);
    Descriptor.ContentHash = HashBytes(OutPayload);
    if (OutPayload.Num() >= MaximumPayloadBytes)
    {
        int64 Bound = 0;
        if (FCompression::CompressMemoryBound(NAME_Zlib, Bound, OutPayload.Num())
            && Bound > 0 && Bound <= static_cast<int64>(MaximumDecodedBytes) + 65536)
        {
            // Byte planes group repeated channels and high address bytes without changing any value.
            TArray<uint8> Planes;
            Planes.SetNumUninitialized(OutPayload.Num());
            for (int32 Byte = 0; Byte < BytesPerCell; ++Byte)
            {
                for (int32 Cell = 0; Cell < States.Num(); ++Cell)
                { Planes[Byte * States.Num() + Cell] = OutPayload[Cell * BytesPerCell + Byte]; }
            }
            TArray<uint8> Compressed;
            Compressed.SetNumUninitialized(static_cast<int32>(Bound));
            int64 CompressedBytes = Bound;
            if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedBytes,
                Planes.GetData(), static_cast<int64>(Planes.Num()))
                && CompressedBytes > 0 && CompressedBytes < OutPayload.Num())
            {
                Compressed.SetNum(static_cast<int32>(CompressedBytes), EAllowShrinking::Yes);
                OutPayload = MoveTemp(Compressed); Descriptor.bCompressed = true;
            }
        }
    }
    Descriptor.PayloadBytes = OutPayload.Num(); Descriptor.PayloadHash = HashBytes(OutPayload);
    return IsDescriptorValid(Descriptor);
}

bool FMD_ScrapeMeshUVSyncProtocol::DecodePayload(
    const FMD_ScrapeMeshUVSnapshotDescriptor& Descriptor, const TConstArrayView<uint8> Payload,
    TArray<FMD_ReplicatedScrapeCellState>& OutStates)
{
    OutStates.Reset();
    if (!IsDescriptorValid(Descriptor) || Payload.Num() != Descriptor.PayloadBytes
        || HashBytes(Payload) != Descriptor.PayloadHash) { return false; }
    TArray<uint8> Decoded;
    TArray<uint8> Interleaved;
    TConstArrayView<uint8> Raw = Payload;
    if (Descriptor.bCompressed)
    {
        // The descriptor has bounded the exact output size before any decompression allocation.
        Decoded.SetNumZeroed(Descriptor.TotalStates * BytesPerCell);
        if (!FCompression::UncompressMemory(NAME_Zlib, Decoded.GetData(), static_cast<int64>(Decoded.Num()),
            Payload.GetData(), static_cast<int64>(Payload.Num()))) { return false; }
        Interleaved.SetNumUninitialized(Decoded.Num());
        for (int32 Byte = 0; Byte < BytesPerCell; ++Byte)
        {
            for (int32 Cell = 0; Cell < Descriptor.TotalStates; ++Cell)
            { Interleaved[Cell * BytesPerCell + Byte] = Decoded[Byte * Descriptor.TotalStates + Cell]; }
        }
        Decoded.Empty();
        Raw = Interleaved;
    }
    return HashBytes(Raw) == Descriptor.ContentHash && DecodeStates(Raw, Descriptor.Grid, OutStates);
}

void FMD_ScrapeMeshUVSendBudget::Refill(const double NowSeconds, const int32 ConnectionBytesPerSecond)
{
    if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
    { AvailableBytes = 0.0; LastUpdateSeconds = -1.0; return; }
    if (ConnectionBytesPerSecond <= 0 || LastUpdateSeconds < 0.0 || NowSeconds < LastUpdateSeconds)
    { AvailableBytes = 0.0; LastUpdateSeconds = NowSeconds; return; }
    const double Elapsed = FMath::Clamp(NowSeconds - LastUpdateSeconds, 0.0, 1.0);
    LastUpdateSeconds = NowSeconds;
    AvailableBytes = FMath::Min(AvailableBytes + Elapsed * ConnectionBytesPerSecond
        * FMD_ScrapeMeshUVSyncProtocol::MaximumConnectionBudgetFraction,
        static_cast<double>(FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes
            + FMD_ScrapeMeshUVSyncProtocol::PageOverheadBudgetBytes));
}

bool FMD_ScrapeMeshUVSendBudget::TrySpend(const int32 PayloadBytes)
{
    if (PayloadBytes < 0 || PayloadBytes > FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes) { return false; }
    const double Cost = PayloadBytes + FMD_ScrapeMeshUVSyncProtocol::PageOverheadBudgetBytes;
    if (AvailableBytes < Cost) { return false; }
    AvailableBytes -= Cost;
    return true;
}

uint32 FMD_ScrapeMeshUVSyncProtocol::HashBytes(const TConstArrayView<uint8> Bytes)
{
    uint32 Hash = 2166136261u;
    for (const uint8 Byte : Bytes) { Hash = (Hash ^ Byte) * 16777619u; }
    return Hash;
}

void FMD_ScrapeMeshUVSyncProtocol::EncodeStates(
    const TConstArrayView<FMD_ReplicatedScrapeCellState> States, TArray<uint8>& OutBytes)
{
    OutBytes.Reset(States.Num() * BytesPerCell);
    for (const FMD_ReplicatedScrapeCellState& State : States)
    {
        for (int32 Shift = 0; Shift < 32; Shift += 8)
        { OutBytes.Add(static_cast<uint8>(State.EncodedCellIndex >> Shift)); }
        OutBytes.Add(State.QuantizedInitialContamination);
        OutBytes.Add(State.QuantizedRemainingContamination);
        OutBytes.Add(State.QuantizedTransferredContamination);
        OutBytes.Add(State.QuantizedTransferredPatternDensity);
        OutBytes.Add(State.QuantizedCoveredFraction);
    }
}

bool FMD_ScrapeMeshUVSyncProtocol::DecodeStates(
    const TConstArrayView<uint8> Bytes, const FIntPoint& Grid,
    TArray<FMD_ReplicatedScrapeCellState>& OutStates)
{
    OutStates.Reset();
    int32 PerFace = 0, Total = 0;
    if (Bytes.Num() % BytesPerCell != 0
        || !FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(Grid, PerFace, Total)
        || Bytes.Num() / BytesPerCell > PerFace) { return false; }
    TSet<uint32> Unique;
    OutStates.Reserve(Bytes.Num() / BytesPerCell);
    for (int32 Offset = 0; Offset < Bytes.Num(); Offset += BytesPerCell)
    {
        uint32 Index = 0;
        for (int32 Byte = 0; Byte < 4; ++Byte)
        { Index |= static_cast<uint32>(Bytes[Offset + Byte]) << (Byte * 8); }
        if (Index >= static_cast<uint32>(PerFace) || Unique.Contains(Index))
        { OutStates.Reset(); return false; }
        Unique.Add(Index);
        OutStates.Emplace(Index, Bytes[Offset + 4], Bytes[Offset + 5],
            Bytes[Offset + 6], Bytes[Offset + 7], Bytes[Offset + 8]);
    }
    return true;
}

int32 FMD_ScrapeMeshUVSendWindow::TakeNextPage(const int32 TotalPages)
{
    if (TotalPages <= 0 || NextPageToSend >= TotalPages
        || GetPagesInFlight() >= FMD_ScrapeMeshUVSyncProtocol::MaximumPagesInFlight) { return INDEX_NONE; }
    const int32 Page = NextPageToSend++;
    PeakPagesInFlight = FMath::Max(PeakPagesInFlight, GetPagesInFlight());
    return Page;
}

bool FMD_ScrapeMeshUVSendWindow::Acknowledge(const int32 PageIndex)
{
    if (PageIndex != AcknowledgedPages || PageIndex >= NextPageToSend) { return false; }
    ++AcknowledgedPages;
    return true;
}

bool FMD_ScrapeMeshUVSendWindow::IsComplete(const int32 TotalPages) const
{
    return TotalPages > 0 && AcknowledgedPages == TotalPages;
}

void FMD_ScrapeMeshUVSnapshotAssembler::Reset()
{
    Descriptor = FMD_ScrapeMeshUVSnapshotDescriptor();
    // Reuse small delta buffers; release snapshot capacity without requesting rounded-up slack.
    if (Bytes.Max() > FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes) { Bytes.Empty(); }
    else { Bytes.Reset(); }
    NextPage = 0; bStarted = false;
}

bool FMD_ScrapeMeshUVSnapshotAssembler::Append(
    const FMD_ScrapeMeshUVSnapshotDescriptor& InDescriptor,
    const int32 PageIndex, const TConstArrayView<uint8> Payload)
{
    if (!FMD_ScrapeMeshUVSyncProtocol::IsDescriptorValid(InDescriptor)
        || PageIndex < 0 || PageIndex != NextPage
        || (bStarted && !(Descriptor == InDescriptor))) { return false; }
    const int32 PageCount = FMD_ScrapeMeshUVSyncProtocol::GetPageCount(InDescriptor);
    if (PageIndex >= PageCount) { return false; }
    const int32 ExpectedBytes = FMath::Min(FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes,
        InDescriptor.PayloadBytes - PageIndex * FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes);
    if (ExpectedBytes < 0 || Payload.Num() != ExpectedBytes
        || Payload.Num() > FMD_ScrapeMeshUVSyncProtocol::MaximumPayloadBytes) { return false; }
    if (!bStarted)
    {
        Descriptor = InDescriptor; bStarted = true;
        Bytes.Reserve(Descriptor.PayloadBytes);
    }
    Bytes.Append(Payload.GetData(), Payload.Num());
    ++NextPage;
    return true;
}

bool FMD_ScrapeMeshUVSnapshotAssembler::Finish(TArray<FMD_ReplicatedScrapeCellState>& OutStates) const
{
    OutStates.Reset();
    return bStarted && NextPage == FMD_ScrapeMeshUVSyncProtocol::GetPageCount(Descriptor)
        && FMD_ScrapeMeshUVSyncProtocol::DecodePayload(Descriptor, Bytes, OutStates);
}
