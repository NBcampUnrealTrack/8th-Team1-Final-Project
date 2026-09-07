#include "Types/Persistence/MD_ContaminationSavePayload.h"
#include "Containers/BitArray.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    constexpr int32 CellsPerChunk = 4096;
    constexpr int32 BytesPerCell = 24;
    constexpr int32 MaximumChunkBytes = CellsPerChunk * BytesPerCell;
    constexpr uint8 RequiredFields = 3;
    struct FChunk
    {
        uint32 Crc = 0;
        int32 RawBytes = 0;
        bool bCompressed = false;
        TArray<uint8> Bytes;
    };
    bool ReadChunk(const FChunk& Chunk, TArray<uint8>& Out)
    {
        if (Chunk.RawBytes <= 0 || Chunk.RawBytes > MaximumChunkBytes
            || Chunk.RawBytes % BytesPerCell != 0 || Chunk.Bytes.IsEmpty()
            || Chunk.Bytes.Num() > Chunk.RawBytes) return false;
        Out.Reserve(Chunk.RawBytes);
        Out.SetNumUninitialized(Chunk.RawBytes);
        if (Chunk.bCompressed)
        {
            if (!FCompression::UncompressMemory(NAME_Zlib, Out.GetData(), Out.Num(),
                Chunk.Bytes.GetData(), Chunk.Bytes.Num())) return false;
        }
        else if (Chunk.Bytes.Num() == Out.Num())
        {
            FMemory::Memcpy(Out.GetData(), Chunk.Bytes.GetData(), Out.Num());
        }
        else return false;
        return FCrc::MemCrc32(Out.GetData(), Out.Num()) == Chunk.Crc;
    }
}
struct FMD_ContaminationSaveStorage
{
    int32 CellCount = 0;
    uint8 Fields = RequiredFields;
    int32 MinimumIndex = MAX_int32;
    int32 MaximumIndex = MIN_int32;
    bool bUniqueBoundedIndices = true;
    TArray<FChunk> Chunks;
};
bool FMD_ContaminationSavePayload::Encode(const TArray<int32>& Indices,
    const TArray<float>& Initial, const TArray<float>& Remaining,
    const TArray<float>& Transferred, const TArray<float>& Pattern, const TArray<float>& Covered)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ContaminationSave_Encode);
    const int32 Count = Indices.Num();
    const TArray<float>* Columns[] = { &Initial, &Remaining, &Transferred, &Pattern, &Covered };
    if (Count > FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount
        || Initial.Num() != Count || Remaining.Num() != Count) return false;
    for (const TArray<float>* Column : Columns)
    {
        if (!Column->IsEmpty() && Column->Num() != Count) return false;
    }
    if (Count == 0)
    {
        Storage.Reset();
        return true;
    }
    auto NewStorage = MakeShared<FMD_ContaminationSaveStorage, ESPMode::ThreadSafe>();
    NewStorage->CellCount = Count;
    bool bIncreasing = true;
    for (int32 Row = 0; Row < Count; ++Row)
    {
        const int32 Index = Indices[Row];
        NewStorage->MinimumIndex = FMath::Min(NewStorage->MinimumIndex, Index);
        NewStorage->MaximumIndex = FMath::Max(NewStorage->MaximumIndex, Index);
        bIncreasing &= Row == 0 || Index > Indices[Row - 1];
    }
    NewStorage->bUniqueBoundedIndices = NewStorage->MinimumIndex >= 0
        && NewStorage->MaximumIndex < FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount;
    if (!bIncreasing && NewStorage->bUniqueBoundedIndices)
    {
        TBitArray<> Seen(false, NewStorage->MaximumIndex + 1);
        for (const int32 Index : Indices)
        {
            if (Seen[Index]) { NewStorage->bUniqueBoundedIndices = false; break; }
            Seen[Index] = true;
        }
    }
    NewStorage->Fields = 0;
    for (int32 Column = 0; Column < 5; ++Column)
    {
        if (!Columns[Column]->IsEmpty()) NewStorage->Fields |= 1 << Column;
    }
    TArray<uint8> Raw;
    TArray<uint8> Compressed;
    for (int32 Start = 0; Start < Count; Start += CellsPerChunk)
    {
        const int32 Rows = FMath::Min(CellsPerChunk, Count - Start);
        Raw.SetNumUninitialized(Rows * BytesPerCell);
        FMemory::Memzero(Raw.GetData(), Raw.Num());
        // Column-major bytes preserve every float bit; optional absent fields retain explicit flags.
        FMemory::Memcpy(Raw.GetData(), Indices.GetData() + Start, Rows * sizeof(int32));
        for (int32 Column = 0; Column < 5; ++Column)
        {
            if (!Columns[Column]->IsEmpty())
            {
                FMemory::Memcpy(Raw.GetData() + (Column + 1) * Rows * sizeof(float),
                    Columns[Column]->GetData() + Start, Rows * sizeof(float));
            }
        }
        FChunk& Chunk = NewStorage->Chunks.AddDefaulted_GetRef();
        Chunk.RawBytes = Raw.Num();
        Chunk.Crc = FCrc::MemCrc32(Raw.GetData(), Raw.Num());
        int32 CompressedBytes = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
        Compressed.SetNumUninitialized(CompressedBytes);
        if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedBytes,
            Raw.GetData(), Raw.Num()) && CompressedBytes < Raw.Num())
        {
            Chunk.bCompressed = true;
            Chunk.Bytes.Reserve(CompressedBytes);
            Chunk.Bytes.Append(Compressed.GetData(), CompressedBytes);
        }
        else Chunk.Bytes = Raw;
    }
    Storage = MoveTemp(NewStorage);
    return true;
}
bool FMD_ContaminationSavePayload::Decode(TArray<int32>& Indices, TArray<float>& Initial,
    TArray<float>& Remaining, TArray<float>& Transferred, TArray<float>& Pattern,
    TArray<float>& Covered) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ContaminationSave_Decode);
    TArray<float>* Columns[] = { &Initial, &Remaining, &Transferred, &Pattern, &Covered };
    Indices.Empty(Num());
    for (TArray<float>* Column : Columns) Column->Empty();
    if (!Storage) return true;
    for (int32 Column = 0; Column < 5; ++Column)
    {
        if (Storage->Fields & (1 << Column)) Columns[Column]->Reserve(Num());
    }
    TArray<uint8> Raw;
    for (const FChunk& Chunk : Storage->Chunks)
    {
        if (!ReadChunk(Chunk, Raw))
        {
            Indices.Empty();
            for (TArray<float>* Column : Columns) Column->Empty();
            return false;
        }
        const int32 Rows = Raw.Num() / BytesPerCell;
        const int32 Start = Indices.AddUninitialized(Rows);
        FMemory::Memcpy(Indices.GetData() + Start, Raw.GetData(), Rows * sizeof(int32));
        for (int32 Column = 0; Column < 5; ++Column)
        {
            if (Storage->Fields & (1 << Column))
            {
                Columns[Column]->AddUninitialized(Rows);
                FMemory::Memcpy(Columns[Column]->GetData() + Start,
                    Raw.GetData() + (Column + 1) * Rows * sizeof(float), Rows * sizeof(float));
            }
        }
    }
    return Indices.Num() == Num();
}
bool FMD_ContaminationSavePayload::ValidateIndices(const int32 MaximumCellCount) const
{
    // Chunks are immutable. CRC and ID uniqueness were established at encode/load;
    // repeatedly validating a snapshot must not decompress the complete world again.
    return !Storage || (MaximumCellCount > 0 && Storage->bUniqueBoundedIndices
        && Num() <= MaximumCellCount && Storage->MinimumIndex >= 0
        && Storage->MaximumIndex < MaximumCellCount);
}
int32 FMD_ContaminationSavePayload::Num() const { return Storage ? Storage->CellCount : 0; }
uint64 FMD_ContaminationSavePayload::GetAllocatedBytes() const
{
    if (!Storage) return 0;
    uint64 Bytes = sizeof(FMD_ContaminationSaveStorage) + Storage->Chunks.GetAllocatedSize();
    for (const FChunk& Chunk : Storage->Chunks) Bytes += Chunk.Bytes.GetAllocatedSize();
    return Bytes;
}
bool FMD_ContaminationSavePayload::IsSharedWith(const FMD_ContaminationSavePayload& Other) const
{
    return Storage && Storage == Other.Storage;
}
bool FMD_ContaminationSavePayload::Serialize(FArchive& Ar)
{
    uint8 Version = 1;
    int32 Count = Num();
    uint8 Fields = Storage ? Storage->Fields : RequiredFields;
    int32 ChunkCount = Storage ? Storage->Chunks.Num() : 0;
    Ar << Version << Count << Fields << ChunkCount;
    if (Ar.IsError() || Version != 1 || Count < 0
        || Count > FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount
        || (Fields & RequiredFields) != RequiredFields || (Fields & ~31) != 0
        || ChunkCount != FMath::DivideAndRoundUp(Count, CellsPerChunk))
    {
        Ar.SetError();
        return true;
    }
    TSharedPtr<FMD_ContaminationSaveStorage, ESPMode::ThreadSafe> Loaded;
    if (Ar.IsLoading())
    {
        Loaded = MakeShared<FMD_ContaminationSaveStorage, ESPMode::ThreadSafe>();
        Loaded->CellCount = Count;
        Loaded->Fields = Fields;
        Loaded->Chunks.SetNum(ChunkCount);
    }
    TArray<uint8> Raw;
    TBitArray<> Seen;
    if (Loaded && Count) Seen.Init(false, FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount);
    for (int32 Index = 0; Index < ChunkCount; ++Index)
    {
        // Some archive scalar operators (notably bool) write back even when saving.
        // Serialize a local header so async writers never mutate a shared immutable chunk.
        FChunk Header;
        if (!Loaded)
        {
            Header.RawBytes = Storage->Chunks[Index].RawBytes;
            Header.Crc = Storage->Chunks[Index].Crc;
            Header.bCompressed = Storage->Chunks[Index].bCompressed;
        }
        FChunk& Chunk = Loaded ? Loaded->Chunks[Index] : Header;
        int32 Size = Loaded ? 0 : Storage->Chunks[Index].Bytes.Num();
        Ar << Chunk.RawBytes << Chunk.Crc << Chunk.bCompressed << Size;
        const int32 ExpectedRows = FMath::Min(CellsPerChunk, Count - Index * CellsPerChunk);
        if (Ar.IsError() || Chunk.RawBytes != ExpectedRows * BytesPerCell
            || Size <= 0 || Size > Chunk.RawBytes)
        {
            Ar.SetError();
            return true;
        }
        if (Loaded)
        {
            Chunk.Bytes.Reserve(Size);
            Chunk.Bytes.SetNumUninitialized(Size);
        }
        uint8* Bytes = Loaded ? Chunk.Bytes.GetData()
            : const_cast<uint8*>(Storage->Chunks[Index].Bytes.GetData());
        Ar.Serialize(Bytes, Size);
        if (Ar.IsError() || (Loaded && !ReadChunk(Chunk, Raw)))
        {
            Ar.SetError();
            return true;
        }
        if (Loaded)
        {
            for (int32 Row = 0; Row < ExpectedRows; ++Row)
            {
                int32 CellIndex;
                FMemory::Memcpy(&CellIndex, Raw.GetData() + Row * sizeof(int32), sizeof(int32));
                Loaded->MinimumIndex = FMath::Min(Loaded->MinimumIndex, CellIndex);
                Loaded->MaximumIndex = FMath::Max(Loaded->MaximumIndex, CellIndex);
                if (CellIndex < 0 || CellIndex >= Seen.Num())
                    Loaded->bUniqueBoundedIndices = false;
                else if (Seen[CellIndex])
                    Loaded->bUniqueBoundedIndices = false;
                else Seen[CellIndex] = true;
            }
        }
    }
    if (Loaded)
    {
        if (Count) Storage = MoveTemp(Loaded);
        else Storage.Reset();
    }
    return true;
}
