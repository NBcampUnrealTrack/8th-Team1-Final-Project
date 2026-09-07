#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncProtocol.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Misc/Compression.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"


namespace
{
    bool ValidateUniqueCellIndices(const TArray<FMD_ReplicatedScrapeCellState>& States)
    {
        bool bSorted = true;
        for (int32 Index = 1; Index < States.Num(); ++Index)
        {
            if (States[Index].EncodedCellIndex == States[Index - 1].EncodedCellIndex) { return false; }
            bSorted &= States[Index].EncodedCellIndex > States[Index - 1].EncodedCellIndex;
        }
        if (bSorted) { return true; }
        // Legacy unordered payloads remain accepted without a node-heavy TSet.
        TArray<uint32> Indices;
        Indices.Reserve(States.Num());
        for (const auto& State : States) { Indices.Add(State.EncodedCellIndex); }
        Indices.Sort();
        for (int32 Index = 1; Index < Indices.Num(); ++Index)
        { if (Indices[Index] == Indices[Index - 1]) { return false; } }
        return true;
    }
}

bool FMD_ScrapeMeshUVSnapshotDescriptor::operator==(const FMD_ScrapeMeshUVSnapshotDescriptor& Other) const
{
    return LayoutId == Other.LayoutId && Grid == Other.Grid && CarrierScale == Other.CarrierScale && Epoch == Other.Epoch
        && Serial == Other.Serial && VisualRevision == Other.VisualRevision
        && TransferId == Other.TransferId && TotalStates == Other.TotalStates
        && ContentHash == Other.ContentHash && PayloadBytes == Other.PayloadBytes
        && PayloadHash == Other.PayloadHash && bCompressed == Other.bCompressed
        && bFullSnapshot == Other.bFullSnapshot;
}

uint64 FMD_ScrapeMeshUVSyncProtocol::GetSnapshotMemoryReservation(const int32 ActiveCells)
{
    if (ActiveCells < 0 || ActiveCells > MaximumSnapshotStates) { return MAX_uint64; }
    return static_cast<uint64>(ActiveCells)
        * (sizeof(FMD_ReplicatedScrapeCellState) + 2 * BytesPerCell + sizeof(uint32)) + 128 * 1024;
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
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_EncodeSnapshot);
    OutPayload.Empty();
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
            const int32 RawBytes = OutPayload.Num();
            OutPayload.Empty(); // Planes now own the raw bytes; do not overlap a third full buffer.
            TArray<uint8> Compressed;
            Compressed.SetNumUninitialized(static_cast<int32>(Bound));
            int64 CompressedBytes = Bound;
            if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedBytes,
                Planes.GetData(), static_cast<int64>(Planes.Num()))
                && CompressedBytes > 0 && CompressedBytes < RawBytes)
            {
                // Avoid a large realloc retaining old/new buffers alongside planes.
                Planes.Empty();
                OutPayload.Reserve(static_cast<int32>(CompressedBytes));
                OutPayload.Append(Compressed.GetData(), static_cast<int32>(CompressedBytes));
                Descriptor.bCompressed = true;
            }
            else
            {
                Compressed.Empty(); Planes.Empty();
                EncodeStates(States, OutPayload);
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
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_DecodeSnapshot);
    OutStates.Empty();
    if (!IsDescriptorValid(Descriptor) || Payload.Num() != Descriptor.PayloadBytes
        || HashBytes(Payload) != Descriptor.PayloadHash) { return false; }
    if (!Descriptor.bCompressed)
    { return HashBytes(Payload) == Descriptor.ContentHash && DecodeStates(Payload, Descriptor.Grid, OutStates); }
    TArray<uint8> Planes;
    Planes.SetNumUninitialized(Descriptor.TotalStates * BytesPerCell);
    if (!FCompression::UncompressMemory(NAME_Zlib, Planes.GetData(), static_cast<int64>(Planes.Num()),
        Payload.GetData(), static_cast<int64>(Payload.Num()))) { return false; }
    OutStates.Reserve(Descriptor.TotalStates);
    uint32 ContentHash = 2166136261u;
    const uint32 PerFace = static_cast<uint32>(static_cast<int64>(Descriptor.Grid.X) * Descriptor.Grid.Y);
    for (int32 Cell = 0; Cell < Descriptor.TotalStates; ++Cell)
    {
        uint8 Bytes[BytesPerCell];
        for (int32 Byte = 0; Byte < BytesPerCell; ++Byte)
        {
            Bytes[Byte] = Planes[Byte * Descriptor.TotalStates + Cell];
            ContentHash = (ContentHash ^ Bytes[Byte]) * 16777619u;
        }
        uint32 Index = 0;
        for (int32 Byte = 0; Byte < 4; ++Byte)
        { Index |= static_cast<uint32>(Bytes[Byte]) << (8 * Byte); }
        if (Index >= PerFace) { OutStates.Empty(); return false; }
        OutStates.Emplace(Index, Bytes[4], Bytes[5], Bytes[6], Bytes[7], Bytes[8]);
    }
    // No interleaved full-size copy. Release planes before unordered duplicate scratch.
    Planes.Empty();
    if (ContentHash != Descriptor.ContentHash || !ValidateUniqueCellIndices(OutStates))
    { OutStates.Empty(); return false; }
    return true;
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
        || Bytes.Num() / BytesPerCell > PerFace
        || Bytes.Num() / BytesPerCell > MaximumSnapshotStates) { return false; }
    OutStates.Reserve(Bytes.Num() / BytesPerCell);
    for (int32 Offset = 0; Offset < Bytes.Num(); Offset += BytesPerCell)
    {
        uint32 Index = 0;
        for (int32 Byte = 0; Byte < 4; ++Byte)
        { Index |= static_cast<uint32>(Bytes[Offset + Byte]) << (Byte * 8); }
        if (Index >= static_cast<uint32>(PerFace))
        { OutStates.Reset(); return false; }
        OutStates.Emplace(Index, Bytes[Offset + 4], Bytes[Offset + 5],
            Bytes[Offset + 6], Bytes[Offset + 7], Bytes[Offset + 8]);
    }
    if (!ValidateUniqueCellIndices(OutStates)) { OutStates.Empty(); return false; }
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
