#pragma once

#include "CoreMinimal.h"
#include "Types/Persistence/MD_WorldActorSnapshotTypes.h"

// 파일 저장과 일차 전환에서 공유하는 맵 물품 및 설비 복원 처리
class MARINEDEMOLITION_API FMD_WorldActorPersistence
{
public:
    // 현재 서버 맵의 물품과 진행 상태를 수집하는 함수
    static bool Capture(UWorld* World, FMD_WorldActorSnapshot& Out);

    // 읽기 전용 저장 구조의 식별자와 클래스 및 숫자 검사
    static bool Validate(const FMD_WorldActorSnapshot& Snapshot);

    // 월드를 변경하기 전에 모든 배치 레벨과 필수 대상 존재 검사
    static bool CanRestore(UWorld* World, const FMD_WorldActorSnapshot& Snapshot);

    // 저장 당시 방문한 구역을 먼저 준비하고 비동기 완료 여부 반환
    static bool PrepareStreaming(UWorld* World, const FMD_WorldActorSnapshot& Snapshot, bool& bReady);

    // 검증한 생존 목록과 개별 상태를 현재 월드에 적용하는 함수
    static bool Restore(UWorld* World, const FMD_WorldActorSnapshot& Snapshot);

    // 액터 이름과 스트리밍 레벨 및 실제 위치를 공통 형식으로 수집
    static void CaptureActor(AActor* Actor, FMD_WorldActorRecord& Out);

    // 서로 다른 구역에서 중복되는 벽과 오염 및 지지 대상 이름 구분
    static FName GetActorKey(AActor* Actor);

    // 현재 월드에 대응하는 저장 레벨 조회
    static ULevel* FindLevel(UWorld* World, FName Package);

    // 다른 레벨의 같은 이름 액터와 혼동하지 않는 대상 조회
    static AActor* FindActor(UWorld* World, const FMD_WorldActorRecord& Record);

    // 저장 상태를 생성 완료 전에 적용하는 동적 물품 재사용 또는 지연 생성
    static AActor* RestoreActor(UWorld* World, const FMD_WorldActorRecord& Record, TFunctionRef<bool(AActor*)> ApplyState);

protected:
    // 공통 식별자 및 클래스 검사를 구역 준비와 전체 검증에서 재사용
    static bool ValidateActorRecord(const FMD_WorldActorRecord& Record, UClass* RequiredClass, TSet<FName>& Keys);

    // 물품 전체를 재순회하지 않고 구역 로더의 필수 기록만 검사
    static bool ValidateStreamingRecord(const FMD_StreamingRecord& Record, TSet<FName>& Keys);
};
