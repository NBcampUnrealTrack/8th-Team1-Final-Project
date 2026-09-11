#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_DayPersistenceSubsystem.generated.h"

struct FMD_WorldData;
class UMD_ScrapeSurfaceComponent;

// 계약(3일) 동안 Day 간 현장 상태를 보관/복원. GameInstance 수명이라 레벨 트래블에도 유지
UCLASS()
class MARINEDEMOLITION_API UMD_DayPersistenceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Day 종료 시 서버에서 호출. 현재 월드 상태를 기록
    void CaptureSnapshot(UWorld* World);

    // Day 시작 시 서버에서 호출. 스냅샷 적용 + 소량 악화. 적용했으면 true
    bool RestoreSnapshot(UWorld* World);

    // 파일 저장에 사용할 현재 서버 월드 상태를 수집하는 함수
    bool BuildWorldSaveData(UWorld* World, FMD_WorldData& OutWorldData);

    // 파일에서 불러온 월드 상태를 현재 서버 월드에 정확히 적용하는 함수
    bool ApplyWorldSaveData(UWorld* World, const FMD_WorldData& WorldData);

    /** Called before actors initialize, once per destination world. Never draws on clients. */
    void PrepareContaminationContext(UWorld* World, const FMD_WorldData* SavedWorld = nullptr,
        bool bNewContract = false, bool bDeferSavedGeneration = false);
    bool IsContaminationContextReady(const UWorld* World) const;
    bool TryGetContaminationSessionSeed(const UWorld* World, int32& OutSeed) const;
    bool TryGetSavedContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface,
        FMD_ContaminationGeneration& OutGeneration, bool& bOutLegacy) const;
    void ResetContaminationContext();
    bool ShouldDeferContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface) const;

    // 새 계약 시작 시 스냅샷 폐기
    void ClearSnapshot() { Snapshot = FMD_DaySnapshot(); }

    bool HasSnapshot() const { return Snapshot.bValid; }

private:
    UPROPERTY()
    FMD_DaySnapshot Snapshot;

    TWeakObjectPtr<UWorld> ContaminationContextWorld;
    bool bHasContaminationSessionSeed = false;
    int32 ContaminationSessionSeed = 0;
    bool bLegacyContaminationGeneration = false;
    bool bDeferSavedContaminationGeneration = false;
    // Only immutable descriptors are cached, never cell arrays or UObject references.
    TMap<FName, TMap<FName, FMD_ContaminationGeneration>> SavedContaminationGenerations;

    // 03 §3 허용 범위의 소량 악화: 지지대 1개 약화 + 오염 재부착 + 잔해 이동
    void ApplyOvernightWorsening(UWorld* World);
};
