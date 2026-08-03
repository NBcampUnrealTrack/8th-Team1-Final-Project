// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MD_ScrapeTypes.generated.h"

/**
 * 한 번의 긁기 판정이 표면에 만든 접촉의 종류
 *
 * 이 값은 판정기가 계산하는 네 가지 상호 배타적 결과 중 정확히 하나다.
 * 데이터에서 자유롭게 확장하는 분류가 아니라 switch의 완전성과 강한 타입 보장이 필요한
 * 닫힌 값 집합이므로 GameplayTag 대신 enum으로 유지한다.
 */
UENUM(BlueprintType)
enum class EMD_ScrapeContactType : uint8
{
	/** 유효한 표면 셀에 헤라가 닿지 않은 상태. 표면 밖을 긁은 경우도 포함 */
	None UMETA(DisplayName = "무접촉"),

	/** 헤라가 닿은 모든 작업량이 오염막 제거에만 사용된 정상적인 긁기 */
	ContaminationOnly UMETA(DisplayName = "오염막만 제거"),

	/** 한 스트로크 안에서 오염막 제거와 맨 벽면 접촉이 함께 발생한 상태 */
	Mixed UMETA(DisplayName = "오염막 제거 + 맨 벽면 접촉"),

	/** 오염막을 새로 제거하지 못하고 이미 드러난 맨 벽면만 긁은 상태 */
	BareSurfaceOnly UMETA(DisplayName = "맨 벽면만 접촉")
};

/**
 * 플레이어가 한 프레임 또는 한 네트워크 갱신 구간 동안 이동시킨 헤라의 경로
 *
 * 좌표는 월드 좌표가 아니라 "오염 표면의 로컬 2D 좌표"를 사용.
 * 표면 중앙이 (0, 0)이고, X는 표면의 가로 방향, Y는 표면의 세로 방향이며 단위는 cm.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeStroke
{
	GENERATED_BODY()

public:
	FMD_ScrapeStroke()
		: StartPositionCm(FVector2D::ZeroVector)
		, EndPositionCm(FVector2D::ZeroVector)
	{
	}

	/**
	 * 이번 갱신 구간에서 헤라가 출발한 표면 로컬 위치
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape")
	FVector2D StartPositionCm;

	/**
	 * 이번 갱신 구간에서 헤라가 도착한 표면 로컬 위치
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape")
	FVector2D EndPositionCm;
};

/** 헤라가 표면과 접촉하는 브러시 형태 */
UENUM(BlueprintType)
enum class EMD_ScrapeBrushShape : uint8
{
	/** 기본 헤라. 이동 방향에 수직으로 날의 가로축을 맞춘 사각 접촉면 */
	Rectangle UMETA(DisplayName = "사각형"),

	/** 원형 헤라 업그레이드. BladeWidthCm을 지름으로 사용 */
	Circle UMETA(DisplayName = "원형")
};

/**
 * 장착한 헤라가 제공하는 판정 성능
 *
 * 플레이어별 업그레이드 상태는 이 구조체로 표현한다. 벽은 이 값을 소유하지 않고
 * 긁기 요청마다 전달받으므로 같은 벽도 플레이어의 장비에 따라 다른 범위로 긁힌다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeToolSpec
{
	GENERATED_BODY()

public:
	FMD_ScrapeToolSpec()
		: BrushShape(EMD_ScrapeBrushShape::Rectangle)
		, BladeWidthCm(12.0f)
		, BladeDepthCm(12.0f)
		, RemovalPerTravelCentimeter(0.08f)
	{
	}

	/** 기본 사각 헤라와 원형 업그레이드를 구분하는 접촉 형태 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool")
	EMD_ScrapeBrushShape BrushShape;

	/**
	 * 표면과 실제로 접촉하는 헤라 날의 폭
	 *
	 * 사각형에서는 이동 방향과 수직인 날의 가로 길이, 원형에서는 지름으로 사용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool",
		meta = (ClampMin = "0.1", UIMin = "1.0", Units = "cm"))
	float BladeWidthCm;

	/**
	 * 사각형 접촉면이 이동 방향으로 차지하는 길이
	 *
	 * 기본값은 폭과 같은 12cm 정사각형이며 원형 판정에서는 사용하지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool",
		meta = (ClampMin = "0.1", UIMin = "1.0", Units = "cm",
			EditCondition = "BrushShape == EMD_ScrapeBrushShape::Rectangle",
			EditConditionHides))
	float BladeDepthCm;

	/**
	 * 헤라가 1cm 이동할 때 접촉 셀에서 제거하려는 오염막의 정규화 양
	 *
	 * 범위 업그레이드와 제거 성능 업그레이드를 서로 독립적으로 조정할 수 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool",
		meta = (ClampMin = "0.0001", UIMin = "0.001"))
	float RemovalPerTravelCentimeter;

	/** 판정기에 전달할 수 있는 유한한 양수 값인지 확인 */
	bool IsValid() const
	{
		const bool bHasKnownShape =
			BrushShape == EMD_ScrapeBrushShape::Rectangle
			|| BrushShape == EMD_ScrapeBrushShape::Circle;

		return bHasKnownShape
			&& FMath::IsFinite(BladeWidthCm)
			&& BladeWidthCm > UE_SMALL_NUMBER
			&& FMath::IsFinite(BladeDepthCm)
			&& BladeDepthCm > UE_SMALL_NUMBER
			&& FMath::IsFinite(RemovalPerTravelCentimeter)
			&& RemovalPerTravelCentimeter > UE_SMALL_NUMBER;
	}
};

/**
 * 긁기 판정의 최종 설정값 모음
 *
 * 기존 Blueprint 직렬화와 직접 벽 호출의 호환성을 위해 헤라 성능값도 유지한다.
 * 일반 플레이 경로에서는 UMD_ScrapeToolComponent가 소유한 FMD_ScrapeToolSpec을
 * 별도 인자로 전달하므로 이 구조체의 헤라 수치는 기존 직접 호출의 기본값으로만 사용한다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeSettings
{
	GENERATED_BODY()

public:
	FMD_ScrapeSettings()
		: BladeWidthCm(12.0f)
		, RemovalPerTravelCentimeter(0.08f)
		, SampleSpacingRatioOfSmallestCell(0.5f)
		, MaximumSampleSpacingCm(2.5f)
		, MinimumTravelDistanceCm(0.1f)
		, CleanTolerance(0.001f)
	{
	}

	/**
	 * 표면과 실제로 접촉하는 헤라 날의 폭
	 *
	 * 기존 직접 호출에서는 이 값을 폭과 깊이로 갖는 기본 정사각형 스펙을 생성한다.
	 * 일반 플레이 경로의 실제 형태와 크기는 FMD_ScrapeToolSpec을 사용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.1", UIMin = "1.0", Units = "cm"))
	float BladeWidthCm;

	/**
	 * 헤라가 1cm 이동할 때 접촉 셀에서 제거하려는 오염막의 정규화 양
	 *
	 * 오염량은 0~1 범위이므로 0.08은 "이동 거리 1cm당 최대 8%"의 제거 작업량을 의미.
	 * 실제 제거량에는 셀 피복 가중치가 곱해지므로 헤라 날 가장자리는 중앙보다 덜 벗겨짐.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.0001", UIMin = "0.001"))
	float RemovalPerTravelCentimeter;

	/**
	 * 긁기 선분을 나눌 때 가장 작은 셀 한 변에 곱하는 비율
	 *
	 * 기본값 0.5는 가장 작은 셀 크기의 절반마다 한 번씩 접촉을 계산한다는 의미.
	 * 프레임률이 낮거나 헤라가 빠르게 움직여도 중간 경로가 빠지지 않게 만드는 값.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.05", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float SampleSpacingRatioOfSmallestCell;

	/**
	 * 셀이 매우 큰 경우에도 샘플 사이가 지나치게 벌어지지 않게 하는 상한 거리
	 *
	 * 실제 샘플 간격은 "가장 작은 셀 크기 × 비율", 이 값, 그리고 이동 방향의
	 * 브러시 크기 절반 중 가장 작은 값을 사용해 얇은 사각 헤라도 점선처럼 끊기지 않게 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.1", UIMin = "0.5", Units = "cm"))
	float MaximumSampleSpacingCm;

	/**
	 * 이 거리보다 짧은 입력을 손 떨림이나 동일 위치 중복 입력으로 보고 무시하는 기준
	 *
	 * 제거량을 DeltaTime이 아니라 실제 이동 거리로 계산하기 때문에,
	 * 이동하지 않은 채 버튼만 누르고 있어서는 오염막이 제거되지 않는 구조.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MinimumTravelDistanceCm;

	/**
	 * 부동소수점 오차를 고려하여 오염막이 완전히 제거됐다고 간주할 잔여량
	 *
	 * 예를 들어 잔여량이 0.0005이고 허용 오차가 0.001이면 셀을 0으로 맞추고
	 * 완전 제거된 셀로 판정. 시각적으로 보이지 않는 극소량 때문에 완료가 막히는 상황을 방지.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "0.1", UIMin = "0.0", UIMax = "0.01"))
	float CleanTolerance;

	/** 이 설정에 들어 있는 헤라 성능값을 별도 도구 스펙으로 반환 */
	FMD_ScrapeToolSpec GetToolSpec() const
	{
		FMD_ScrapeToolSpec ToolSpec;
		ToolSpec.BladeWidthCm = BladeWidthCm;
		ToolSpec.BladeDepthCm = BladeWidthCm;
		ToolSpec.RemovalPerTravelCentimeter = RemovalPerTravelCentimeter;
		return ToolSpec;
	}
};

/**
 * 오염 표면을 구성하는 격자 한 칸의 상태
 *
 * 각 셀이 독립적인 잔여 오염량을 가지므로 헤라가 지나간 자리만 부분적으로 벗겨지는 구조.
 * 표면 전체의 제거율은 모든 셀의 초기량과 잔여량을 합산하여 산출.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeSurfaceCell
{
	GENERATED_BODY()

public:
	FMD_ScrapeSurfaceCell()
		: InitialContamination(1.0f)
		, RemainingContamination(1.0f)
	{
	}

	/**
	 * 미션 시작 시 이 셀에 존재했던 오염막의 양
	 *
	 * 0은 처음부터 깨끗한 부분, 1은 완전히 덮인 부분을 의미.
	 * 제거율의 분모로 사용되므로 플레이 도중에는 바꾸지 않는 값으로 취급.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialContamination;

	/**
	 * 현재 이 셀에 남아 있는 오염막의 양
	 *
	 * 긁기 판정이 성공하면 감소하며, 항상 0과 InitialContamination 사이로 보정.
	 * 머티리얼 마스크나 Decal의 투명도는 이 값을 이용해 실제 벗겨진 모습을 표현 가능.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RemainingContamination;

	/**
	 * 외부 데이터 입력이나 블루프린트 수정으로 범위를 벗어난 값을 안전한 상태로 복구
	 * NaN/무한대 값은 계산 전체를 오염시키므로 0으로 정리
	 */
	void Normalize()
	{
		InitialContamination = FMath::IsFinite(InitialContamination)
			? FMath::Clamp(InitialContamination, 0.0f, 1.0f)
			: 0.0f;

		RemainingContamination = FMath::IsFinite(RemainingContamination)
			? FMath::Clamp(RemainingContamination, 0.0f, InitialContamination)
			: 0.0f;
	}

	/** 현재 잔여량이 허용 오차 이하인지 확인 */
	bool IsClean(const float Tolerance) const
	{
		return !FMath::IsFinite(RemainingContamination)
			|| RemainingContamination <= FMath::Max(0.0f, Tolerance);
	}

	/**
	 * 이 셀에서 제거된 비율을 0~1로 반환
	 * 처음부터 오염이 없던 셀은 제거할 대상도 없으므로 완료된 값인 1을 반환
	 */
	float GetRemovalRatio() const
	{
		if (!FMath::IsFinite(InitialContamination)
			|| InitialContamination <= UE_SMALL_NUMBER)
		{
			return 1.0f;
		}

		const float SafeRemaining = FMath::IsFinite(RemainingContamination)
			? FMath::Clamp(RemainingContamination, 0.0f, InitialContamination)
			: 0.0f;

		return FMath::Clamp(
			1.0f - (SafeRemaining / InitialContamination),
			0.0f,
			1.0f);
	}
};

/**
 * 한 번의 긁기로 실제 값이 변한 셀 하나의 변경 내역
 *
 * 표면 Actor는 이 배열만 순회해 머티리얼 마스크를 갱신 가능.
 * 매번 전체 격자를 다시 검사하지 않아도 되므로 시각 처리와 물리 조각 생성 비용을 절감.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeCellChange
{
	GENERATED_BODY()

public:
	FMD_ScrapeCellChange()
		: CellIndex(INDEX_NONE)
		, GridCoordinate(FIntPoint::ZeroValue)
		, RemovedAmount(0.0f)
		, RemainingAmountAfter(0.0f)
		, bBecameClean(false)
	{
	}

	/** 1차원 셀 배열에서의 위치. 인덱스 계산식은 X + (Y * GridWidth) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	int32 CellIndex;

	/** 디버그 표시나 텍스처 픽셀 갱신에 바로 쓸 수 있는 2차원 격자 좌표 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	FIntPoint GridCoordinate;

	/**
	 * 이번 스트로크가 이 셀에서 실제로 제거한 오염량
	 * 요청 작업량이 잔여 오염량보다 많아도 실제 제거량은 잔여량을 넘지 않음
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	float RemovedAmount;

	/** 이번 스트로크 적용이 끝난 뒤 셀에 남은 오염량 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	float RemainingAmountAfter;

	/** 이번 스트로크 도중 잔여량이 처음으로 CleanTolerance 이하가 되었는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	bool bBecameClean;
};

/**
 * 한 번의 긁기 입력을 격자에 적용한 최종 결과
 *
 * 이 구조체에는 판정 결과만 담고 사운드 재생, 이펙트 생성, 점수 변경은 미수행.
 * 그 덕분에 서버 권한 판정, 클라이언트 예측, 자동화 테스트에서 같은 계산 코드를 재사용 가능.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeResult
{
	GENERATED_BODY()

public:
	FMD_ScrapeResult()
		: bIsInputValid(false)
		, bWasStrokeLongEnough(false)
		, bHadSurfaceContact(false)
		, ContactType(EMD_ScrapeContactType::None)
		, TouchedCellCount(0)
		, ModifiedCellCount(0)
		, NewlyCleanedCellCount(0)
		, RemovedContaminationAmount(0.0f)
		, CleanRatioBefore(0.0f)
		, CleanRatioAfter(0.0f)
		, CleanRatioDelta(0.0f)
		, bIsSurfaceFullyClean(false)
		, bTouchedBareSurface(false)
	{
	}

	/** 격자 크기, 셀 개수, 설정값 등 판정에 필요한 입력이 모두 올바른지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	bool bIsInputValid;

	/**
	 * 스트로크가 MinimumTravelDistanceCm 이상 이동했는지 여부
	 * false라면 유효한 입력이어도 오염도는 변화 없음
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	bool bWasStrokeLongEnough;

	/** 헤라의 사각형 또는 원형 접촉 범위가 표면의 셀 하나 이상과 겹쳤는지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	bool bHadSurfaceContact;

	/**
	 * 정상 긁기와 맨 벽면 긁기를 구분한 결과
	 * 이후 사운드/이펙트 계층은 이 값으로 부드러운 박리음과 위험한 금속 마찰음을 구분 가능
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	EMD_ScrapeContactType ContactType;

	/** 이번 스트로크에서 한 번 이상 접촉한 고유 셀 수. 같은 셀을 여러 번 샘플링해도 한 번만 집계 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	int32 TouchedCellCount;

	/** 잔여 오염량이 실제로 감소한 고유 셀 수. CellChanges.Num()과 동일한 값 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	int32 ModifiedCellCount;

	/** 이번 스트로크로 처음 완전히 깨끗해진 셀 수 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	int32 NewlyCleanedCellCount;

	/**
	 * 변경된 모든 셀의 제거량 합계
	 *
	 * 이 값은 격자 해상도에 따라 크기가 달라질 수 있으므로 최종 점수 퍼센트로는 부적합.
	 * 파편 개수나 임시 이펙트 강도처럼 같은 표면 안에서 상대적인 크기를 비교할 때 사용.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	float RemovedContaminationAmount;

	/** 스트로크 적용 전 표면 전체의 제거 비율. 0은 미제거, 1은 완전 제거를 의미 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	float CleanRatioBefore;

	/** 스트로크 적용 후 표면 전체의 제거 비율 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	float CleanRatioAfter;

	/** 이번 스트로크가 증가시킨 표면 전체 제거 비율이며 CleanRatioAfter - CleanRatioBefore 값 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	float CleanRatioDelta;

	/**
	 * 초기 오염이 존재했던 모든 셀의 잔여량이 CleanTolerance 이하인지 여부
	 * Task 1 이후의 해금 판정은 이 값이 true로 바뀌는 순간 한 번만 실행하면 되는 구조
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	bool bIsSurfaceFullyClean;

	/**
	 * 이미 깨끗한 셀을 긁었거나, 해당 샘플의 작업량이 남은 오염량을 초과해
	 * 여분의 힘이 맨 벽면에 전달됐는지 여부
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	bool bTouchedBareSurface;

	/** 실제 오염량이 바뀐 셀만 담는 변경 내역. 시각 마스크와 파편 처리가 소비하는 값 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	TArray<FMD_ScrapeCellChange> CellChanges;
};
