#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_DayPersistenceSubsystem.generated.h"

struct FMD_GameData;

struct FMD_WorldData;

// 계약(3일) 동안 Day 간 현장 상태를 보관/복원. GameInstance 수명이라 레벨 트래블에도 유지
UCLASS()
class MARINEDEMOLITION_API UMD_DayPersistenceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    virtual void Deinitialize() override;

public:
    // Day 종료 시 서버에서 호출. 현재 월드 상태를 기록
    void CaptureSnapshot(UWorld* World);

    // Day 시작 시 서버에서 호출. 스냅샷 적용 + 소량 악화. 적용했으면 true
    bool RestoreSnapshot(UWorld* World);

    // 일차 스냅샷에 포함된 방문 구역의 선행 복원
    bool PrepareSnapshotStreaming(UWorld* World, bool& bReady);

    // 파일 저장에 사용할 현재 서버 월드 상태를 수집하는 함수
    bool BuildWorldSaveData(UWorld* World, FMD_WorldData& OutWorldData);

    // 파일에서 불러온 월드 상태를 현재 서버 월드에 정확히 적용하는 함수
    bool ApplyWorldSaveData(UWorld* World, const FMD_WorldData& WorldData);

    // 새 계약 시작 시 스냅샷 폐기
    void ClearSnapshot() { Snapshot = FMD_DaySnapshot(); }

    bool HasSnapshot() const { return Snapshot.bValid; }

private:
    void CaptureSnapshotInternal(UWorld* World, bool bCaptureTrashPlacements);
    bool ApplyWorldState(UWorld* World, const FMD_WorldData& WorldData,
        const TMap<FName, FMD_TrashGeneratorPlacement>* DayPlacements);

    UPROPERTY()
    FMD_DaySnapshot Snapshot;

    // 03 §3 허용 범위의 소량 악화: 지지대 약화 + 따개비 재부착 + 잔해 이동
    void ApplyOvernightWorsening(UWorld* World);
protected:
    // 소유한 기능 상태를 저장 이벤트의 구조체에 기록하는 함수
    UFUNCTION()
    void CollectGameData(UPARAM(ref) FMD_GameData& GameData, UPARAM(ref) bool& bWasSuccessful);

    // 전달된 저장 데이터에서 담당 태그의 필드를 찾아 직접 적용하는 함수
    UFUNCTION()
    void OverwriteGameData(const FMD_GameData& GameData, UPARAM(ref) bool& bWasSuccessful);

};
