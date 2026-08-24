#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_DaySnapshotTypes.generated.h"

class AMD_SupportBrace;
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

// 표면별 오염 셀 상태. 생성 기준값과 달라진 셀만 저장한다
USTRUCT()
struct FMD_WallContaminationRecord
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
};

/**
 * 배치기별로 어제 떼어낸 따개비 순번 (기획 03 §3)
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

      UPROPERTY()
      TArray<int32> RemovedSpawnIndices;
};

// Day 종료 시점의 현장 상태. Cut()이 벽을 Destroy하므로 "살아남은 벽"을 기록한다
USTRUCT()
struct FMD_DaySnapshot
{
      GENERATED_BODY()

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

      // 이름은 하위 호환을 위해 유지하지만 일반 표면 컴포넌트도 포함한다
      UPROPERTY()
      TArray<FMD_WallContaminationRecord> WallContaminations;

      // 마감 완료된 섹터 마감 마커 (레벨 배치 액터라 이름으로 식별)
      UPROPERTY()
      TArray<FName> ClosedMarkerNames;

      // 배치기별로 어제 떼어낸 따개비 순번
      UPROPERTY()
      TArray<FMD_BarnacleRemovalRecord> BarnacleRemovals;
};
