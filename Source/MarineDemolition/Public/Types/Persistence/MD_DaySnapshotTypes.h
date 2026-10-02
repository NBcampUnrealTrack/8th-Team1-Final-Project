#pragma once

#include "Gameplay/Scrape/MD_ScrapeBoundaryPayload.h"
#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ContaminationGeneration.h"
#include "Types/Persistence/MD_ContaminationSavePayload.h"
#include "MD_DaySnapshotTypes.generated.h"

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

      // 저장 대상인 벽·바닥·설비 표면의 실제 컴포넌트 이름
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
};

template<> struct TStructOpsTypeTraits<FMD_WallContaminationRecord>
    : TStructOpsTypeTraitsBase2<FMD_WallContaminationRecord>
{
      // 압축 본문 공유 참조를 보존하는 구조체 복사 지원
      enum { WithCopy = true };
};
