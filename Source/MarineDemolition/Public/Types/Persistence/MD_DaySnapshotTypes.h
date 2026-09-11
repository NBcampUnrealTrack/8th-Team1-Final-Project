#pragma once

#include "Gameplay/Scrape/MD_ScrapeBoundaryPayload.h"
#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ContaminationGeneration.h"
#include "Templates/SubclassOf.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "Types/Persistence/MD_ContaminationSavePayload.h"
#include "Types/Persistence/MD_SurfaceStainRecord.h"
#include "Types/Scrape/MD_ContaminationPendingDrop.h"
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

      /** 구버전 저장 호환용 폐기 필드. 현재 판정은 읽지 않고 새 저장은 false다. */
      UPROPERTY()
      bool bSpillTiltAttemptConsumedUntilRearm = false;

      /** 손상 record를 월드에 일부 적용하기 전에 거르는 공통 검사. */
      bool IsStructurallyValid() const;
};

// 표면별 오염 셀 상태. 생성 기준값과 달라진 셀만 저장한다
USTRUCT()
struct MARINEDEMOLITION_API FMD_WallContaminationRecord
{
      GENERATED_BODY()

      /**
       * 표면 컴포넌트를 소유한 레벨 배치 액터 이름.
       * 기존 스냅샷 호환 때문에 WallName이라는 직렬화 이름은 유지한다.
       */
      UPROPERTY()
      FName WallName = NAME_None;

      /**
       * 일반 벽·바닥·설비의 표면 컴포넌트 이름.
       * NAME_None은 기존 AMD_CuttableWall의 기본 표면을 뜻한다.
       */
      UPROPERTY()
      FName SurfaceComponentName = NAME_None;

      /** 0: legacy/analytic surface, 1: baked mesh UV. Old records retain version 0. */
      UPROPERTY()
      int32 LayoutVersion = 0;

      /** Deterministic bake identity. Required for MeshUV; invalid for analytic surfaces. */
      UPROPERTY()
      FGuid LayoutId;

      /** Prevents interpreting saved rows at a new grid width. Zero is legacy only. */
      UPROPERTY()
      FIntPoint GridDimensions = FIntPoint::ZeroValue;

      /** MeshUV carrier ledger is tied to the initialized world scale. Zero denotes legacy. */
      UPROPERTY()
      FVector CarrierScale = FVector::ZeroVector;

      UPROPERTY()
      TArray<int32> CellIndices;

      /**
       * 셀이 짊어진 오염 총량. 밤샘 분출(기획 03 §3)이 이 값을 올리므로 함께 저장한다.
       * 저장하지 않으면 다음 Day에 생성 기준값으로 되돌아가고, 복원 시 남은 오염량이
       * 그 값까지 깎여 밤새 생긴 얼룩이 통째로 사라진다.
       */
      UPROPERTY()
      TArray<float> InitialAmounts;

      UPROPERTY()
      TArray<float> RemainingAmounts;

      /** 포화된 헤라가 새로 덧바른 오염량. 구버전 스냅샷에서는 비어 있다. */
      UPROPERTY()
      TArray<float> TransferredAmounts;

      /** 새 페인트의 성김-조밀함 시각 패턴 밀도. */
      UPROPERTY()
      TArray<float> TransferredPatternDensities;

      /**
       * 셀에서 헤라 날에 덮인 면적 비율. 구버전 스냅샷에서는 비어 있다.
       *
       * 저장하지 않으면 다음 Day에 어제 긁어 둔 자국의 경계가 격자 계단으로
       * 되돌아간다. 잔여량만으로는 셀 전체에 얇게 남은 막과 셀의 일부만 덮인
       * 상태를 구분할 수 없기 때문이다.
       */
      UPROPERTY()
      TArray<float> CoveredFractions;

      /** Initial state for cells omitted from the sparse delta payload. */
      UPROPERTY()
      FMD_ContaminationGeneration Generation;

      /** New records share immutable compressed chunks; old array property names remain readable. */
      UPROPERTY()
      FMD_ContaminationSavePayload CellPayload;

      /** Versioned current footprint contours; independent of gameplay cell encoding. */
      UPROPERTY()
      FMD_ScrapeBoundaryPayload BoundaryPayload;

      bool CompactCells();
      bool ExpandCells(FMD_WallContaminationRecord& Out) const;
      bool ValidateCellIndices(int32 MaximumCellCount) const;
      int32 GetCellCount() const { return CellPayload.Num() ? CellPayload.Num() : CellIndices.Num(); }
      void PostSerialize(const FArchive& Ar);
};

template<> struct TStructOpsTypeTraits<FMD_WallContaminationRecord>
    : TStructOpsTypeTraitsBase2<FMD_WallContaminationRecord>
{
      enum { WithPostSerialize = true, WithCopy = true };
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

// Day 종료 시점의 현장 상태. Cut()이 벽을 Destroy하므로 "살아남은 벽"을 기록한다
USTRUCT()
struct FMD_DaySnapshot
{
      GENERATED_BODY()

      UPROPERTY() bool bHasContaminationGenerationRecords = false;

    /** Missing in older saves: preserve authored/per-surface seeds, including zero. */
    UPROPERTY() bool bHasContaminationSessionSeed = false;
    UPROPERTY() int32 ContaminationSessionSeed = 0;

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

      // 살아남은 벽 중 지지대가 설치된 벽
      UPROPERTY()
      TArray<FName> SupportedWallNames;

      UPROPERTY()
      TArray<FMD_BraceRecord> Braces;

      UPROPERTY()
      TArray<FMD_SampleRecord> Samples;

      UPROPERTY()
      TArray<FMD_PieceRecord> Pieces;

      /** 보급기에서 생성되어 다음 Day에도 같은 개체로 이어질 동적 쓰레기통. */
      UPROPERTY()
      TArray<FMD_TrashBinRecord> TrashBins;

      // 이름은 하위 호환을 위해 유지하지만 일반 표면 컴포넌트도 포함한다
      UPROPERTY()
      TArray<FMD_WallContaminationRecord> WallContaminations;

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
