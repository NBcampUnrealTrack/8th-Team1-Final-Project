#pragma once

#include "CoreMinimal.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"

class AMD_TrashDispenser;

// 사전 검사 결과를 재사용하여 통과 원래 보급기를 일대일 연결하는 복원 문맥
struct FMD_TrashBinRestoreContext
{
    // 저장 통 식별자로 한 번만 해석한 원래 보급기 조회
    TMap<FGuid, AMD_TrashDispenser*> SourcesByBinId;
    // 중복되지 않은 현재 통의 재사용 후보 조회
    TMap<FGuid, AMD_TrashBin*> ReusableBinsById;
    // 같은 식별자를 공유하는 통의 임의 재사용 방지
    TSet<FGuid> AmbiguousExistingIds;
    // 저장에 없는 통을 복원 완료 후 정리할 대상
    TArray<AMD_TrashBin*> ExistingDispensedBins;
};

// 파일과 일차 복원에서 공유하는 통의 수집 및 보급기 연결 처리
class MARINEDEMOLITION_API FMD_TrashBinPersistence
{
public:
    // 현재 통의 액체 원장과 원래 보급기 구역을 함께 수집
    static bool Capture(UWorld* World, TArray<FMD_TrashBinRecord>& OutRecords);

    // 월드 변경 전에 구역별 보급기와 재사용 통을 한 번씩 색인
    static bool BuildRestoreContext(UWorld* World, TConstArrayView<FMD_TrashBinRecord> Records, FMD_TrashBinRestoreContext& OutContext);

    // 사전 검사로 해석한 원래 보급기에 저장 통을 복원 및 재등록
    static bool Restore(UWorld* World, TConstArrayView<FMD_TrashBinRecord> Records, FMD_TrashBinRestoreContext& Context);
};
