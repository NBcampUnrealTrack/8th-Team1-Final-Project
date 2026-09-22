#include "Gameplay/Persistence/MD_WorldActorPersistence.h"
#include "World/Sector/MD_SubLevelLoadTrigger.h"
#include "EngineUtils.h"
#include "World/Sample/MD_QuestInteractionPickup.h"
#include "World/Interaction/MD_DataDumpTerminal.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Trash/MD_TrashItem.h"
#include "World/Wall/MD_CuttableWall.h"

void FMD_WorldActorPersistence::CaptureActor(AActor* Actor, FMD_WorldActorRecord& Out)
{
    // PIE 실행 번호를 제거하여 재실행 후에도 같은 스트리밍 레벨 식별
    Out.LevelPackage = FName(UWorld::RemovePIEPrefix(Actor->GetLevel()->GetOutermost()->GetName()));
    // 해당 레벨에서 액터를 찾을 실제 객체 이름 보관
    Out.ActorName = Actor->GetFName();
    // 사라진 동적 물품을 다시 생성할 클래스 보관
    Out.ActorClass = Actor->GetClass();
    // 물리 운반 이후의 실제 위치와 회전 및 크기 보관
    Out.Transform = Actor->GetActorTransform();
}

FName FMD_WorldActorPersistence::GetActorKey(AActor* Actor)
{
    // 구역 인스턴스와 액터 이름을 하나의 기존 기록 키로 구성
    return FName(UWorld::RemovePIEPrefix(Actor->GetLevel()->GetOutermost()->GetName()) + TEXT(":") + Actor->GetName());
}

ULevel* FMD_WorldActorPersistence::FindLevel(UWorld* World, FName Package)
{
    // 현재 로드된 레벨에서만 저장 대상을 검색
    for (ULevel* Level : World->GetLevels())
    {
        // 다른 스트리밍 레벨의 동일 이름 액터와 혼동 방지
        if (IsValid(Level) == true && FName(UWorld::RemovePIEPrefix(Level->GetOutermost()->GetName())) == Package)
        {
            // 정확한 배치 레벨 반환
            return Level;
        }
    }
    // 아직 로드되지 않았거나 제거된 레벨의 복원 거부
    return nullptr;
}

AActor* FMD_WorldActorPersistence::FindActor(UWorld* World, const FMD_WorldActorRecord& Record)
{
    // 저장된 배치 레벨로 검색 범위 제한
    ULevel* Level = FindLevel(World, Record.LevelPackage);
    // 레벨이 없는 상태에서 전역 객체 검색으로 잘못 복원하는 상황 방지
    if (IsValid(Level) == false)
    {
        // 대상 레벨 부재 반환
        return nullptr;
    }
    // 저장된 레벨과 객체 이름의 정확한 조합 조회
    return FindObjectFast<AActor>(Level, Record.ActorName);
}

AActor* FMD_WorldActorPersistence::RestoreActor(UWorld* World, const FMD_WorldActorRecord& Record, TFunctionRef<bool(AActor*)> ApplyState)
{
    // 기존 물품을 우선 재사용하여 외부 참조 유지
    AActor* Actor = FindActor(World, Record);
    // 이번 요청이 생성한 액터만 생성 완료 및 실패 정리 대상으로 구분
    bool bSpawned = IsValid(Actor) == false;
    // 현재 월드에 없는 동적 물품만 생성
    if (bSpawned == true)
    {
        // 기존 저장의 객체 이름과 배치 레벨을 유지할 생성 옵션
        FActorSpawnParameters Params;
        // 저장 당시 이름으로 재접속 및 재저장 식별 유지
        Params.Name = Record.ActorName;
        // 이름 충돌을 임의 이름 변경으로 숨기지 않도록 설정
        Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
        // 원래 스트리밍 레벨에 물품 생성
        Params.OverrideLevel = FindLevel(World, Record.LevelPackage);
        // 물품의 저장 위치를 임의 충돌 조정으로 변경하지 않도록 설정
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Construction Script와 BeginPlay가 저장값을 읽도록 생성 완료 연기
        Params.bDeferConstruction = true;
        // 클래스 기본 루트 크기와 저장 크기의 이중 곱셈 방지
        Params.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
        // 원래 클래스와 레벨 및 이름을 보존한 지연 생성
        Actor = World->SpawnActor<AActor>(Record.ActorClass, Record.Transform, Params);
    }
    // 이름이 같지만 다른 클래스인 액터와 실패한 생성을 거부
    if (IsValid(Actor) == false || Actor->IsActorBeingDestroyed() == true || Actor->GetClass() != Record.ActorClass.Get())
    {
        // 복원 실패를 상위 요청에 전달
        return nullptr;
    }
    // 새 물품은 생성 스크립트와 초기 이벤트 전에 퀘스트 및 품목 상태 적용
    if (ApplyState(Actor) == false)
    {
        // 실패한 요청이 미완성 액터를 월드에 남기는 상황 차단
        if (bSpawned == true)
        {
            // 이번 요청이 생성한 대상만 제거
            Actor->Destroy();
        }
        // 개별 상태 복원 실패 전파
        return nullptr;
    }
    // 저장값을 준비한 새 액터만 엔진의 생성 수명주기 완료
    if (bSpawned == true)
    {
        // 저장 스케일을 유지하면서 Construction Script와 BeginPlay 실행
        Actor->FinishSpawning(Record.Transform, false, nullptr, ESpawnActorScaleMethod::OverrideRootScale);
        // 생성 스크립트의 자기 제거와 외형 재설정 이후 저장 상태 최종 확인
        if (IsValid(Actor) == false || Actor->IsActorBeingDestroyed() == true || ApplyState(Actor) == false)
        {
            // 생성 도중 남은 불완전한 새 액터 정리
            if (IsValid(Actor) == true && Actor->IsActorBeingDestroyed() == false)
            {
                // 실패한 복원 대상만 제거
                Actor->Destroy();
            }
            // 엔진 생성 완료 이후의 복원 실패 전파
            return nullptr;
        }
    }
    // 물리 물품을 저장 위치에 정확히 배치
    Actor->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics);
    // 클라이언트 위치 갱신 요청
    Actor->ForceNetUpdate();
    // 개별 상태를 적용할 물품 반환
    return Actor;
}

bool FMD_WorldActorPersistence::Capture(UWorld* World, FMD_WorldActorSnapshot& Out)
{
    // 유효한 서버 월드의 상태만 기록
    if (IsValid(World) == false || World->GetNetMode() == NM_Client)
    {
        // 비권위 수집 실패 반환
        return false;
    }
    // 이전 수집의 물품이 남지 않는 새 기록 생성
    Out = FMD_WorldActorSnapshot();
    // 빈 생존 목록도 모든 물품을 수거한 유효 상태로 기록
    Out.bCaptured = true;
    // 지속 대상의 실제 런타임 상태 수집
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        // 여러 분류에서 사용할 현재 액터
        AActor* Actor = *It;
        // 운반 가능한 퀘스트 물품 확인
        AMD_QuestItem* QuestItem = Cast<AMD_QuestItem>(Actor);
        // 샘플을 포함한 모든 퀘스트 물품을 동일한 목록에 기록
        if (IsValid(QuestItem) == true)
        {
            // 퀘스트 물품의 생성과 배치 기록 확보
            FMD_QuestItemRecord& Record = Out.QuestItems.AddDefaulted_GetRef();
            // 공통 액터 상태 수집
            CaptureActor(Actor, Record);
            // 제출 대상 퀘스트 연결 보존
            Record.QuestDefinitionId = QuestItem->QuestDefinitionId;
            // 샘플 전용 등급과 손상 상태 수집 대상 조회
            AMD_SampleItem* Sample = Cast<AMD_SampleItem>(Actor);
            // 일반 물품에 없는 샘플 상태만 추가 수집
            if (IsValid(Sample) == true)
            {
                // 원래 납품 등급 보존
                Record.Grade = Sample->Grade;
                // 손상 조건 보존
                Record.bDamaged = Sample->bDamaged;
                // 미운반 샘플의 오납품과 운반한 샘플의 자격 유실 방지
                Record.bHasBeenCarried = Sample->HasBeenCarried();
            }
            // 같은 물품의 다른 분류 중복 기록 방지
            continue;
        }
        // 일반 쓰레기 분류 확인
        AMD_TrashItem* Trash = Cast<AMD_TrashItem>(Actor);
        // 수거되지 않은 쓰레기의 실제 종류와 위치 기록
        if (IsValid(Trash) == true)
        {
            // 쓰레기 상태 기록 확보
            FMD_TrashItemRecord& Record = Out.TrashItems.AddDefaulted_GetRef();
            // 공통 액터 상태 수집
            CaptureActor(Actor, Record);
            // 분리수거 종류 보존
            Record.Kind = Trash->GetTrashKindId();
            // 선택된 외형 보존
            Record.Variant = Trash->GetTrashVariantId();
            // 다른 분류 검사 생략
            continue;
        }
        // 획득 시 사라지는 퀘스트 상호작용 물품 확인
        AMD_QuestInteractionPickup* Pickup = Cast<AMD_QuestInteractionPickup>(Actor);
        // 아직 남아 있는 물품만 생존 목록에 기록
        if (IsValid(Pickup) == true)
        {
            // 상호작용 물품의 배치 정보 확보
            FMD_QuestPickupRecord& Record = Out.Pickups.AddDefaulted_GetRef();
            // 공통 액터 상태 수집
            CaptureActor(Actor, Record);
            // 연결된 퀘스트 보존
            Record.QuestDefinitionId = Pickup->QuestDefinitionId;
            // 진행 대상 단계 보존
            Record.PhaseIndex = Pickup->PhaseIndex;
            // 인스턴스 표시 이름 보존
            Record.DisplayName = Pickup->DisplayName;
            // 다른 분류 검사 생략
            continue;
        }
        // 절단 가능한 벽의 진행 기록 대상 조회
        AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(Actor);
        // 이미 철거된 벽은 기존 생존 목록 처리에 맡김
        if (IsValid(Wall) == true && Wall->CanCut() == true)
        {
            // 완료 전 절단 상태 기록 확보
            FMD_WallProgressRecord& Record = Out.WallProgress.AddDefaulted_GetRef();
            // 배치 레벨과 벽 식별자 보관
            CaptureActor(Actor, Record);
            // 실제 누적 절단량 보존
            Record.CutSeconds = Wall->GetPersistentCutSeconds();
            // 다른 분류 검사 생략
            continue;
        }
        // 수도관과 배선의 상태 기록 대상 조회
        AMD_WallUtility* Utility = Cast<AMD_WallUtility>(Actor);
        // 파생 설비의 타이머를 포함한 기록 수집
        if (IsValid(Utility) == true)
        {
            // 설비별 부가 상태까지 기록
            Out.Utilities.Add(Utility->BuildPersistenceRecord());
            // 다른 분류 검사 생략
            continue;
        }
        // 완료된 카트리지 단말의 재사용 방지 상태 조회
        AMD_DataDumpTerminal* Terminal = Cast<AMD_DataDumpTerminal>(Actor);
        // 단말 사용 완료와 전원 잠금을 함께 기록
        if (IsValid(Terminal) == true)
        {
            // 단말별 기록 확보
            FMD_TerminalRecord& Record = Out.Terminals.AddDefaulted_GetRef();
            // 원래 배치 단말 식별자 보관
            CaptureActor(Actor, Record);
            // 카트리지 발급 완료 상태 보관
            Record.bDumped = Terminal->IsDumped();
            // 전원 잠금 상태 보관
            Record.bLocked = Terminal->GetPersistentLocked();
            // 다른 분류 검사 생략
            continue;
        }
        // 방문 구역의 로드 상태를 수집할 트리거 조회
        AMD_SubLevelLoadTrigger* Streaming = Cast<AMD_SubLevelLoadTrigger>(Actor);
        // 구역 로더를 내부 물품보다 먼저 복원할 별도 목록에 기록
        if (IsValid(Streaming) == true)
        {
            // 구역 설정과 실제 로드 의도 수집
            FMD_StreamingRecord Record = Streaming->BuildPersistenceRecord();
            // 로딩 중 일부 액터만 저장하는 불완전한 수집 차단
            if (Streaming->IsPersistentStreamingReady(Record.bLoaded) == false)
            {
                // 구역 전환이 끝난 뒤 다시 저장하도록 실패 전파
                return false;
            }
            // 방문 구역 기록 추가
            Out.Streaming.Add(MoveTemp(Record));
            // 다른 분류 검사 생략
            continue;
        }
        // 납품 전시 상태를 보존할 선반 조회
        AMD_SampleShelf* Shelf = Cast<AMD_SampleShelf>(Actor);
        // 빈 선반도 이전 전시를 지우는 유효 상태로 기록
        if (IsValid(Shelf) == true)
        {
            // 선반별 기록 확보
            FMD_ShelfRecord& Record = Out.Shelves.AddDefaulted_GetRef();
            // 배치 레벨과 선반 식별자 보관
            CaptureActor(Actor, Record);
            // 회수 완료된 물품의 외형 목록 보관
            Record.Entries = Shelf->GetPersistentDisplayEntries();
        }
    }
    // 수집 단계에서도 잘못된 상태의 파일 기록 차단
    return Validate(Out);
}

bool FMD_WorldActorPersistence::Validate(const FMD_WorldActorSnapshot& Snapshot)
{
    // 추가 기록이 없던 기존 파일의 원래 복원 경로 유지
    if (Snapshot.bCaptured == false)
    {
        // 기록된 형식 표시 없이 본문만 섞인 데이터 거부
        return Snapshot.QuestItems.IsEmpty() == true && Snapshot.TrashItems.IsEmpty() == true && Snapshot.Pickups.IsEmpty() == true
            && Snapshot.WallProgress.IsEmpty() == true && Snapshot.Utilities.IsEmpty() == true && Snapshot.Shelves.IsEmpty() == true
            && Snapshot.Terminals.IsEmpty() == true && Snapshot.Streaming.IsEmpty() == true;
    }
    // 여러 분류가 동일 액터를 중복 적용하는 상황 방지
    TSet<FName> Keys;
    // 모든 분류에 공통인 클래스와 식별자 및 좌표 검사
    auto ValidateRecords = [&Keys](auto& Records, UClass* RequiredClass)
    {
        // 개별 액터 기록의 공통 구조 확인
        for (auto& Record : Records)
        {
            // 같은 공통 검사로 전체 복원과 구역 준비의 식별 규칙 유지
            if (ValidateActorRecord(Record, RequiredClass, Keys) == false)
            {
                // 부분 적용 전에 잘못된 기록 거부
                return false;
            }
        }
        // 해당 분류의 공통 검사 완료
        return true;
    };
    // 지원하는 액터 분류별 필수 기반 클래스 검사
    if (ValidateRecords(Snapshot.QuestItems, AMD_QuestItem::StaticClass()) == false
        || ValidateRecords(Snapshot.TrashItems, AMD_TrashItem::StaticClass()) == false
        || ValidateRecords(Snapshot.Pickups, AMD_QuestInteractionPickup::StaticClass()) == false
        || ValidateRecords(Snapshot.WallProgress, AMD_CuttableWall::StaticClass()) == false
        || ValidateRecords(Snapshot.Utilities, AMD_WallUtility::StaticClass()) == false
        || ValidateRecords(Snapshot.Shelves, AMD_SampleShelf::StaticClass()) == false
        || ValidateRecords(Snapshot.Terminals, AMD_DataDumpTerminal::StaticClass()) == false)
    {
        // 잘못된 종류의 액터 생성을 차단하는 실패 반환
        return false;
    }
    // 방문한 구역은 재로드 가능한 경로와 고정 인스턴스 이름을 필수로 검사
    for (auto& Record : Snapshot.Streaming)
    {
        // 공통 액터 식별자와 구역 경로를 함께 검사
        if (ValidateStreamingRecord(Record, Keys) == false)
        {
            // 복원 대상을 식별할 수 없는 구역 거부
            return false;
        }
    }
    // 샘플의 유효한 등급만 복원 허용
    for (auto& Record : Snapshot.QuestItems)
    {
        // 잘못된 열거 값의 납품 판정 유입 차단
        if (StaticEnum<EMD_SampleGrade>()->IsValidEnumValue(static_cast<int64>(Record.Grade)) == false)
        {
            // 손상된 등급 거부
            return false;
        }
    }
    // 존재하지 않는 종류의 쓰레기 방지
    for (auto& Record : Snapshot.TrashItems)
    {
        // 기본 종류로 조용히 대체되는 상황 차단
        if (Record.Kind.IsNone() == true)
        {
            // 필수 쓰레기 종류 누락 거부
            return false;
        }
    }
    // 상호작용 단계의 유효 범위 검사
    for (auto& Record : Snapshot.Pickups)
    {
        // 음수 단계가 다른 퀘스트 진행에 반영되는 상황 차단
        if (Record.PhaseIndex < 0)
        {
            // 잘못된 단계 거부
            return false;
        }
    }
    // 누적 절단량의 숫자 손상 검사
    for (auto& Record : Snapshot.WallProgress)
    {
        // 무한값과 음수 진행량 거부
        if (FMath::IsFinite(Record.CutSeconds) == false || Record.CutSeconds < 0.0f)
        {
            // 절단 진행 손상 반환
            return false;
        }
    }
    // 설비 상태와 감점 타이머의 숫자 검사
    for (auto& Record : Snapshot.Utilities)
    {
        // 손상된 좌표와 타이머 및 열거 값의 적용 차단
        if (Record.State > EMD_UtilityState::Repaired || Record.BreakLocation.ContainsNaN() == true
            || Record.BreakNormal.ContainsNaN() == true || Record.LeakTicksApplied < 0
            || FMath::IsFinite(Record.LeakTimeRemaining) == false || Record.LeakTimeRemaining < 0.0f)
        {
            // 잘못된 설비 기록 반환
            return false;
        }
    }
    // 전시 목록에서 허용되는 빈 메시와 잘못된 크기 구분
    for (auto& Record : Snapshot.Shelves)
    {
        // 각 납품 표시의 메시와 크기 검사
        for (auto& Entry : Record.Entries)
        {
            // 메시 없는 납품 기록은 유지하고 제거 중 객체 및 손상된 크기 거부
            if ((Entry.Mesh != nullptr && IsValid(Entry.Mesh) == false) || Entry.Scale.ContainsNaN() == true)
            {
                // 불완전한 선반 복원 차단
                return false;
            }
        }
    }
    // 모든 분류의 저장 구조 검증 완료
    return true;
}

bool FMD_WorldActorPersistence::PrepareStreaming(UWorld* World, const FMD_WorldActorSnapshot& Snapshot, bool& bReady)
{
    // 완료 여부는 실제 모든 구역을 확인한 뒤 확정
    bReady = false;
    // 비권위 또는 형식 표시 없는 구역 요청 차단
    if (IsValid(World) == false || World->GetNetMode() == NM_Client
        || (Snapshot.bCaptured == false && Snapshot.Streaming.IsEmpty() == false))
    {
        // 준비할 수 없는 저장 반환
        return false;
    }
    // 대기 틱마다 물품 전체 대신 구역 요청만 검증할 식별자 집합
    TSet<FName> StreamingKeys;
    // 어떤 로드 요청도 실행하기 전에 구역 기록 전체 검사
    for (auto& Record : Snapshot.Streaming)
    {
        // 손상된 후속 구역 때문에 앞선 요청만 적용되는 상황 방지
        if (ValidateStreamingRecord(Record, StreamingKeys) == false)
        {
            // 유효하지 않은 구역 준비 중단
            return false;
        }
    }
    // 상위 구역 로드를 기다리는 중첩 트리거 구분
    bool bMissingParent = false;
    // 실제 진행 중인 스트리밍 요청 추적
    bool bWaitingForStreaming = false;
    // 이미 준비된 상위 구역부터 트리거 적용
    for (auto& Record : Snapshot.Streaming)
    {
        // 상위 구역이 준비되지 않은 중첩 트리거 연기
        if (IsValid(FindLevel(World, Record.LevelPackage)) == false)
        {
            // 후속 틱에서 상위 구역 준비 여부를 다시 확인할 표시
            bMissingParent = true;
            // 다른 최상위 구역 로드 요청 계속 처리
            continue;
        }
        // 정확한 배치 구역과 이름의 트리거 조회
        AMD_SubLevelLoadTrigger* Trigger = Cast<AMD_SubLevelLoadTrigger>(FindActor(World, Record));
        // 변경된 배치나 구역 설정을 월드 적용 전에 거부
        if (IsValid(Trigger) == false || Trigger->GetClass() != Record.ActorClass.Get()
            || Trigger->RestorePersistenceRecord(Record) == false)
        {
            // 유효하지 않은 구역 로더의 실패 전파
            return false;
        }
        // 내부 액터 등록이 끝날 때까지 준비 완료 연기
        if (Trigger->IsPersistentStreamingReady(Record.bLoaded) == false)
        {
            // 진행 중인 구역 요청 표시
            bWaitingForStreaming = true;
        }
    }
    // 어떤 로더도 준비하지 않는 사라진 상위 구역의 무한 대기 방지
    if (bMissingParent == true && bWaitingForStreaming == false)
    {
        // 저장과 맵의 구역 구성 불일치 반환
        return false;
    }
    // 모든 트리거와 내부 액터가 준비된 경우에만 적용 허용
    bReady = bMissingParent == false && bWaitingForStreaming == false;
    // 정상적인 비동기 준비 요청의 성공 반환
    return true;
}

bool FMD_WorldActorPersistence::CanRestore(UWorld* World, const FMD_WorldActorSnapshot& Snapshot)
{
    // 서버 월드와 기록 구조를 월드 변경 전에 검사
    if (IsValid(World) == false || World->GetNetMode() == NM_Client || Validate(Snapshot) == false)
    {
        // 적용할 수 없는 요청 종료
        return false;
    }
    // 분류별로 동적 재생성 허용 여부를 구분하는 검사
    auto CheckTargets = [World](auto& Records, bool bRequired)
    {
        // 실제 배치 레벨과 액터 클래스 일치 여부 검사
        for (auto& Record : Records)
        {
            // 스트리밍 레벨이 아직 준비되지 않은 경우 실패
            if (IsValid(FindLevel(World, Record.LevelPackage)) == false)
            {
                // 다른 레벨에 대신 생성하는 상황 차단
                return false;
            }
            // 존재하는 대상과 저장 클래스 비교
            AActor* Actor = FindActor(World, Record);
            // 필수 배치 액터 소실과 이름 충돌 차단
            if ((IsValid(Actor) == false && bRequired == true)
                || (IsValid(Actor) == true && (Actor->IsActorBeingDestroyed() == true || Actor->GetClass() != Record.ActorClass.Get())))
            {
                // 실제 적용 전 대상 불일치 반환
                return false;
            }
        }
        // 해당 분류의 대상 검사 완료
        return true;
    };
    // 생성 가능한 물품과 기존 배치가 필수인 설비를 별도 검사
    if (CheckTargets(Snapshot.QuestItems, false) == false || CheckTargets(Snapshot.TrashItems, false) == false
        || CheckTargets(Snapshot.Pickups, false) == false || CheckTargets(Snapshot.WallProgress, true) == false
        || CheckTargets(Snapshot.Utilities, true) == false || CheckTargets(Snapshot.Shelves, true) == false
        || CheckTargets(Snapshot.Terminals, true) == false || CheckTargets(Snapshot.Streaming, true) == false)
    {
        // 필수 대상을 찾지 못한 복원 거부
        return false;
    }
    // 내부 물품 적용 전에 저장된 모든 구역의 실제 준비 확인
    for (auto& Record : Snapshot.Streaming)
    {
        // 앞선 대상 검증을 통과한 구역 트리거 조회
        AMD_SubLevelLoadTrigger* Trigger = Cast<AMD_SubLevelLoadTrigger>(FindActor(World, Record));
        // 로드 도중의 부분 월드 적용 차단
        if (IsValid(Trigger) == false || Trigger->IsPersistentStreamingReady(Record.bLoaded) == false)
        {
            // 구역 준비를 먼저 완료하도록 실패 반환
            return false;
        }
    }
    // 맵 수정으로 절단 한도가 바뀐 경우 사전 차단
    for (auto& Record : Snapshot.WallProgress)
    {
        // 앞선 대상 검사를 통과한 벽 조회
        AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(FindActor(World, Record));
        // 현재 정의로 해석할 수 있는 절단 진행량 검사
        if (IsValid(Wall) == false || Record.CutSeconds > Wall->GetRequiredCutSeconds())
        {
            // 월드를 변경하기 전 호환 실패 반환
            return false;
        }
    }
    // 모든 현재 대상의 복원 가능성 검사 완료
    return true;
}

bool FMD_WorldActorPersistence::Restore(UWorld* World, const FMD_WorldActorSnapshot& Snapshot)
{
    // 지원하지 않는 구조와 잘못된 맵 대상 차단
    if (CanRestore(World, Snapshot) == false)
    {
        // 생존 목록 적용 이전 실패 반환
        return false;
    }
    // 추가 정보가 없던 기존 파일은 기존 월드 복원 유지
    if (Snapshot.bCaptured == false)
    {
        // 미기록 상태를 모든 물품 수거로 해석하지 않도록 정상 종료
        return true;
    }
    // 분류별 생존 목록의 재생성과 이미 수거된 물품 제거 처리
    auto RestoreItems = [World](auto& Records, UClass* ItemClass, auto ApplyState)
    {
        // 실제로 복원한 액터만 생존 대상으로 보관
        TSet<AActor*> Restored;
        // 저장 당시 존재했던 모든 물품 복원
        for (auto& Record : Records)
        {
            // 현재 물품 재사용 또는 원래 레벨에서 재생성
            AActor* Actor = RestoreActor(World, Record, [&Record, &ApplyState](AActor* Target)
            {
                // 엔진 생성 완료 전후에 동일한 저장 상태 적용
                return ApplyState(Target, Record);
            });
            // 생성과 개별 상태 적용의 실패 전파
            if (IsValid(Actor) == false)
            {
                // 누락된 물품을 성공으로 처리하지 않는 실패 반환
                return false;
            }
            // 생존이 확인된 물품 보관
            Restored.Add(Actor);
        }
        // 새 맵이 초기 배치한 물품 중 이미 수거된 대상 제거
        for (TActorIterator<AActor> It(World, ItemClass); It; ++It)
        {
            // 저장 목록에 없는 해당 분류의 물품만 제거
            if (Restored.Contains(*It) == false && It->Destroy() == false)
            {
                // 제거되지 않은 물품이 중복 보상으로 이어지는 상황 차단
                return false;
            }
        }
        // 해당 분류의 정확한 생존 목록 적용 완료
        return true;
    };
    // 퀘스트 물품의 귀속과 손상 및 등급 복원
    if (RestoreItems(Snapshot.QuestItems, AMD_QuestItem::StaticClass(), [](AActor* Actor, auto& Record)
    {
        // 사전 검사로 확인된 퀘스트 물품 조회
        AMD_QuestItem* Item = CastChecked<AMD_QuestItem>(Actor);
        // 저장 당시 납품 대상 연결
        Item->QuestDefinitionId = Record.QuestDefinitionId;
        // 샘플 전용 상태 적용 대상 조회
        AMD_SampleItem* Sample = Cast<AMD_SampleItem>(Item);
        // 손상 효과 없이 샘플 저장 상태 복원
        if (IsValid(Sample) == true)
        {
            // 납품 등급과 손상 외형 및 자동 제출 자격 동기화
            Sample->RestorePersistentSampleState(Record.Grade, Record.bDamaged, Record.bHasBeenCarried);
        }
        // 퀘스트 물품 복원 성공 반환
        return true;
    }) == false)
    {
        // 필수 퀘스트 물품 누락 전파
        return false;
    }
    // 일반 쓰레기의 종류와 Variant 복원
    if (RestoreItems(Snapshot.TrashItems, AMD_TrashItem::StaticClass(), [](AActor* Actor, auto& Record)
    {
        // 실제 종류 테이블 조회 결과를 전체 복원 성공 여부에 반영
        return CastChecked<AMD_TrashItem>(Actor)->SetTrashAppearance(Record.Kind, Record.Variant);
    }) == false)
    {
        // 쓰레기 외형이나 생성 실패 전파
        return false;
    }
    // 남은 상호작용 물품의 퀘스트 단계 복원
    if (RestoreItems(Snapshot.Pickups, AMD_QuestInteractionPickup::StaticClass(), [](AActor* Actor, auto& Record)
    {
        // 사전 검사로 확인된 상호작용 물품 조회
        AMD_QuestInteractionPickup* Pickup = CastChecked<AMD_QuestInteractionPickup>(Actor);
        // 귀속 퀘스트 연결
        Pickup->QuestDefinitionId = Record.QuestDefinitionId;
        // 저장된 상호작용 단계 연결
        Pickup->PhaseIndex = Record.PhaseIndex;
        // 인스턴스별 표시 이름 복원
        Pickup->DisplayName = Record.DisplayName;
        // 상호작용 물품 복원 성공 반환
        return true;
    }) == false)
    {
        // 필수 상호작용 물품 누락 전파
        return false;
    }
    // 완료되지 않은 벽의 절단 진행 복원
    for (auto& Record : Snapshot.WallProgress)
    {
        // 원래 배치된 벽 조회
        AMD_CuttableWall* Wall = Cast<AMD_CuttableWall>(FindActor(World, Record));
        // 파괴 이벤트 없이 절단 진행만 적용
        if (IsValid(Wall) == false || Wall->RestorePersistentCutSeconds(Record.CutSeconds) == false)
        {
            // 벽 상태 복원 실패 전파
            return false;
        }
    }
    // 설비의 위험과 수리 및 누수 감점 진행 복원
    for (auto& Record : Snapshot.Utilities)
    {
        // 원래 배치된 설비 조회
        AMD_WallUtility* Utility = Cast<AMD_WallUtility>(FindActor(World, Record));
        // 사고 점수 중복 없이 파생 설비별 상태 복원
        if (IsValid(Utility) == false || Utility->RestorePersistenceRecord(Record) == false)
        {
            // 설비 복원 실패 전파
            return false;
        }
    }
    // 이미 납품한 물품의 전시 상태 복원
    for (auto& Record : Snapshot.Shelves)
    {
        // 원래 배치된 선반 조회
        AMD_SampleShelf* Shelf = Cast<AMD_SampleShelf>(FindActor(World, Record));
        // 저장 후 제거된 선반의 조용한 누락 차단
        if (IsValid(Shelf) == false)
        {
            // 선반 대상 누락 전파
            return false;
        }
        // 퀘스트 진행과 보상을 재발행하지 않는 전시 적용
        Shelf->RestorePersistentDisplayEntries(Record.Entries);
    }
    // 사용 완료 단말을 카트리지 재지급 없이 복원
    for (auto& Record : Snapshot.Terminals)
    {
        // 저장된 배치 단말 조회
        AMD_DataDumpTerminal* Terminal = Cast<AMD_DataDumpTerminal>(FindActor(World, Record));
        // 복원 중 제거된 단말의 누락 전파
        if (IsValid(Terminal) == false)
        {
            // 단말 복원 실패 반환
            return false;
        }
        // 전원과 완료 표시의 최종 상태 적용
        Terminal->RestorePersistentState(Record.bDumped, Record.bLocked);
    }
    // 모든 맵 상호작용 상태 복원 완료
    return true;
}

bool FMD_WorldActorPersistence::ValidateActorRecord(const FMD_WorldActorRecord& Record, UClass* RequiredClass, TSet<FName>& Keys)
{
    // 실제 객체 조회와 동일한 이름 비교 방식으로 구역별 키 구성
    FName Key(Record.LevelPackage.ToString() + TEXT(":") + Record.ActorName.ToString());
    // 클래스 소실과 잘못된 좌표 및 중복 식별자 차단
    if (Record.LevelPackage.IsNone() == true || Record.ActorName.IsNone() == true || Keys.Contains(Key) == true
        || IsValid(Record.ActorClass.Get()) == false || Record.ActorClass->IsChildOf(RequiredClass) == false
        || Record.ActorClass->HasAnyClassFlags(CLASS_Abstract) == true || Record.Transform.ContainsNaN() == true
        || Record.Transform.GetRotation().IsNormalized() == false)
    {
        // 실제 액터 검색이나 생성 이전의 구조 오류 반환
        return false;
    }
    // 후속 분류를 포함한 중복 식별자 검사 기준 등록
    Keys.Add(Key);
    // 공통 기록 검사 완료
    return true;
}

bool FMD_WorldActorPersistence::ValidateStreamingRecord(const FMD_StreamingRecord& Record, TSet<FName>& Keys)
{
    // 구역을 복원할 로더 클래스와 실제 식별자 검사
    if (ValidateActorRecord(Record, AMD_SubLevelLoadTrigger::StaticClass(), Keys) == false)
    {
        // 유효하지 않은 구역 로더 기록 거부
        return false;
    }
    // 방문한 구역에 재로드 가능한 경로와 고정 인스턴스 이름 요구
    return Record.bLoaded == false || (Record.LevelAsset.IsNull() == false && Record.InstanceName.IsEmpty() == false);
}
