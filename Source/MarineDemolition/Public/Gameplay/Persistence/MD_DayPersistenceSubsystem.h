#pragma once

#include "CoreMinimal.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_DayPersistenceSubsystem.generated.h"

class UMD_ScrapeSurfaceComponent;

/**
 * 세이브 삭제 후 남은 오염 세션 시드 컨텍스트. 스크레이프 표면이 결정적 생성 시드를 조회한다.
 * 헤라질 시스템 제거가 머지되면 이 서브시스템도 함께 삭제한다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_DayPersistenceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** Called before actors initialize, once per destination world. Never draws on clients. */
    void PrepareContaminationContext(UWorld* World);
    bool IsContaminationContextReady(const UWorld* World) const;
    bool TryGetContaminationSessionSeed(const UWorld* World, int32& OutSeed) const;
    void ResetContaminationContext();

    // 저장 기록이 더 이상 없으므로 항상 조회 실패를 돌려주는 호환 API
    bool TryGetSavedContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface, FMD_ContaminationGeneration& OutGeneration) const;
    bool TryGetSavedContaminationLayout(const UMD_ScrapeSurfaceComponent* Surface, FGuid& OutLayout) const;
    bool ShouldDeferContaminationGeneration(const UMD_ScrapeSurfaceComponent* Surface) const;
    const FMD_WallContaminationRecord* FindDeferredContaminationRecord(const UMD_ScrapeSurfaceComponent* Surface) const;

private:
    TWeakObjectPtr<UWorld> ContaminationContextWorld;
    bool bHasContaminationSessionSeed = false;
    int32 ContaminationSessionSeed = 0;
};
