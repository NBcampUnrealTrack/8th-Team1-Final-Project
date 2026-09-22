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
    // 디스크 읽기와 해시 검사는 작업 스레드에서 수행하고 결과는 게임 스레드로 전달
    void ReadIndexAsync(TFunction<void(FMD_SaveSlotIndex, bool)> Completion);
    bool ReadSlot(const FMD_GameData& Summary, FMD_GameData& Out) const;
    bool Save(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser) const;
    void SaveAsync(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser,
        TFunction<void(bool)> Completion) const;
    bool Remove(const FString& Name) const;
    // 호출자가 파일 작업을 직렬화하는 조건에서 인덱스 삭제를 비동기로 처리
    void RemoveAsync(FString Name, TFunction<void(bool)> Completion);
    static FMD_GameData MakeSummary(const FMD_GameData& Data);
private:
    bool PrepareSave(const FMD_GameData& Data, bool bAuto, int32 MaxAuto, int32 MaxUser,
        FMD_PreparedSlotWrite& Out) const;
    FString Root;
    bool ReadManifest(const FString& Path, FMD_SaveSlotIndex& Out) const;
    // 파일 존재 여부와 크기 및 해시를 객체 역직렬화 없이 확인
    static bool ReadManifestBytes(FString Path, TArray<uint8>& Out, bool& bExists);
    // 검증된 바이트의 엔진 구조체 해석을 게임 스레드에서 수행
    static bool DecodeManifest(FArchive& Reader, FMD_SaveSlotIndex& Out);
    // 직렬화 완료된 인덱스만 작업 스레드에서 내구성 쓰기와 교체 처리
    static bool WriteManifestBytes(FString StoreRoot, uint64 Generation, TArray<uint8>& Bytes);
    bool WriteManifest(FMD_SaveSlotIndex& Index) const;

};
