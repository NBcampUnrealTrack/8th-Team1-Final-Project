#pragma once

#include "CoreMinimal.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_DayPersistenceSubsystem.generated.h"

struct FMD_GameData;

struct FMD_WorldData;
class UMD_ScrapeSurfaceComponent;

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

    /** Called before actors initialize, once per destination world. Never draws on clients. */
    void PrepareContaminationContext(UWorld* World, const FMD_WorldData* SavedWorld = nullptr,
        bool bNewContract = false, bool bDeferSavedGeneration = false);
    bool IsContaminationContextReady(const UWorld* World) const;
    bool TryGetContaminationSessionSeed(const UWorld* World, int32& OutSeed) const;
    // 현재 표면 식별자에 대응하는 저장된 생성 정보 조회
    bool TryGetSavedContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface, FMD_ContaminationGeneration& OutGeneration) const;
    void ResetContaminationContext();
    bool TryGetSavedContaminationLayout(const UMD_ScrapeSurfaceComponent* Surface, FGuid& OutLayout) const;
    bool ShouldDeferContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface) const;
    const FMD_WallContaminationRecord* FindDeferredContaminationRecord(const UMD_ScrapeSurfaceComponent* Surface) const;

    // 새 계약 시작 시 스냅샷 폐기
    void ClearSnapshot() { Snapshot = FMD_DaySnapshot(); }

    bool HasSnapshot() const { return Snapshot.bValid; }

private:
    UPROPERTY()
    FMD_DaySnapshot Snapshot;

    TWeakObjectPtr<UWorld> ContaminationContextWorld;
    bool bHasContaminationSessionSeed = false;
    // 기존 파일의 단순 이름과 새 파일의 구역 포함 식별자 구분
    bool bUseQualifiedActorNames = false;
    int32 ContaminationSessionSeed = 0;
    bool bDeferSavedContaminationGeneration = false;
    // Compact records share immutable payload bytes only during covered saved-world startup.
    TMap<FName, TMap<FName, FMD_WallContaminationRecord>> DeferredContaminationRecords;
    // Generation/layout descriptors contain no UObject references.
    TMap<FName, TMap<FName, FMD_ContaminationGeneration>> SavedContaminationGenerations;
    TMap<FName, TMap<FName, FGuid>> SavedContaminationLayouts;

    // 03 §3 허용 범위의 소량 악화: 지지대 1개 약화 + 오염 재부착 + 잔해 이동
    void ApplyOvernightWorsening(UWorld* World);
protected:
    // 액터와 컴포넌트 이름이 모두 일치하는 현재 표면 기록의 복원 대상 조회
    UMD_ScrapeSurfaceComponent* FindSavedScrapeSurface(const FMD_WallContaminationRecord& Record, TMap<FName, TArray<UMD_ScrapeSurfaceComponent*>>& SurfacesByOwnerName);

    // 소유한 기능 상태를 저장 이벤트의 구조체에 기록하는 함수
    UFUNCTION()
    void CollectGameData(UPARAM(ref) FMD_GameData& GameData, UPARAM(ref) bool& bWasSuccessful);

    // 전달된 저장 데이터에서 담당 태그의 필드를 찾아 직접 적용하는 함수
    UFUNCTION()
    void OverwriteGameData(const FMD_GameData& GameData, UPARAM(ref) bool& bWasSuccessful);

};
