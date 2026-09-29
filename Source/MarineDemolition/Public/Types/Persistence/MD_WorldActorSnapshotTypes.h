#pragma once

#include "CoreMinimal.h"
#include "World/Sample/MD_SampleShelf.h"
#include "World/Wall/MD_WallUtility.h"
#include "Types/MD_SampleFloatSettings.h"
#include "MD_WorldActorSnapshotTypes.generated.h"

// 재생성 가능한 맵 액터의 배치 레벨과 클래스 및 위치 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 같은 이름을 가진 서로 다른 서브레벨 액터 구분
    UPROPERTY()
    FName LevelPackage;

    // 해당 레벨에서 기존 액터와 복원 기록을 연결할 식별자
    UPROPERTY()
    FName ActorName;

    // 현재 월드에 없는 동적 물품의 생성 원형
    UPROPERTY()
    TSubclassOf<AActor> ActorClass;

    // 플레이어가 옮긴 위치와 회전 및 크기
    UPROPERTY()
    FTransform Transform = FTransform::Identity;
};

// 샘플과 일반 운반 퀘스트 물품의 진행 연결 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_QuestItemRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 물품 납품이 연결되는 퀘스트 정의
    UPROPERTY()
    FPrimaryAssetId QuestDefinitionId;

    // 샘플의 납품 조건을 보존하는 등급
    UPROPERTY()
    EMD_SampleGrade Grade = EMD_SampleGrade::C;

    // 이미 손상된 샘플을 온전한 물품으로 되돌리는 상황 방지
    UPROPERTY()
    bool bDamaged = false;

    // 복원된 샘플의 복귀 구역 자동 제출 자격 보존
    UPROPERTY()
    bool bHasBeenCarried = false;

    UPROPERTY()
    FMD_SampleFloatSettings WaterFloat;
};

// 수거되지 않은 일반 쓰레기의 종류와 외형 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashItemRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 분리수거 판정과 질량을 결정하는 종류
    UPROPERTY()
    FName Kind;

    // 같은 종류 안에서 선택된 외형
    UPROPERTY()
    FName Variant;
};

// 아직 획득하지 않은 상호작용 퀘스트 오브젝트 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_QuestPickupRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 상호작용 진행을 반영할 퀘스트 정의
    UPROPERTY()
    FPrimaryAssetId QuestDefinitionId;

    // 상호작용 대상이 속한 퀘스트 단계
    UPROPERTY()
    int32 PhaseIndex = 0;

    // 배치 인스턴스에서 정한 표시 이름
    UPROPERTY()
    FText DisplayName;
};

// 완료 전 절단량을 잃지 않는 벽 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_WallProgressRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 저장 직전까지 누적된 실제 절단 시간
    UPROPERTY()
    float CutSeconds = 0.0f;
};

// 점수를 중복 지급하지 않고 설비 파손과 수리를 재현할 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_UtilityRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 정상과 파손 및 수리 완료 상태
    UPROPERTY()
    EMD_UtilityState State = EMD_UtilityState::Intact;

    // 누수와 스파크를 재생할 파손 지점
    UPROPERTY()
    FVector BreakLocation = FVector::ZeroVector;

    // 파손 연출의 분출 방향
    UPROPERTY()
    FVector BreakNormal = FVector::UpVector;

    // 수도관에서 이미 적용한 누수 감점 횟수
    UPROPERTY()
    int32 LeakTicksApplied = 0;

    // 배치 당시 파손과 플레이 중 파손의 누수 타이머 구분
    UPROPERTY()
    bool bLeakTimerActive = false;

    // 다음 누수 감점까지 남은 시간
    UPROPERTY()
    float LeakTimeRemaining = 0.0f;
};

// 납품한 물품이 선반에서 다시 사라지지 않도록 하는 전시 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_ShelfRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 납품 완료된 물품의 전시 메시와 손상 외형
    UPROPERTY()
    TArray<FMD_ShelfDisplayEntry> Entries;
};

// 일회성 퀘스트 단말의 사용 완료 및 잠금 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_TerminalRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 카트리지 지급이 완료된 단말 상태
    UPROPERTY()
    bool bDumped = false;

    // 전원 게이트의 잠금 상태
    UPROPERTY()
    bool bLocked = false;
};

// 방문한 추가 구역의 로드 상태와 인스턴스 식별 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_StreamingRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()

    // 내부 액터를 복원하기 전에 준비할 구역 상태
    UPROPERTY()
    bool bLoaded = false;

    // 맵 편집으로 다른 구역을 여는 잘못된 복원 방지 경로
    UPROPERTY()
    TSoftObjectPtr<UWorld> LevelAsset;

    // 같은 맵의 여러 인스턴스를 구분하는 원래 이름
    UPROPERTY()
    FString InstanceName;
};

// Rack-local progress; contamination itself uses the shared surface snapshot.
USTRUCT()
struct MARINEDEMOLITION_API FMD_QuestRackRecord : public FMD_WorldActorRecord
{
    GENERATED_BODY()
    UPROPERTY() uint8 Mode = 2;
    UPROPERTY() bool bCartridgeReleased = false;
    UPROPERTY() bool bCartridgeCollected = false;
    UPROPERTY() bool bRackDisabled = false;
    UPROPERTY() FName CartridgeActorName;
    UPROPERTY() FName CartridgeLevelPackage;
    UPROPERTY() bool bPadlockCut = false;
    UPROPERTY() float CutSeconds = 0.f;
    UPROPERTY() TArray<float> BarnacleRemovalRatios;
};

// 동일한 수집 시점의 월드 액터 변경 기록
USTRUCT()
struct MARINEDEMOLITION_API FMD_WorldActorSnapshot
{
    GENERATED_BODY()

    // 기존 파일의 미기록 상태와 실제로 모든 물품을 수거한 빈 상태 구분
    UPROPERTY()
    bool bCaptured = false;

    // 운반 가능한 퀘스트 물품의 전체 생존 목록
    UPROPERTY()
    TArray<FMD_QuestItemRecord> QuestItems;

    // 일반 쓰레기의 전체 생존 목록
    UPROPERTY()
    TArray<FMD_TrashItemRecord> TrashItems;

    // 상호작용으로 아직 획득하지 않은 물품 목록
    UPROPERTY()
    TArray<FMD_QuestPickupRecord> Pickups;

    // 살아 있는 벽의 절단 진행
    UPROPERTY()
    TArray<FMD_WallProgressRecord> WallProgress;

    // 수도관과 배선의 파손 및 수리 상태
    UPROPERTY()
    TArray<FMD_UtilityRecord> Utilities;

    // 납품 선반별 전시 상태
    UPROPERTY()
    TArray<FMD_ShelfRecord> Shelves;

    // 퀘스트 카트리지 단말의 재사용 방지 기록
    UPROPERTY()
    TArray<FMD_TerminalRecord> Terminals;

    UPROPERTY()
    TArray<FMD_QuestRackRecord> QuestRacks;

    // 방문한 동적 스트리밍 구역의 복원 순서 결정 기록
    UPROPERTY()
    TArray<FMD_StreamingRecord> Streaming;

};
