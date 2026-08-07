#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
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

// 벽별 오염 셀 상태. 초기값과 다른 셀만 저장한다
USTRUCT()
struct FMD_WallContaminationRecord
{
      GENERATED_BODY()

      UPROPERTY()
      FName WallName = NAME_None;

      UPROPERTY()
      TArray<int32> CellIndices;

      UPROPERTY()
      TArray<float> RemainingAmounts;
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

      UPROPERTY()
      TArray<FMD_WallContaminationRecord> WallContaminations;

      // 마감 완료된 섹터 마감 마커 (레벨 배치 액터라 이름으로 식별)
      UPROPERTY()
      TArray<FName> ClosedMarkerNames;
};
