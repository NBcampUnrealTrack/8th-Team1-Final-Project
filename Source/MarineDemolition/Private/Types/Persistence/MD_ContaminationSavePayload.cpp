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
    // 현재 형식의 다섯 오염 열 전체 필수
    constexpr uint8 RequiredFields = 31;
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
        // 현재 저장에서는 모든 열의 셀 개수가 일치해야 인코딩 허용
        if (Column->Num() != Count) return false;
        // 비정상 실수가 압축된 정상 파일로 고정되는 상황 차단
        for (float Value : *Column)
        {
            // 기존 값의 정밀도와 범위를 유지하면서 NaN과 무한대만 거부
            if (FMath::IsFinite(Value) == false)
            {
                // 원본 압축 저장을 교체하기 전 실패 반환
                return false;
            }
        }
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
    TArray<uint8> Raw;
    TArray<uint8> Compressed;
    for (int32 Start = 0; Start < Count; Start += CellsPerChunk)
    {
        const int32 Rows = FMath::Min(CellsPerChunk, Count - Start);
        Raw.SetNumUninitialized(Rows * BytesPerCell);
        FMemory::Memzero(Raw.GetData(), Raw.Num());
        // 현재 저장의 모든 열을 연속 배치해 실수 비트와 압축 효율 보존
        FMemory::Memcpy(Raw.GetData(), Indices.GetData() + Start, Rows * sizeof(int32));
        for (int32 Column = 0; Column < 5; ++Column)
        {
            // 모든 셀 열의 원본 비트를 압축 전 버퍼에 복사
            FMemory::Memcpy(Raw.GetData() + (Column + 1) * Rows * sizeof(float), Columns[Column]->GetData() + Start, Rows * sizeof(float));
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
        // 현재 형식의 모든 열에 필요한 셀 공간 확보
        Columns[Column]->Reserve(Num());
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
            // 현재 청크의 셀 개수만큼 열 확장
            Columns[Column]->AddUninitialized(Rows);
            // 필수 열의 저장 비트를 대응하는 셀 위치로 복원
            FMemory::Memcpy(Columns[Column]->GetData() + Start, Raw.GetData() + (Column + 1) * Rows * sizeof(float), Rows * sizeof(float));
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
        // 누락 열을 포함한 구형 압축 본문 거부
        || Fields != RequiredFields
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
                // CRC가 일치하더라도 의미 없는 오염 수치의 월드 유입 차단
                for (int32 Column = 0; Column < 5; ++Column)
                {
                    // 비정렬 압축 해제 버퍼의 실수를 안전하게 복사
                    float Value;
                    // 직렬화된 열 배치에 맞춰 셀 값 추출
                    FMemory::Memcpy(&Value, Raw.GetData() + ((Column + 1) * ExpectedRows + Row) * sizeof(float), sizeof(float));
                    // 한 번의 파일 읽기에서 모든 수치의 유한성 검증
                    if (FMath::IsFinite(Value) == false)
                    {
                        // 손상된 본문의 이후 적용 차단
                        Ar.SetError();
                        // 아카이브 오류를 유지한 채 직렬화 종료
                        return true;
                    }
                }
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
