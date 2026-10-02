#pragma once

#include "CoreMinimal.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "Types/Persistence/MD_SurfaceStainRecord.h"
#include "Types/Scrape/MD_ContaminationPendingDrop.h"
#include "Types/Persistence/MD_WorldActorSnapshotTypes.h"
#include "Types/Trash/MD_TrashSpawnState.h"
#include "MD_DaySnapshotTypes.generated.h"

class AMD_SupportBrace;
class AMD_TrashBin;
class AMD_WallPiece;

// Day 종료 시점에 살아남은 샘플 기록
USTRUCT()
struct FMD_SampleRecord
{
      GENERATED_BODY()

      UPROPERTY()
      FName ActorName = NAME_None;

      UPROPERTY()
      FTransform Transform = FTransform::Identity;

      UPROPERTY()
      bool bDamaged = false;
};

// 설치된 지지대 기록. 동적 스폰 액터라 클래스+트랜스폼으로 재스폰
USTRUCT()
struct FMD_BraceRecord
{
      GENERATED_BODY()

      UPROPERTY()
      TSubclassOf<AMD_SupportBrace> BraceClass;

      UPROPERTY()
      FTransform Transform = FTransform::Identity;

      UPROPERTY()
      FName SupportedWallName = NAME_None;

      // All walls connected at installation; the single name above remains for existing callers.
      UPROPERTY()
      TArray<FName> SupportedWallNames;

      UPROPERTY()
      bool bWeakened = false;
};

// 미수거 잔해(격벽 조각) 기록
USTRUCT()
struct FMD_PieceRecord
{
      GENERATED_BODY()

      UPROPERTY()
      TSubclassOf<AMD_WallPiece> PieceClass;

      UPROPERTY()
      FTransform Transform = FTransform::Identity;

      // 기존 파일의 필드만 읽으며 새 캡처와 복원은 항상 깨끗한 상태를 사용한다.
      UPROPERTY()
      bool bContaminated = false;
};

/**
 * 보급기가 만든 동적 쓰레기통 하나의 서버 권위 영속 상태.
 *
 * 클라이언트에는 FMD_TrashBinLiquidState 요약만 복제한다. 이 record의 stable ID,
 * 전체 recovery lots와 NextSpillSequence는 Day/ServerTravel/파일 저장에서만
 * 왕복하며 cosmetic 횟수나 액터 이름을 원장으로 사용하지 않는다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinRecord
{
      GENERATED_BODY()

      /** 재스폰과 쏟김 transaction ID의 기준이 되는 영구 식별자. */
      UPROPERTY()
      FGuid StableBinId;

      /** 원래 보급 제한과 파괴 추적 원장에 다시 연결할 레벨 배치 보급기 이름. */
      UPROPERTY()
      FName SourceDispenserName = NAME_None;

      // 다른 구역의 동일 이름 보급기를 구분하며 기존 파일은 미지정으로 해석
      UPROPERTY()
      FName SourceDispenserLevelPackage = NAME_None;

      UPROPERTY()
      TSubclassOf<AMD_TrashBin> TrashBinClass;

      UPROPERTY()
      FTransform Transform = FTransform::Identity;

      UPROPERTY()
      double LiquidUnits = 0.0;

      UPROPERTY()
      double RewardUnits = 0.0;

      /** 복원은 이 값을 증가시키지 않고 저장값을 그대로 유지한다. */
      UPROPERTY()
      int32 LiquidRevision = 0;

      /** SourceId별 carrier/reward 합을 잃지 않는 서버 전용 전체 원장. */
      UPROPERTY()
      TArray<FMD_ScrapeRecoveryLot> LiquidRecoveryLots;

      /** SPILL-001이 current+1을 preview할 성공 sequence high-water. 신규/구버전은 0. */
      UPROPERTY()
      uint64 NextSpillSequence = 0;

      /** 손상 record를 월드에 일부 적용하기 전에 거르는 공통 검사. */
      bool IsStructurallyValid() const;
};

/**
 * 배치기별 런타임 시드와 어제 떼어낸 따개비 순번 (기획 03 §3)
 *
 * 따개비는 배치기가 시드로 흩뿌리는 런타임 스폰 액터라 FName이 세션마다 달라진다.
 * 벽·샘플처럼 이름으로 식별할 수 없어 "몇 번째였는지"를 남긴다.
 * 이것이 없으면 Day가 바뀔 때 어제 떼어낸 따개비가 같은 자리에 전부 되살아난다.
 */
USTRUCT()
struct FMD_BarnacleRemovalRecord
{
      GENERATED_BODY()

      // 배치기는 레벨 배치 액터라 이름이 안정적이다
      UPROPERTY()
      FName SpawnerName = NAME_None;

      // RandomPerNewGame 모드의 시드가 유효한지. int32의 0도 정상 시드라 별도 표시한다
      UPROPERTY()
      bool bHasRuntimePlacementSeed = false;

      // 새 게임에서 서버가 생성하고 같은 세이브·레벨 재방문에 재사용하는 시드
      UPROPERTY()
      int32 RuntimePlacementSeed = 0;

      UPROPERTY()
      TArray<int32> RemovedSpawnIndices;
};

// Day 종료 시점의 현장 상태. 남은 벽과 절단 후 지지 중인 하중을 따로 기록한다
USTRUCT()
struct FMD_DaySnapshot
{
      GENERATED_BODY()

      // 물품과 설비 및 절단 진행을 다음 일차까지 보존하는 추가 기록
      UPROPERTY()
      FMD_WorldActorSnapshot ActorSnapshot;

      UPROPERTY()
      bool bValid = false;

      // 스냅샷을 찍은 레벨. 다른 레벨(모선 등)에서는 복원하지 않음
      UPROPERTY()
      FName WorldName = NAME_None;

      // 마감한 Day. 다음 시작 Day = CompletedDay + 1
      UPROPERTY()
      int32 CompletedDay = 0;

      UPROPERTY()
      int32 Score = 0;

      // 점검 경고 상태. Day 간 유지 (다음 점검 기준 ×1.1 적용용)
      UPROPERTY()
      bool bWarningIssued = false;

      // 계약 인원수 (Day 간 고정 유지)
      UPROPERTY()
      int32 CrewCount = 0;

      // 출발 시 선택한 점검 난이도를 다음 Day에서도 동일하게 유지하기 위한 계약 값
      UPROPERTY()
      EMD_Difficulty ContractDifficulty = EMD_Difficulty::Standard;

      // 비상 4일차 진행 여부
      UPROPERTY()
      bool bEmergencyDay = false;

      // 목록에 없는 레벨 배치 벽 = 어제 철거된 벽
      UPROPERTY()
      TArray<FName> SurvivingWallNames;

      // 지지 중인 벽(아직 절단되지 않은 벽과 절단 후 남은 하중)
      UPROPERTY()
      TArray<FName> SupportedWallNames;

      // Cut struts held by braces, awaiting loss of their last support.
      UPROPERTY()
      TArray<FName> PendingCollapseWallNames;

      UPROPERTY()
      TArray<FMD_BraceRecord> Braces;

      UPROPERTY()
      TArray<FMD_SampleRecord> Samples;

      UPROPERTY()
      TArray<FMD_PieceRecord> Pieces;

      /** 보급기에서 생성되어 다음 Day에도 같은 개체로 이어질 동적 쓰레기통. */
      UPROPERTY()
      TArray<FMD_TrashBinRecord> TrashBins;

      /** Set for new snapshots even when every generator has no trash left. */
      UPROPERTY()
      bool bHasTrashGeneratorState = false;

      UPROPERTY()
      TArray<FMD_TrashGeneratorState> TrashGenerators;

      // Day travel is a map reload, but must retain the previous day's exact placement.
      // This field is deliberately absent from FMD_WorldData (ordinary file/session loads).
      UPROPERTY()
      TMap<FName, FMD_TrashGeneratorPlacement> TrashPlacements;

      /** Detached debris mass/recovery state survives source destruction and day travel. */
      UPROPERTY()
      TArray<FMD_ContaminationPendingDrop> PendingContaminationDrops;

      UPROPERTY()
      TArray<FMD_SurfaceStainRecord> SurfaceStains;

      // 마감 완료된 섹터 마감 마커 (레벨 배치 액터라 이름으로 식별)
      UPROPERTY()
      TArray<FName> ClosedMarkerNames;

      // 배치기별로 어제 떼어낸 따개비 순번
      UPROPERTY()
      TArray<FMD_BarnacleRemovalRecord> BarnacleRemovals;
};
