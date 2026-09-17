#pragma once
#include "CoreMinimal.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"

/** Metadata only. Both retained manifest generations pin immutable payload files. */
struct FMD_SaveSlotIndex
{
    uint64 Generation = 0;
    TArray<FMD_GameData> Auto;
    TArray<FMD_GameData> User;
};

struct FMD_PreparedSlotWrite;

/** Streaming local file store. Tests must supply an isolated Root and LegacyFile. */
class MARINEDEMOLITION_API FMD_SaveSlotStore
{
public:
    // 저장 파일을 생성하고 조회할 루트 디렉터리 지정
    FMD_SaveSlotStore(FString InRoot);
    static FMD_SaveSlotStore ForProject();
    bool ReadIndex(FMD_SaveSlotIndex& Out) const;
    bool ReadSlot(const FMD_GameData& Summary, FMD_GameData& Out) const;
    bool Save(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser) const;
    void SaveAsync(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser,
        TFunction<void(bool)> Completion) const;
    bool Remove(const FString& Name) const;
    static FMD_GameData MakeSummary(const FMD_GameData& Data);
private:
    bool PrepareSave(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser,
        FMD_PreparedSlotWrite& Out) const;
    FString Root;
    bool ReadManifest(const FString& Path, FMD_SaveSlotIndex& Out) const;
    bool WriteManifest(FMD_SaveSlotIndex& Index) const;

};
