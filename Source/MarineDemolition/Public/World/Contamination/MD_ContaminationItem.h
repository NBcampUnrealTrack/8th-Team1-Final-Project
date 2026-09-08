#pragma once

#include "CoreMinimal.h"
#include "World/Contamination/MD_ScrapedDebrisBase.h"
#include "Types/Scrape/MD_ContaminationPendingDrop.h"
#include "MD_ContaminationItem.generated.h"

/**
 * 헤라로 긁어낸 오염물 덩어리 (기획 02 §1 청소 요소 표)
 *
 * 대상은 "벽·바닥·설비 표면의 바이오필름·도장·점액층"이다. 따개비와 달리 보통벽
 * 제한이 없어 철거 대상 벽에도 붙는다.
 *
 * 조각벽과 달리 칸이 갈리지 않는다. 오염물은 언제나 오염된 쓰레기다.
 * 물리 표현은 한 품목 단위를 유지하고 서버에는 정확한 질량과 보상 출처를 보관한다.
 */
UCLASS()
class MARINEDEMOLITION_API AMD_ContaminationItem : public AMD_ScrapedDebrisBase
{
	GENERATED_BODY()

public:
	AMD_ContaminationItem();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void InitializeRecoveryRecord(FMD_ContaminationPendingDrop&& Record);
    bool CaptureRecoveryRecord(FMD_ContaminationPendingDrop& Out) const;
    void RestoreSavedVelocity();
    uint64 GetRecoveryAllocatedSize() const { return sizeof(RecoveryRecord) + RecoveryRecord.RecoveryLots.GetAllocatedSize(); }

private:
    UPROPERTY() FMD_ContaminationPendingDrop RecoveryRecord;
    UPROPERTY() FGuid PhysicalItemId;
    bool bHasRecoveryRecord = false;
};
