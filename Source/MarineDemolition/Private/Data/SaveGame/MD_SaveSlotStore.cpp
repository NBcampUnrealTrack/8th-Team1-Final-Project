#include "Data/SaveGame/MD_SaveSlotStore.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/ObjectVersion.h"
#include "UObject/UnrealType.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

struct FMD_PreparedSlotWrite
{
    FString Root;
    FString PayloadPath;
    FString ManifestPath;
    FString ManifestTemporary;
    TArray<uint8> PayloadHeader;
    TArray<uint8> Metadata;
    TArray<uint8> Manifest;
    TArray<FMD_ContaminationSavePayload> Payloads;
};

namespace
{
    constexpr uint32 FileMagic = 0x3253444d;
    constexpr int32 DigestBytes = 20;
    constexpr int32 MaxStoredSaveSlots = 1024;
    constexpr int64 MaxMetadataBytes = 16 * 1024 * 1024;
    bool EngineHeader(FArchive& Ar, const uint32 ExpectedKind)
    {
        uint32 Magic = FileMagic, Kind = ExpectedKind;
        int32 Version = 1, Format = static_cast<int32>(ECustomVersionSerializationFormat::Latest);
        FPackageFileVersion PackageVersion = GPackageFileUEVersion;
        FEngineVersion EngineVersion = FEngineVersion::Current();
        FCustomVersionContainer Versions = FCurrentCustomVersions::GetAll();
        Ar << Magic << Version << Kind;
        if (Ar.IsError() || Magic != FileMagic || Version != 1 || Kind != ExpectedKind) return false;
        Ar << PackageVersion;
        Ar << EngineVersion;
        Ar << Format;
        if (Format != static_cast<int32>(ECustomVersionSerializationFormat::Latest)) return false;
        Versions.Serialize(Ar, static_cast<ECustomVersionSerializationFormat>(Format));
        if (Ar.IsLoading())
        {
            Ar.SetUEVer(PackageVersion);
            Ar.SetEngineVer(EngineVersion);
            Ar.SetCustomVersions(Versions);
        }
        return !Ar.IsError();
    }
    bool HashRange(FArchive& Reader, int64 Bytes, uint8 (&Digest)[DigestBytes])
    {
        if (Bytes < 0 || Bytes > Reader.TotalSize()) return false;
        Reader.Seek(0);
        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(1024 * 1024);
        FSHA1 Hash;
        while (Bytes > 0 && !Reader.IsError())
        {
            const int64 Count = FMath::Min<int64>(Bytes, Buffer.Num());
            Reader.Serialize(Buffer.GetData(), Count);
            Hash.Update(Buffer.GetData(), static_cast<uint32>(Count));
            Bytes -= Count;
        }
        Hash.Final();
        Hash.GetHash(Digest);
        return !Reader.IsError();
    }
    bool SealFile(const FString& Path)
    {
        uint8 Digest[DigestBytes];
        {
            TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
            if (!Reader || !HashRange(*Reader, Reader->TotalSize(), Digest)) return false;
        }
        TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_Append));
        if (!Writer) return false;
        Writer->Serialize(Digest, sizeof(Digest));
        const bool bClosed = Writer->Close() && !Writer->IsError();
        Writer.Reset();
        if (!bClosed) return false;
        TUniquePtr<IFileHandle> DurableHandle(
            FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, true, false));
        return DurableHandle && DurableHandle->Flush(true);
    }
    bool VerifyFile(FArchive& Reader)
    {
        if (Reader.TotalSize() <= DigestBytes) return false;
        uint8 Actual[DigestBytes], Expected[DigestBytes];
        if (!HashRange(Reader, Reader.TotalSize() - DigestBytes, Actual)) return false;
        Reader.Serialize(Expected, sizeof(Expected));
        Reader.Seek(0);
        return !Reader.IsError() && FMemory::Memcmp(Actual, Expected, sizeof(Actual)) == 0;
    }
    class FSlotArchive : public FObjectAndNameAsStringProxyArchive
    {
    public:
        FSlotArchive(FArchive& Inner)
            // 객체 이름과 경로의 엔진 표준 직렬화 사용
            : FObjectAndNameAsStringProxyArchive(Inner, true)
        {
            ArMaxSerializeSize = 128 * 1024 * 1024;
        }
        virtual void Serialize(void* Data, int64 Bytes) override
        {
            const FProperty* Property = GetSerializedProperty();
            const bool bFirstScalar = Property != LastSerializedProperty;
            LastSerializedProperty = Property;
            FObjectAndNameAsStringProxyArchive::Serialize(Data, Bytes);
            if (!IsLoading() || !bFirstScalar || Bytes != sizeof(int32)) return;
            const FArrayProperty* Array = CastField<FArrayProperty>(Property);
            if (!Array) return;
            int32 Limit = (64 * 1024 * 1024) / FMath::Max(1, Array->Inner->GetElementSize());
            if (Property->GetOwnerStruct() == FMD_WallContaminationRecord::StaticStruct())
                Limit = 2 * 1024 * 1024;
            int32& Count = *static_cast<int32*>(Data);
            if (Count < 0 || Count > Limit)
            {
                Count = 0; // reject corrupted sizes before reflected array allocation
                SetCriticalError();
            }
        }

    private:
        const FProperty* LastSerializedProperty = nullptr;
    };

    bool IsPayloadId(const FString& Id)
    {
        FGuid Guid;
        return Id.Len() == 32 && FGuid::ParseExact(Id, EGuidFormats::Digits, Guid) && Guid.IsValid();
    }
    bool IsSummaryValid(const FMD_GameData& Row)
    {
        // 현재 스키마와 실제 개별 파일 식별자를 가진 요약만 허용
        return Row.bIsSlotSummary == true && Row.IsEmpty() == false && Row.SchemaVersion == FMD_GameData::CurrentSchemaVersion
            && IsPayloadId(Row.SlotPayloadId) == true;
    }
    bool WriteRawFile(const FString& Path, const TArray<uint8>& Bytes)
    {
        TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_NoReplaceExisting));
        if (!Writer) return false;
        Writer->Serialize(const_cast<uint8*>(Bytes.GetData()), Bytes.Num());
        return !Writer->IsError() && Writer->Close();
    }
    bool ExecutePreparedWrite(FMD_PreparedSlotWrite& Prepared)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_SaveSlots_AsyncDiskWrite);
        // Only byte buffers, immutable chunk references and paths cross to this worker.
        if (!IFileManager::Get().MakeDirectory(*Prepared.Root, true)) return false;
        {
            TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(
                *Prepared.PayloadPath, FILEWRITE_NoReplaceExisting));
            if (!Writer) return false;
            Writer->Serialize(Prepared.PayloadHeader.GetData(), Prepared.PayloadHeader.Num());
            int32 Size = Prepared.Metadata.Num();
            *Writer << Size;
            Writer->Serialize(Prepared.Metadata.GetData(), Size);
            int32 Count = Prepared.Payloads.Num();
            *Writer << Count;
            for (FMD_ContaminationSavePayload& Payload : Prepared.Payloads) Payload.Serialize(*Writer);
            if (Writer->IsError() || !Writer->Close()) return false;
        }
        if (!SealFile(Prepared.PayloadPath)
            || !WriteRawFile(Prepared.ManifestTemporary, Prepared.Manifest)
            || !SealFile(Prepared.ManifestTemporary)) return false;
        {
            TUniquePtr<FArchive> Check(IFileManager::Get().CreateFileReader(*Prepared.ManifestTemporary));
            if (!Check || !VerifyFile(*Check)) return false;
        }
        return IFileManager::Get().Move(*Prepared.ManifestPath, *Prepared.ManifestTemporary,
            true, false, false, true);
    }
    bool BuildManifestBytes(FMD_SaveSlotIndex& Index, TArray<uint8>& Out)
    {
        FMemoryWriter Writer(Out, true);
        if (!EngineHeader(Writer, 2)) return false;
        Writer << Index.Generation;
        FSlotArchive Ar(Writer);
        for (TArray<FMD_GameData>* Rows : { &Index.Auto, &Index.User })
        {
            int32 Count = Rows->Num();
            Ar << Count;
            for (FMD_GameData& Row : *Rows)
                FMD_GameData::StaticStruct()->SerializeItem(Ar, &Row, nullptr);
        }
        return !Ar.IsError() && Out.Num() <= MaxMetadataBytes - DigestBytes;
    }
}
FMD_SaveSlotStore::FMD_SaveSlotStore(FString InRoot)
    // 상대 경로 영향을 받지 않는 저장 루트 보관
    : Root(FPaths::ConvertRelativePathToFull(InRoot))
{
}
FMD_SaveSlotStore FMD_SaveSlotStore::ForProject()
{
    // 최초 배포 스키마의 독립 저장 경로로 개발용 인덱스와 충돌 방지
    return FMD_SaveSlotStore(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames/MarineDemolition/Schema1")));
}
FMD_GameData FMD_SaveSlotStore::MakeSummary(const FMD_GameData& Data)
{
    FMD_GameData Row;
    Row.SchemaVersion = Data.SchemaVersion;
    Row.UniqueName = Data.UniqueName;
    Row.MapPackagePath = Data.MapPackagePath;
    Row.SaveTime = Data.SaveTime;
    // 전체 월드 데이터를 읽지 않고 목록에서 저장 장면 표시
    Row.ThumbnailImage = Data.ThumbnailImage;
    Row.bIsSlotSummary = true;
    for (const auto& Field : Data.DataFields)
    {
        if (Field.Key != FMD_WorldData::DataFieldKey) Row.DataFields.Add(Field.Key, Field.Value);
    }
    return Row;
}
bool FMD_SaveSlotStore::ReadManifest(const FString& Path, FMD_SaveSlotIndex& Out) const
{
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent));
    if (!Reader || Reader->TotalSize() > MaxMetadataBytes || !VerifyFile(*Reader)
        || !EngineHeader(*Reader, 2)) return false;
    FMD_SaveSlotIndex Loaded;
    *Reader << Loaded.Generation;
    if (!Loaded.Generation) return false;
    FSlotArchive Ar(*Reader);
    for (TArray<FMD_GameData>* Rows : { &Loaded.Auto, &Loaded.User })
    {
        int32 Count = 0;
        Ar << Count;
        if (Ar.IsError() || Count < 0 || Count > MaxStoredSaveSlots) return false;
        Rows->SetNum(Count);
        TSet<FString> Names;
        for (FMD_GameData& Row : *Rows)
        {
            FMD_GameData::StaticStruct()->SerializeItem(Ar, &Row, nullptr);
            if (Ar.IsError() || !IsSummaryValid(Row) || Names.Contains(Row.UniqueName)) return false;
            Names.Add(Row.UniqueName);
        }
    }
    if (Ar.IsError() || Ar.Tell() != Reader->TotalSize() - DigestBytes) return false;
    Out = MoveTemp(Loaded);
    return true;
}
bool FMD_SaveSlotStore::ReadIndex(FMD_SaveSlotIndex& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_SaveSlots_ReadMetadata);
    FMD_SaveSlotIndex A, B;
    const FString APath = FPaths::Combine(Root, TEXT("index-a.mdindex"));
    const FString BPath = FPaths::Combine(Root, TEXT("index-b.mdindex"));
    const bool bA = ReadManifest(APath, A), bB = ReadManifest(BPath, B);
    if (bA || bB)
    {
        Out = bA && (!bB || A.Generation >= B.Generation) ? MoveTemp(A) : MoveTemp(B);
        return true;
    }
    // A damaged new index must not resurrect deleted rows from the untouched old file.
    if (IFileManager::Get().FileExists(*APath) == true || IFileManager::Get().FileExists(*BPath) == true) return false;
    Out = FMD_SaveSlotIndex();
    return true;
}

bool FMD_SaveSlotStore::ReadSlot(const FMD_GameData& Summary, FMD_GameData& Out) const
{
    // 지원하는 스키마와 개별 파일 식별자를 확인한 뒤 슬롯 접근
    if (IsSummaryValid(Summary) == false)
    {
        // 현재 형식이 아닌 슬롯 요약 거부
        return false;
    }
    // 검증된 식별자로 읽을 개별 저장 파일 경로 구성
    FString Path = FPaths::Combine(Root, Summary.SlotPayloadId + TEXT(".mdslot"));
    // 파일 수명을 관리할 읽기 아카이브 생성
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent));
    // 파일이 없거나 열리지 않은 경우 적용 데이터 생성 차단
    if (Reader.Get() == nullptr)
    {
        // 개별 저장 파일 열기 실패 반환
        return false;
    }
    // 손상된 메타데이터의 과도한 직렬화 메모리 사용 제한
    Reader->ArMaxSerializeSize = 128 * 1024 * 1024;
    // 파일 해시와 현재 파일 헤더 형식 확인
    if (VerifyFile(*Reader) == false || EngineHeader(*Reader, 1) == false)
    {
        // 손상되거나 다른 형식인 파일 거부
        return false;
    }
    // 제한된 크기로 읽을 메타데이터 영역 길이 준비
    int32 Size = 0;
    // 저장된 메타데이터 바이트 길이 조회
    *Reader << Size;
    // 파일 범위를 벗어나거나 상한을 초과한 메타데이터 길이 차단
    if (Reader->IsError() == true || Size < 0 || Size > MaxMetadataBytes
        || Size > Reader->TotalSize() - Reader->Tell() - DigestBytes)
    {
        // 메타데이터 영역 손상의 실패 반환
        return false;
    }
    // 직렬화된 저장 구조체를 읽을 제한된 메모리 버퍼
    TArray<uint8> Metadata;
    // 검증한 바이트 수만큼 버퍼 할당
    Metadata.SetNumUninitialized(Size);
    // 파일의 메타데이터를 메모리로 읽기
    Reader->Serialize(Metadata.GetData(), Size);
    // 압축 셀 본문과 분리한 메타데이터 아카이브 구성
    FMemoryReader MetadataReader(Metadata, true);
    // 현재 파일 헤더에 기록된 엔진 직렬화 버전 적용
    MetadataReader.SetUEVer(Reader->UEVer());
    // 구조체 해석에 필요한 엔진 버전 전달
    MetadataReader.SetEngineVer(Reader->EngineVer());
    // 엔진 자료형의 사용자 정의 직렬화 버전 전달
    MetadataReader.SetCustomVersions(Reader->GetCustomVersions());
    // UObject 참조를 경로로 읽는 현재 슬롯 아카이브 구성
    FSlotArchive Ar(MetadataReader);
    // 검증 완료 전 호출자의 데이터를 변경하지 않는 임시 저장 구조체
    FMD_GameData Loaded;
    // 태그 기반 구조체 메타데이터 복원
    FMD_GameData::StaticStruct()->SerializeItem(Ar, &Loaded, nullptr);
    // 잘못된 메타데이터 영역과 지원하지 않는 스키마를 셀 본문 읽기 전에 차단
    if (Ar.IsError() == true || Ar.Tell() != Size || Loaded.SchemaVersion != FMD_GameData::CurrentSchemaVersion)
    {
        // 메타데이터 형식 또는 버전 불일치 반환
        return false;
    }
    // 별도 저장된 압축 셀 본문을 연결할 월드 필드 조회
    FInstancedStruct* Field = Loaded.DataFields.Find(FMD_WorldData::DataFieldKey);
    // 현재 파일 형식에서 필수인 월드 필드 타입 확인
    if (Field == nullptr || Field->IsValid() == false || Field->GetScriptStruct() != FMD_WorldData::StaticStruct())
    {
        // 월드 필드가 없거나 다른 타입인 파일 거부
        return false;
    }
    // 타입을 검증한 월드 필드의 본문 연결 대상 조회
    FMD_WorldData* World = Field->GetMutablePtr<FMD_WorldData>();
    // 월드의 표면 기록 수와 비교할 본문 개수 준비
    int32 Count = 0;
    // 저장된 압축 셀 본문 개수 조회
    *Reader << Count;
    // 표면 기록과 본문 개수가 다른 파일의 부분 복원 차단
    if (Reader->IsError() == true || Count != World->WallContaminations.Num())
    {
        // 압축 본문 개수 불일치 반환
        return false;
    }
    // 각 표면 기록에 해당하는 압축 셀 본문 복원
    for (FMD_WallContaminationRecord& Record : World->WallContaminations)
    {
        // 표면별 압축 본문 읽기
        Record.CellPayload.Serialize(*Reader);
    }
    // 파일 경계와 요약 식별 정보 및 전체 저장 구조 검증
    if (Reader->IsError() == true || Reader->Tell() != Reader->TotalSize() - DigestBytes || Loaded.UniqueName != Summary.UniqueName
        || Loaded.MapPackagePath != Summary.MapPackagePath || Loaded.SaveTime != Summary.SaveTime || Loaded.IsValidForLoad() == false)
    {
        // 손상 또는 다른 슬롯 본문의 적용 거부
        return false;
    }
    // 모든 검사를 통과한 현재 형식의 데이터만 호출자에 전달
    Out = MoveTemp(Loaded);
    // 개별 슬롯 읽기 성공 반환
    return true;
}
bool FMD_SaveSlotStore::WriteManifest(FMD_SaveSlotIndex& Index) const
{
    if (Index.Generation == MAX_uint64) return false;
    ++Index.Generation;
    const FString Name = Index.Generation % 2 ? TEXT("index-a.mdindex") : TEXT("index-b.mdindex");
    const FString Destination = FPaths::Combine(Root, Name);
    const FString Temporary = Destination + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    {
        TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Temporary, FILEWRITE_NoReplaceExisting));
        if (!Writer || !EngineHeader(*Writer, 2)) return false;
        *Writer << Index.Generation;
        FSlotArchive Ar(*Writer);
        for (TArray<FMD_GameData>* Rows : { &Index.Auto, &Index.User })
        {
            int32 Count = Rows->Num();
            Ar << Count;
            for (FMD_GameData& Row : *Rows) FMD_GameData::StaticStruct()->SerializeItem(Ar, &Row, nullptr);
        }
        if (Ar.IsError() || Ar.Tell() > MaxMetadataBytes - DigestBytes || !Writer->Close()) return false;
    }
    FMD_SaveSlotIndex Verified;
    if (!SealFile(Temporary) || !ReadManifest(Temporary, Verified)) return false;
    // The other complete generation survives any interruption or failed replacement.
    return IFileManager::Get().Move(*Destination, *Temporary, true, false, false, true);
}
bool FMD_SaveSlotStore::PrepareSave(const FMD_GameData& Data, bool bAuto, int32 MaxAuto,
    int32 MaxUser, FMD_PreparedSlotWrite& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_SaveSlots_PrepareMetadata);
    if (!Data.IsValidForLoad() || MaxAuto < 0 || MaxUser < 0
        || MaxAuto > MaxStoredSaveSlots || MaxUser > MaxStoredSaveSlots || (bAuto ? MaxAuto : MaxUser) == 0) return false;
    FMD_SaveSlotIndex Index;
    if (!ReadIndex(Index) || Index.Generation == MAX_uint64) return false;
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Out.Root = Root;
    Out.PayloadPath = FPaths::Combine(Root, Id + TEXT(".mdslot"));
    FMD_GameData Metadata = Data;
    if (FInstancedStruct* Field = Metadata.DataFields.Find(FMD_WorldData::DataFieldKey))
    {
        FMD_WorldData* World = Field->GetMutablePtr<FMD_WorldData>();
        if (!World) return false;
        Out.Payloads.Reserve(World->WallContaminations.Num());
        for (FMD_WallContaminationRecord& Record : World->WallContaminations)
        {
            if (!Record.CompactCells()) return false;
            Out.Payloads.Add(Record.CellPayload);
            Record.CellPayload = FMD_ContaminationSavePayload();
        }
    }
    {
        FMemoryWriter Header(Out.PayloadHeader, true);
        if (!EngineHeader(Header, 1)) return false;
        FMemoryWriter Writer(Out.Metadata, true);
        FSlotArchive Ar(Writer);
        FMD_GameData::StaticStruct()->SerializeItem(Ar, &Metadata, nullptr);
        if (Ar.IsError() || Out.Metadata.Num() > MaxMetadataBytes) return false;
    }
    FMD_GameData Summary = MakeSummary(Data);
    Summary.SlotPayloadId = Id;
    TArray<FMD_GameData>& Rows = bAuto ? Index.Auto : Index.User;
    Rows.RemoveAll([&](const FMD_GameData& Row) { return Row.UniqueName == Data.UniqueName; });
    Rows.Insert(MoveTemp(Summary), 0);
    if (Index.Auto.Num() > MaxAuto) Index.Auto.SetNum(MaxAuto);
    if (Index.User.Num() > MaxUser) Index.User.SetNum(MaxUser);
    ++Index.Generation;
    Out.ManifestPath = FPaths::Combine(Root, Index.Generation % 2 ? TEXT("index-a.mdindex") : TEXT("index-b.mdindex"));
    Out.ManifestTemporary = Out.ManifestPath + TEXT(".") + Id + TEXT(".tmp");
    return BuildManifestBytes(Index, Out.Manifest);
}
bool FMD_SaveSlotStore::Save(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser) const
{
    FMD_PreparedSlotWrite Prepared;
    return PrepareSave(Data, bAuto, MaxAuto, MaxUser, Prepared) && ExecutePreparedWrite(Prepared);
}
void FMD_SaveSlotStore::SaveAsync(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser,
    TFunction<void(bool)> Completion) const
{
    auto Prepared = MakeShared<FMD_PreparedSlotWrite, ESPMode::ThreadSafe>();
    if (!PrepareSave(Data, bAuto, MaxAuto, MaxUser, *Prepared))
    {
        AsyncTask(ENamedThreads::GameThread, [Completion = MoveTemp(Completion)]() mutable
        {
            Completion(false);
        });
        return;
    }
    Async(EAsyncExecution::ThreadPool, [Prepared, Completion = MoveTemp(Completion)]() mutable
    {
        const bool bSuccess = ExecutePreparedWrite(*Prepared);
        AsyncTask(ENamedThreads::GameThread, [Completion = MoveTemp(Completion), bSuccess]() mutable
        {
            Completion(bSuccess);
        });
    });
}
bool FMD_SaveSlotStore::Remove(const FString& Name) const
{
    FMD_SaveSlotIndex Index;
    if (Name.IsEmpty() || !ReadIndex(Index)) return false;
    Index.Auto.RemoveAll([&](const FMD_GameData& Row) { return Row.UniqueName == Name; });
    Index.User.RemoveAll([&](const FMD_GameData& Row) { return Row.UniqueName == Name; });
    if (!IFileManager::Get().MakeDirectory(*Root, true)) return false;
    return WriteManifest(Index);
}
