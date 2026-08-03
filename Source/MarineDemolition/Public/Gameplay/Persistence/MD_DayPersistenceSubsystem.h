#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_DayPersistenceSubsystem.generated.h"

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

    // 새 계약 시작 시 스냅샷 폐기
    void ClearSnapshot() { Snapshot = FMD_DaySnapshot(); }

    bool HasSnapshot() const { return Snapshot.bValid; }

private:
    UPROPERTY()
    FMD_DaySnapshot Snapshot;

    // 03 §3 허용 범위의 소량 악화: 지지대 1개 약화 + 오염 재부착 + 잔해 이동
    void ApplyOvernightWorsening(UWorld* World);
};
