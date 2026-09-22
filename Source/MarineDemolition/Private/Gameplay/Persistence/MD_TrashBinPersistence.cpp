#include "Gameplay/Persistence/MD_TrashBinPersistence.h"
#include "Gameplay/Persistence/MD_WorldActorPersistence.h"
#include "EngineUtils.h"
#include "World/Trash/MD_TrashBin.h"
#include "World/Trash/MD_TrashDispenser.h"

bool FMD_TrashBinPersistence::Capture(UWorld* World, TArray<FMD_TrashBinRecord>& OutRecords)
{
    // 이전 수집 기록이 새 결과에 섞이지 않도록 초기화
    OutRecords.Reset();
    // 현재 서버 월드에서만 통 상태 수집
    if (IsValid(World) == false || World->GetNetMode() == NM_Client)
    {
        // 수집 대상 부재 반환
        return false;
    }
    // 같은 영구 식별자의 중복 기록 차단
    TSet<FGuid> CapturedIds;
    // 구역까지 포함한 현재 보급기 식별자 색인
    TSet<FName> AvailableSources;
    // 통마다 모든 보급기를 다시 찾지 않도록 한 번만 수집
    for (TActorIterator<AMD_TrashDispenser> It(World); It; ++It)
    {
        // 서로 다른 구역의 동일 이름 보급기 분리
        AvailableSources.Add(FMD_WorldActorPersistence::GetActorKey(*It));
    }
    // 서버의 동적 통별 원장 수집
    for (TActorIterator<AMD_TrashBin> It(World); It; ++It)
    {
        // 배치 통과 보급 통을 구분할 현재 대상
        AMD_TrashBin* Bin = *It;
        // 보급 이력이 없는 기본 배치 통 제외
        if (Bin->GetSourceTrashDispenserName().IsNone() == true && IsValid(Cast<AMD_TrashDispenser>(Bin->GetOwner())) == false)
        {
            // 동적 재생성 대상이 아닌 통 처리 생략
            continue;
        }
        // 현재 액체 원장과 보급기 귀속을 기록할 결과
        FMD_TrashBinRecord Record;
        // 진행 중 거래와 중복 식별자 및 사라진 보급기 기록 차단
        if (Bin->BuildPersistenceRecord(Record) == false || CapturedIds.Contains(Record.StableBinId) == true
            || AvailableSources.Contains(FName(Record.SourceDispenserLevelPackage.ToString() + TEXT(":") + Record.SourceDispenserName.ToString())) == false)
        {
            // 일부 통만 포함하는 저장 차단
            OutRecords.Reset();
            // 전체 수집 실패 반환
            return false;
        }
        // 후속 통의 중복 식별자 검사 기준 추가
        CapturedIds.Add(Record.StableBinId);
        // 대용량 액체 원장의 불필요한 복사 없이 결과 이관
        OutRecords.Add(MoveTemp(Record));
    }
    // 모든 동적 통 수집 완료
    return true;
}

bool FMD_TrashBinPersistence::BuildRestoreContext(UWorld* World, TConstArrayView<FMD_TrashBinRecord> Records, FMD_TrashBinRestoreContext& OutContext)
{
    // 이전 월드의 보급기 및 통 참조 제거
    OutContext = FMD_TrashBinRestoreContext();
    // 제거된 월드 조회 차단
    if (IsValid(World) == false)
    {
        // 복원 문맥 생성 실패 반환
        return false;
    }
    // 신규 기록의 구역 및 이름 조합으로 보급기 조회
    TMap<FName, AMD_TrashDispenser*> SourcesByKey;
    // 기존 파일의 이름이 유일한 경우에만 허용할 호환 조회표
    TMap<FName, AMD_TrashDispenser*> LegacySourcesByName;
    // 보급기 배치를 한 번만 순회하여 두 형식의 색인 구성
    for (TActorIterator<AMD_TrashDispenser> It(World); It; ++It)
    {
        // 구역별로 유일한 신규 저장 식별자 등록
        SourcesByKey.Add(FMD_WorldActorPersistence::GetActorKey(*It), *It);
        // 기존 파일에서 같은 이름을 다른 구역으로 잘못 연결하지 않도록 검사
        if (LegacySourcesByName.Contains(It->GetFName()) == true)
        {
            // 중복 이름은 구역 정보 없는 기록에서만 복원 거부 대상으로 표시
            LegacySourcesByName[It->GetFName()] = nullptr;
        }
        else
        {
            // 유일한 이름의 기존 저장 호환 대상 등록
            LegacySourcesByName.Add(It->GetFName(), *It);
        }
    }
    // 저장 통마다 원래 보급기를 한 번만 해석
    for (const FMD_TrashBinRecord& Record : Records)
    {
        // 기록 없는 구역을 현재 통 위치로 추정하지 않는 호환 분기
        AMD_TrashDispenser* Dispenser = Record.SourceDispenserLevelPackage.IsNone() == true
            ? LegacySourcesByName.FindRef(Record.SourceDispenserName)
            : SourcesByKey.FindRef(FName(Record.SourceDispenserLevelPackage.ToString() + TEXT(":") + Record.SourceDispenserName.ToString()));
        // 잘못된 통과 중복 식별자 및 모호한 보급기 연결 차단
        if (Record.IsStructurallyValid() == false || IsValid(Dispenser) == false || OutContext.SourcesByBinId.Contains(Record.StableBinId) == true)
        {
            // 월드를 변경하기 전 실패 반환
            return false;
        }
        // 생성과 재사용 단계에서 반복 검색하지 않을 확정된 소유자 등록
        OutContext.SourcesByBinId.Add(Record.StableBinId, Dispenser);
    }
    // 실제 통 목록을 한 번만 순회하여 재사용 후보 구성
    for (TActorIterator<AMD_TrashBin> It(World); It; ++It)
    {
        // 여러 검사에서 사용할 현재 통
        AMD_TrashBin* Bin = *It;
        // 식별자 유효성과 재사용 판정에 사용할 현재 통의 영구 키
        FGuid ExistingId = Bin->GetStableBinId();
        // 기본 배치 통은 동적 복원 및 정리 대상에서 제외
        if (Bin->GetSourceTrashDispenserName().IsNone() == true)
        {
            // 저장 통 전체를 재순회하지 않고 영구 식별자 충돌 검사
            if (OutContext.SourcesByBinId.Contains(ExistingId) == true)
            {
                // 기본 배치 통을 저장 통으로 덮어쓰는 상황 차단
                return false;
            }
            // 일반 배치 통의 동적 추적 생략
            continue;
        }
        // 생존 목록 적용 후 남는 기존 동적 통 정리 대상 등록
        OutContext.ExistingDispensedBins.Add(Bin);
        // 재사용할 수 없는 영구 식별자 제외
        if (ExistingId.IsValid() == false || OutContext.AmbiguousExistingIds.Contains(ExistingId) == true)
        {
            // 이름이나 순서로 모호한 통을 선택하지 않도록 생략
            continue;
        }
        // 둘 이상의 현재 통이 같은 영구 식별자를 사용하는지 검사
        if (OutContext.ReusableBinsById.Contains(ExistingId) == true)
        {
            // 먼저 발견한 통의 임의 재사용 취소
            OutContext.ReusableBinsById.Remove(ExistingId);
            // 후속 중복 통도 같은 이유로 제외할 표시
            OutContext.AmbiguousExistingIds.Add(ExistingId);
            // 중복 식별자의 현재 통 처리 종료
            continue;
        }
        // 유일한 현재 통을 복원 재사용 후보로 등록
        OutContext.ReusableBinsById.Add(ExistingId, Bin);
    }
    // 복원 전에 필요한 모든 소유자와 통 검사 완료
    return true;
}

bool FMD_TrashBinPersistence::Restore(UWorld* World, TConstArrayView<FMD_TrashBinRecord> Records, FMD_TrashBinRestoreContext& Context)
{
    // 복원된 생존 통과 제거할 기존 통 구분
    TSet<TWeakObjectPtr<AMD_TrashBin>> RestoredBins;
    // 검증한 각 통의 원래 소유권과 원장 적용
    for (const FMD_TrashBinRecord& Record : Records)
    {
        // 사전 해석한 소유자를 영구 식별자로 즉시 조회
        AMD_TrashDispenser* Dispenser = Context.SourcesByBinId.FindRef(Record.StableBinId);
        // 사전 검사 이후 해제된 보급기로의 재등록 차단
        if (IsValid(Dispenser) == false)
        {
            // 복원 진행 실패 반환
            return false;
        }
        // 같은 영구 식별자를 가진 유일한 현재 통 조회
        AMD_TrashBin* Bin = Context.ReusableBinsById.FindRef(Record.StableBinId);
        // 클래스와 원래 보급기 귀속이 다른 통의 재사용 제외
        if (IsValid(Bin) == true && (Bin->GetClass() != Record.TrashBinClass.Get()
            || Bin->GetSourceTrashDispenserName() != Record.SourceDispenserName
            || (Bin->GetSourceTrashDispenserLevelPackage().IsNone() == false
                && FName(Bin->GetSourceTrashDispenserLevelPackage().ToString() + TEXT(":") + Bin->GetSourceTrashDispenserName().ToString()) != FMD_WorldActorPersistence::GetActorKey(Dispenser))))
        {
            // 원래 저장 클래스로 재생성할 대상으로 전환
            Bin = nullptr;
        }
        // 현재 월드에 없는 저장 통만 생성
        if (IsValid(Bin) == false)
        {
            // BeginPlay 이전에 원장을 적용하여 초기 액체 상태 손실 방지
            Bin = World->SpawnActorDeferred<AMD_TrashBin>(Record.TrashBinClass, Record.Transform, Dispenser, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            // 생성과 상태 적용 실패 검사
            if (IsValid(Bin) == false || Bin->RestorePersistenceRecord(Record) == false)
            {
                // 이번 요청에서 생성한 불완전한 통만 정리
                if (IsValid(Bin) == true)
                {
                    // 원장 없는 통이 남는 상황 방지
                    Bin->Destroy();
                }
                // 복원 실패 반환
                return false;
            }
            // 저장 원장으로 초기화한 통의 생성 완료
            Bin->FinishSpawning(Record.Transform);
            // 기본 루트 스케일과 곱해진 생성 크기를 절대 저장 좌표로 보정
            if (Bin->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics) == false)
            {
                // 잘못된 위치로 생성된 통 제거
                Bin->Destroy();
                // 실제 배치 실패 반환
                return false;
            }
        }
        // 기존 통은 원장 연출 갱신 전에 위치를 한 번만 복원
        else if (Bin->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics) == false
            || Bin->RestorePersistenceRecord(Record) == false)
        {
            // 기존 통을 파괴하지 않고 적용 실패 반환
            return false;
        }
        // 원래 보급기의 생존 제한 및 파괴 추적 원장에 연결
        if (Dispenser->RegisterDispensedBin(Bin) == false)
        {
            // 잘못된 보급기 연결 상태로 남는 통 정리
            Bin->Destroy();
            // 등록 실패 반환
            return false;
        }
        // 복원한 통을 후속 정리 대상에서 제외
        RestoredBins.Add(Bin);
    }
    // 저장 당시 존재하지 않았던 동적 통만 제거
    for (AMD_TrashBin* ExistingBin : Context.ExistingDispensedBins)
    {
        // 새 생존 목록에 포함되지 않은 유효 통 검사
        if (IsValid(ExistingBin) == true && RestoredBins.Contains(ExistingBin) == false)
        {
            // 과거 저장에 없던 동적 통 정리
            ExistingBin->Destroy();
        }
    }
    // 모든 통의 상태와 소유권 복원 완료
    return true;
}
