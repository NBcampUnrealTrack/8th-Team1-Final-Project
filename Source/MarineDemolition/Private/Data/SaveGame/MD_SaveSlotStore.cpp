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
#include "UObject/StrongObjectPtr.h"
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
        FSlotArchive(FArchive& Inner, bool bInSummary = false)
            : FObjectAndNameAsStringProxyArchive(Inner, true), bSummary(bInSummary)
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
            if (Property->GetOwnerStruct() == UMD_SaveGame::StaticClass()) Limit = MaxStoredSaveSlots;
            if (Property->GetOwnerStruct() == FMD_WallContaminationRecord::StaticStruct())
                Limit = 2 * 1024 * 1024;
            int32& Count = *static_cast<int32*>(Data);
            if (Count < 0 || Count > Limit)
            {
                Count = 0; // reject corrupted sizes before reflected array allocation
                SetCriticalError();
            }
        }
        virtual FString GetArchiveName() const override
        {
            return bSummary ? TEXT("MDLegacySlotIndex") : TEXT("MDSlotArchive");
        }
        virtual bool ShouldSkipProperty(const FProperty* Property) const override
        {
            return (bSummary && Property
                && (Property->GetOwnerStruct() == FMD_WorldData::StaticStruct()
                    || (Property->GetOwnerStruct() == FMD_GameData::StaticStruct()
                        && Property->GetFName() == TEXT("PlayerFields"))))
                || FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property);
        }
    private:
        bool bSummary;
        const FProperty* LastSerializedProperty = nullptr;
    };
    bool ReadLegacyHeader(FArchive& Ar)
    {
        int32 Magic = 0, Version = 0;
        Ar << Magic << Version;
        if (Magic != 0x53415647 || Version < 1 || Version > 3) return false;
        FPackageFileVersion PackageVersion;
        if (Version >= 3) Ar << PackageVersion;
        else
        {
            int32 UE4 = 0;
            Ar << UE4;
            PackageVersion = FPackageFileVersion::CreateUE4Version(UE4);
        }
        FEngineVersion EngineVersion;
        Ar << EngineVersion;
        Ar.SetUEVer(PackageVersion);
        Ar.SetEngineVer(EngineVersion);
        if (Version >= 2)
        {
            int32 Format = 0;
            Ar << Format;
            if (Format <= static_cast<int32>(ECustomVersionSerializationFormat::Unknown)
                || Format > static_cast<int32>(ECustomVersionSerializationFormat::Latest)) return false;
            FCustomVersionContainer Versions;
            Versions.Serialize(Ar, static_cast<ECustomVersionSerializationFormat>(Format));
            Ar.SetCustomVersions(Versions);
        }
        FString ClassName;
        Ar << ClassName;
        return !Ar.IsError() && ClassName == UMD_SaveGame::StaticClass()->GetPathName();
    }
    bool IsPayloadId(const FString& Id)
    {
        FGuid Guid;
        return Id.Len() == 32 && FGuid::ParseExact(Id, EGuidFormats::Digits, Guid) && Guid.IsValid();
    }
    bool IsSummaryValid(const FMD_GameData& Row)
    {
        return Row.bIsSlotSummary && !Row.IsEmpty() && Row.SchemaVersion <= FMD_GameData::CurrentSchemaVersion
            && (IsPayloadId(Row.SlotPayloadId) || (Row.SlotPayloadId.IsEmpty()
                && Row.LegacyDataOffset > 0 && Row.LegacyDataBytes > 0
                && Row.LegacyDataOffset <= Row.LegacyFileBytes - Row.LegacyDataBytes));
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
FMD_SaveSlotStore::FMD_SaveSlotStore(FString InRoot, FString InLegacyFile)
    : Root(FPaths::ConvertRelativePathToFull(InRoot)), LegacyFile(FPaths::ConvertRelativePathToFull(InLegacyFile)) {}
FMD_SaveSlotStore FMD_SaveSlotStore::ForProject()
{
    const FString Saves = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
    return FMD_SaveSlotStore(FPaths::Combine(Saves, TEXT("MarineDemolitionSlotsV1")),
        FPaths::Combine(Saves, TEXT("MarineDemolitionGame.sav")));
}
FMD_GameData FMD_SaveSlotStore::MakeSummary(const FMD_GameData& Data)
{
    FMD_GameData Row;
    Row.SchemaVersion = Data.SchemaVersion;
    Row.UniqueName = Data.UniqueName;
    Row.MapPackagePath = Data.MapPackagePath;
    Row.SaveTime = Data.SaveTime;
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
    if (IFileManager::Get().FileExists(*APath) || IFileManager::Get().FileExists(*BPath)) return false;
    if (IFileManager::Get().FileExists(*LegacyFile)) return ReadLegacyIndex(Out);
    Out = FMD_SaveSlotIndex();
    return true;
}
bool FMD_SaveSlotStore::ReadLegacyIndex(FMD_SaveSlotIndex& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_SaveSlots_ReadLegacyMetadata);
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*LegacyFile, FILEREAD_Silent));
    if (!Reader) return false;
    Reader->ArMaxSerializeSize = 128 * 1024 * 1024;
    if (!ReadLegacyHeader(*Reader)) return false;
    FSlotArchive Ar(*Reader, true);
    TStrongObjectPtr<UMD_SaveGame> Legacy(NewObject<UMD_SaveGame>());
    // Engine owns all versioned class/array tags. The archive seeks over world properties;
    // native game-data serialization only records each small row's original byte range.
    Legacy->Serialize(Ar);
    if (Ar.IsError() || Ar.Tell() != Ar.TotalSize()
        || Legacy->SchemaVersion > FMD_GameData::CurrentSchemaVersion
        || Legacy->AutoSaveList.Num() > MaxStoredSaveSlots || Legacy->UserSaveList.Num() > MaxStoredSaveSlots) return false;
    FMD_SaveSlotIndex Loaded;
    for (const bool bAuto : { true, false })
    {
        TArray<FMD_GameData>& Input = bAuto ? Legacy->AutoSaveList : Legacy->UserSaveList;
        TArray<FMD_GameData>& Output = bAuto ? Loaded.Auto : Loaded.User;
        for (FMD_GameData& Data : Input)
        {
            FMD_GameData Row = MakeSummary(Data);
            Row.LegacyDataOffset = Data.LegacyDataOffset;
            Row.LegacyDataBytes = Data.LegacyDataBytes;
            Row.LegacyFileBytes = Reader->TotalSize();
            Row.LegacyFileTime = IFileManager::Get().GetTimeStamp(*LegacyFile);
            Data.DataFields.Remove(FMD_WorldData::DataFieldKey);
            if (Data.MigrateToCurrentVersion() && Data.IsValidForLoad() && IsSummaryValid(Row))
                Output.Add(MoveTemp(Row));
        }
    }
    Out = MoveTemp(Loaded);
    return true;
}
bool FMD_SaveSlotStore::ReadSlot(const FMD_GameData& Summary, FMD_GameData& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_SaveSlots_ReadOne);
    if (!IsSummaryValid(Summary)) return false;
    const bool bLegacy = Summary.SlotPayloadId.IsEmpty();
    const FString Path = bLegacy ? LegacyFile : FPaths::Combine(Root, Summary.SlotPayloadId + TEXT(".mdslot"));
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent));
    if (!Reader) return false;
    Reader->ArMaxSerializeSize = 128 * 1024 * 1024;
    if (bLegacy)
    {
        if (Reader->TotalSize() != Summary.LegacyFileBytes
            || IFileManager::Get().GetTimeStamp(*LegacyFile) != Summary.LegacyFileTime
            || !ReadLegacyHeader(*Reader)) return false;
        Reader->Seek(Summary.LegacyDataOffset);
    }
    else if (!VerifyFile(*Reader) || !EngineHeader(*Reader, 1)) return false;
    FMD_GameData Loaded;
    if (bLegacy)
    {
        FSlotArchive Ar(*Reader);
        FMD_GameData::StaticStruct()->SerializeItem(Ar, &Loaded, nullptr);
        if (Ar.IsError()) return false;
    }
    else
    {
        int32 Size = 0;
        *Reader << Size;
        if (Reader->IsError() || Size < 0 || Size > MaxMetadataBytes
            || Size > Reader->TotalSize() - Reader->Tell() - DigestBytes) return false;
        TArray<uint8> Metadata;
        Metadata.SetNumUninitialized(Size);
        Reader->Serialize(Metadata.GetData(), Size);
        FMemoryReader MetadataReader(Metadata, true);
        MetadataReader.SetUEVer(Reader->UEVer());
        MetadataReader.SetEngineVer(Reader->EngineVer());
        MetadataReader.SetCustomVersions(Reader->GetCustomVersions());
        FSlotArchive Ar(MetadataReader);
        FMD_GameData::StaticStruct()->SerializeItem(Ar, &Loaded, nullptr);
        if (Ar.IsError() || Ar.Tell() != Size) return false;
        FInstancedStruct* Field = Loaded.DataFields.Find(FMD_WorldData::DataFieldKey);
        FMD_WorldData* World = Field ? Field->GetMutablePtr<FMD_WorldData>() : nullptr;
        int32 Count = 0;
        *Reader << Count;
        if (Reader->IsError() || Count != (World ? World->WallContaminations.Num() : 0)) return false;
        if (World)
            for (FMD_WallContaminationRecord& Record : World->WallContaminations)
                Record.CellPayload.Serialize(*Reader);
    }
    const int64 ExpectedEnd = bLegacy ? Summary.LegacyDataOffset + Summary.LegacyDataBytes : Reader->TotalSize() - DigestBytes;
    if (Reader->IsError() || Reader->Tell() != ExpectedEnd || Loaded.UniqueName != Summary.UniqueName
        || Loaded.MapPackagePath != Summary.MapPackagePath || Loaded.SaveTime != Summary.SaveTime
        || !Loaded.MigrateToCurrentVersion() || !Loaded.IsValidForLoad()) return false;
    Out = MoveTemp(Loaded);
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
