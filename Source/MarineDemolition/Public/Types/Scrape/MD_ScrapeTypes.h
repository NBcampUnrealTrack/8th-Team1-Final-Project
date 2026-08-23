// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "MD_ScrapeTypes.generated.h"

class AMD_Barnacle;
class UMD_ScrapeSurfaceComponent;

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
 * 오염 작업면의 앞면과 뒷면을 구분한다.
 *
 * 셀 저장과 마스크는 두 면이 하나의 파이프라인을 공유하지만, 긁기 판정과 제거율은
 * 이 값으로 작업면을 구분한다. 기존 데이터의 기본 동작을 유지하기 위해 앞면을 0으로 둔다.
 */
UENUM(BlueprintType)
enum class EMD_ScrapeSurfaceFace : uint8
{
	Front = 0 UMETA(DisplayName = "앞면"),
	Back = 1 UMETA(DisplayName = "뒷면")
};

/** 우클릭으로 유지하는 헤라 전용 작업 상태. */
UENUM(BlueprintType)
enum class EMD_ScrapeFocusState : uint8
{
	Free UMETA(DisplayName = "비활성"),
	FocusPending UMETA(DisplayName = "진입 대기"),
	FocusedReady UMETA(DisplayName = "작업 준비"),
	FocusedScraping UMETA(DisplayName = "긁는 중")
};

/** 서버 Focus 요청의 Reliable 응답. */
UENUM()
enum class EMD_ScrapeFocusBeginResult : uint8
{
	Denied,
	Accepted
};

/** 서버가 Focus를 강제로 정리한 이유. 클라이언트에서는 모두 같은 멱등 Cleanup을 탄다. */
UENUM()
enum class EMD_ScrapeFocusTerminationReason : uint8
{
	SessionTimeout,
	ViewInvalid,
	ProtocolViolation,
	EligibilityLost
};

/**
 * 클라이언트 hit를 포함하지 않는 Focus aim 전송 단위.
 *
 * CameraRelativeAimDirection은 승인된 고정 View의 Forward/Right/Up 좌표계에
 * 대한 단위 방향이다. 서버가 자신의 eye 원점과 session basis로 world ray를
 * 재구성한다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ScrapeAimNetSample
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 FocusSessionId = 0;

	UPROPERTY()
	uint32 Sequence = 0;

	/** Focus 시작 뒤 client monotonic 초. float 정밀도로 0.15s age 판정을 충분히 보존한다. */
	UPROPERTY()
	float ClientMonotonicSeconds = 0.0f;

	UPROPERTY()
	FVector_NetQuantizeNormal CameraRelativeAimDirection = FVector::ForwardVector;
};

/** 하나의 헤라 조준 ray가 찾은 처리 가능한 대상 종류. */
UENUM(BlueprintType)
enum class EMD_ScrapeAimQueryType : uint8
{
	/** 아무것도 맞지 않았거나 일반 물체가 작업 대상을 가린 상태. */
	NoTarget UMETA(DisplayName = "대상 없음"),

	/** 오염 작업면을 직접 맞혔거나 따개비 뒤에서 작업면을 찾은 상태. */
	SurfaceHit UMETA(DisplayName = "작업면"),

	/** 따개비를 맞혔지만 그 뒤에 오염 작업면은 없는 독립 접촉 상태. */
	DirectBarnacleOnly UMETA(DisplayName = "직접 따개비")
};

/**
 * 진입 검증, 로컬 조준과 이후 서버 재검증이 공유하는 헤라 ray 조회 결과.
 *
 * Type은 기존 표면/직접 따개비/무접촉 세 갈래를 보존한다. NoTarget일 때
 * bWasBlocked가 true면 ray가 빗나간 것이 아니라 일반 물체에 가로막힌 것이다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeAimQueryResult
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	EMD_ScrapeAimQueryType Type = EMD_ScrapeAimQueryType::NoTarget;

	/** SurfaceHit일 때 실제 판정을 적용할 작업면. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape|Aim")
	TObjectPtr<UMD_ScrapeSurfaceComponent> Surface = nullptr;

	/** ray가 처음 직접 맞힌 따개비. 뒤 Surface가 있으면 SurfaceHit에도 채워진다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient,
		Category = "MD|Scrape|Aim")
	TObjectPtr<AMD_Barnacle> Barnacle = nullptr;

	/** SurfaceHit의 표면 충돌 또는 DirectBarnacleOnly의 따개비 충돌. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	FHitResult Hit;

	/** 따개비를 먼저 맞힌 경우 원래 따개비 충돌. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	FHitResult BarnacleHit;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim",
		meta = (Units = "cm"))
	FVector2D SurfacePositionCm = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	FVector WorldContactLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	FVector WorldContactNormal = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim",
		meta = (Units = "cm"))
	float TraceDistanceCm = 0.0f;

	/** NoTarget이 일반 충돌물 또는 투영 불가능한 표면 때문에 발생했는지 여부. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Aim")
	bool bWasBlocked = false;

	bool IsActionable() const
	{
		return Type == EMD_ScrapeAimQueryType::SurfaceHit
			|| Type == EMD_ScrapeAimQueryType::DirectBarnacleOnly;
	}
};

/** 헤라 날에 쌓인 오염물의 즉시 교체형 시각 단계 */
UENUM(BlueprintType)
enum class EMD_ScrapeLoadStage : uint8
{
	/** ScrapeLoad가 0인 깨끗한 날 */
	Empty = 0 UMETA(DisplayName = "0단계 - 깨끗함"),

	/** 0보다 크고 34보다 작은 상태 */
	Stage1 = 1 UMETA(DisplayName = "1단계 - 얇은 코팅"),

	/** 34 이상 67 미만인 포화 직전 상태 */
	Stage2 = 2 UMETA(DisplayName = "2단계 - 처짐과 방울"),

	/** 67 이상인 포화 상태 */
	Stage3 = 3 UMETA(DisplayName = "3단계 - 포화")
};

/**
 * 오염 작업면의 앞/뒤를 결정하는 메시 로컬 축.
 *
 * Auto는 메시 자체에서 뚜렷하게 얇은 Bounds 축을 우선하고, 크기가 비슷하면
 * 월드 스케일이 반영된 크기에서 가장 얇은 축을 선택한다.
 * 방향이 특이하거나 두께가 비슷한 메시에서는 X/Y/Z를 직접 지정할 수 있다.
 */
UENUM(BlueprintType)
enum class EMD_ScrapeSurfaceNormalAxis : uint8
{
	Auto UMETA(DisplayName = "자동(가장 얇은 축)"),
	X UMETA(DisplayName = "로컬 X"),
	Y UMETA(DisplayName = "로컬 Y"),
	Z UMETA(DisplayName = "로컬 Z")
};

/**
 * 표면 로컬 2D 공간에서의 헤라 날 방향.
 *
 * 두 축이 모두 0이면 아직 basis를 제공하지 않은 legacy 입력이다. 둘 중 하나만 있거나
 * 평행한 축은 잘못된 명시 입력으로 취급한다. Evaluator는 유효한 입력을 정규직교화한 뒤
 * 사용하므로 월드→표면 투영에서 생긴 작은 수치 오차는 footprint 크기에 영향을 주지 않는다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeBladeBasis
{
	GENERATED_BODY()

public:
	FMD_ScrapeBladeBasis()
		: DepthAxis(FVector2D::ZeroVector)
		, WidthAxis(FVector2D::ZeroVector)
	{
	}

	FMD_ScrapeBladeBasis(
		const FVector2D& InDepthAxis,
		const FVector2D& InWidthAxis)
		: DepthAxis(InDepthAxis)
		, WidthAxis(InWidthAxis)
	{
	}

	/** BladeDepthCm이 놓이는 축. 이동 방향과 별개인 180도 동치 방향이다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Blade")
	FVector2D DepthAxis;

	/** BladeWidthCm이 놓이는 축. DepthAxis와 직교해야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Blade")
	FVector2D WidthAxis;

	/** 기존 호출자가 basis를 전혀 제공하지 않은 기본 상태인지 확인한다. */
	bool IsUnset() const
	{
		return DepthAxis.IsNearlyZero() && WidthAxis.IsNearlyZero();
	}
};

/**
 * Stage3 역오염 dab의 논리적 식별자.
 *
 * DistanceBin은 월드 좌표나 진행축 투영값이 아니라, 해당
 * sub-stroke의 서버 승인 누적 arc length에서 계산한 고정 간격 bin이다.
 * 네트워크 batch를 재전송해도 이 네 값이 같으면 같은 dab이다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ScrapeContaminationStampIdentity
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint64 FocusSessionGeneration = 0;

	UPROPERTY()
	uint32 LmbStrokeGeneration = 0;

	UPROPERTY()
	uint32 SubStrokeGeneration = 0;

	UPROPERTY()
	int32 DistanceBin = INDEX_NONE;

	bool IsValid() const
	{
		return FocusSessionGeneration != 0
			&& LmbStrokeGeneration != 0
			&& SubStrokeGeneration != 0
			&& DistanceBin >= 0;
	}

	bool operator==(const FMD_ScrapeContaminationStampIdentity& Other) const
	{
		return FocusSessionGeneration == Other.FocusSessionGeneration
			&& LmbStrokeGeneration == Other.LmbStrokeGeneration
			&& SubStrokeGeneration == Other.SubStrokeGeneration
			&& DistanceBin == Other.DistanceBin;
	}

	bool operator!=(const FMD_ScrapeContaminationStampIdentity& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(
		const FMD_ScrapeContaminationStampIdentity& Identity)
	{
		uint32 Hash = HashCombineFast(
			GetTypeHash(Identity.FocusSessionGeneration),
			GetTypeHash(Identity.LmbStrokeGeneration));
		Hash = HashCombineFast(Hash, GetTypeHash(Identity.SubStrokeGeneration));
		return HashCombineFast(Hash, GetTypeHash(Identity.DistanceBin));
	}
};

/**
 * ordered Stage3 path에서 확정된 exact-position dab.
 *
 * 위치와 blade basis는 candidate 생성 시점의 값을 저장한다. tail flush나
 * 재전송 시점의 현재 tangent로 다시 계산하지 않는다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ScrapeContaminationDab
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMD_ScrapeContaminationStampIdentity Identity;

	UPROPERTY()
	FVector2D PositionCm = FVector2D::ZeroVector;

	UPROPERTY()
	FMD_ScrapeBladeBasis BladeBasis;

	UPROPERTY()
	EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;

	UPROPERTY()
	double ArcLengthCm = 0.0;

	/** TASK 7 RPC sequence가 붙기 전에는 서버 로컬 승인 순번을 사용한다. */
	UPROPERTY()
	uint64 SampleSequence = 0;

	UPROPERTY()
	uint32 ToolSpecGeneration = 0;
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
		: SurfaceFace(EMD_ScrapeSurfaceFace::Front)
		, StartPositionCm(FVector2D::ZeroVector)
		, EndPositionCm(FVector2D::ZeroVector)
		, TravelDistanceCm(0.0f)
		, StartBladeBasis()
		, EndBladeBasis()
	{
	}

	/** 이번 스트로크가 적용되는 앞면 또는 뒷면. 한 스트로크는 한 면에만 속한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape")
	EMD_ScrapeSurfaceFace SurfaceFace;

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

	/**
	 * 이 구간이 담당하는 실제 누적 이동 거리.
	 *
	 * 0이면 기존 호출과 호환되도록 시작-끝 직선 거리를 사용한다. 양수 값은 명시 blade
	 * basis와 함께 안정화된 polyline을 짧은 구간으로 replay할 때만 제거 작업량의 기준이 된다.
	 * basis가 모두 미지정인 legacy 입력에서는 양수 값도 무시하고 직선 거리를 사용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float TravelDistanceCm;

	/** 구간 시작점에서의 명시적 표면 로컬 날 방향. 미지정 시 legacy tangent fallback을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Blade")
	FMD_ScrapeBladeBasis StartBladeBasis;

	/** 구간 끝점의 날 방향. 한쪽 끝만 지정하면 그 basis를 구간 전체에서 고정 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Blade")
	FMD_ScrapeBladeBasis EndBladeBasis;
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
		, BladeWidthCm(24.0f)
		, BladeDepthCm(12.0f)
		, RemovalPerTravelCentimeter(0.028f)
		, ReachCm(220.0f)
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
	 * 셀 하나는 날 길이(BladeDepthCm)에 셀 크기를 더한 거리만큼 날 밑을 지나므로,
	 * 24 x 12cm 날과 2.5cm 셀에서는 한 번 통과에 약 14.5cm 분량이 가해진다.
	 * 기본값 0.028은 한 번 통과에 약 1/3을 지워, 세 번 겹쳐 긁으면 완전히
	 * 벗겨지도록 맞춘 값이다. 이보다 크면 첫 통과에 거의 다 지워져서 긁은
	 * 횟수만큼 누적되는 느낌이 사라진다.
	 *
	 * 범위 업그레이드와 제거 성능 업그레이드를 서로 독립적으로 조정할 수 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool",
		meta = (ClampMin = "0.0001", UIMin = "0.001"))
	float RemovalPerTravelCentimeter;

	/**
	 * 카메라 ray 기준으로 이 헤라가 작업 대상을 판정할 수 있는 최대 거리.
	 *
	 * 기존 제품 동작을 보존하는 임시 기본값은 220cm다. 각 도구의 출시 값은 실제
	 * 메시·팔 IK 도달 시험 뒤 ToolDefinition의 ScrapeProfile에서 승인해야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float ReachCm;

	/**
	 * 따개비를 때릴 수 있는 최소 헤라 속도
	 *
	 * 따개비는 문질러서 벗기는 것이 아니라 쳐서 떼어내는 부착물이다. 이 속도에
	 * 못 미치면 헤라가 닿아도 흔들리기만 하고 내구도는 줄지 않는다. 기대고
	 * 밀기만 해서 떼는 편법을 막는 값이기도 하다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool|Barnacle",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float BarnacleMinimumImpactSpeedCmPerSecond = 60.0f;

	/**
	 * 한 번의 타격으로 인정하는 최대 헤라 속도
	 *
	 * 프레임이 한 번 튀거나 순간이동하듯 시선이 돌아가면 속도가 비현실적으로
	 * 크게 잡힌다. 상한이 없으면 그 한 프레임에 따개비가 떨어진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool|Barnacle",
		meta = (ClampMin = "0.0", UIMin = "1.0", Units = "cm/s"))
	float BarnacleMaximumImpactSpeedCmPerSecond = 600.0f;

	/**
	 * 최소 속도를 넘어선 1cm/s마다 따개비 내구도에서 깎는 작업량
	 *
	 * 벽의 RemovalPerTravelCentimeter와 따로 두는 이유는 둘의 대상이 다르기 때문이다.
	 * 저쪽은 표면 오염 셀을 훑어 벗기는 것이고 이쪽은 부착물 하나를 쳐서 떼는 것이다.
	 * 도구 등급 차이도 두 대상에서 같지 않다 — 08 §2.1의 열선 헤라는 "두꺼운
	 * 오염층·단단한 따개비"에 x2.2인데, 광폭 헤라는 넓이로 이득을 보므로 따개비
	 * 하나에는 이점이 덜하다.
	 *
	 * 기획서에 절대 수치가 없어 임시값이다. 기본 헤라로 250cm/s 정도의 보통 스트로크를
	 * 네다섯 번 치면 정규화 내구도 1.0이 소진되도록 잡았다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape|Tool|Barnacle",
		meta = (ClampMin = "0.0", UIMin = "0.0001"))
	float BarnacleImpactWorkPerSpeedUnit = 0.0015f;

	/**
	 * 헤라 속도를 따개비 내구도에서 깎을 작업량으로 바꾼다.
	 *
	 * 최소 속도 미만이면 0을 반환한다. 잘못 설정된 값이 들어와도 0을 돌려줄 뿐
	 * 음수나 NaN을 내보내지 않는다. 여기서 막지 않으면 진행도가 통째로 무너진다.
	 */
	float CalculateBarnacleImpactWork(const float ImpactSpeedCmPerSecond) const
	{
		if (!FMath::IsFinite(ImpactSpeedCmPerSecond)
			|| !FMath::IsFinite(BarnacleMinimumImpactSpeedCmPerSecond)
			|| !FMath::IsFinite(BarnacleMaximumImpactSpeedCmPerSecond)
			|| !FMath::IsFinite(BarnacleImpactWorkPerSpeedUnit)
			|| BarnacleImpactWorkPerSpeedUnit <= 0.0f)
		{
			return 0.0f;
		}

		const float MinimumSpeed = FMath::Max(0.0f, BarnacleMinimumImpactSpeedCmPerSecond);
		if (ImpactSpeedCmPerSecond < MinimumSpeed)
		{
			return 0.0f;
		}

		// 상한이 최소 속도보다 낮게 설정돼도 구간이 음수가 되지 않게 한다.
		const float MaximumSpeed = FMath::Max(
			MinimumSpeed,
			BarnacleMaximumImpactSpeedCmPerSecond);
		const float EffectiveSpeed = FMath::Min(ImpactSpeedCmPerSecond, MaximumSpeed);

		return (EffectiveSpeed - MinimumSpeed) * BarnacleImpactWorkPerSpeedUnit;
	}

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
			&& RemovalPerTravelCentimeter > UE_SMALL_NUMBER
			&& FMath::IsFinite(ReachCm)
			&& ReachCm > UE_SMALL_NUMBER;
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
		, RemovalPerTravelCentimeter(0.028f)
		, SampleSpacingRatioOfSmallestCell(0.5f)
		, MaximumSampleSpacingCm(2.5f)
		, MaximumBladeRotationDegreesPerSample(5.0f)
		, MinimumTravelDistanceCm(0.1f)
		, CleanTolerance(0.001f)
		, NegligibleFilmFraction(0.13f)
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
	 * 오염량은 0~1 범위이므로 0.028은 "이동 거리 1cm당 최대 2.8%"의 제거 작업량을 의미.
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
	 * 인접 Evaluator 샘플 사이에서 허용하는 최대 날 회전각.
	 *
	 * 명시적 시작/끝 basis가 회전하면 이동 거리가 짧더라도 이 각도 이하가 되도록 추가
	 * 세분화한다. 날 방향은 180도 동치이므로 실제 보간 회전은 항상 0~90도 범위다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.1", ClampMax = "90.0", UIMin = "1.0",
			UIMax = "30.0", Units = "deg"))
	float MaximumBladeRotationDegreesPerSample;

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

	/**
	 * 날에 완전히 덮인 셀에서 "다 벗겼다"고 인정할 잔여 막 비율
	 *
	 * 마스크는 이 비율보다 얇은 막을 화면에 표현하지 못한다. 그대로 두면 깨끗해
	 * 보이는데 완료 판정만 막히는 셀이 남으므로, 이미 있는 부분 피복 하한의
	 * MinimumVisibleContaminationFraction과 같은 원칙으로 0까지 정리한다.
	 *
	 * 부분 피복 셀에는 적용하지 않는다. 적용하면 자국 경계의 피복 램프에 구멍이
	 * 생겨 테두리가 셀 단위 계단으로 돌아간다.
	 *
	 * 기본값 0.13은 UMD_ScrapeSurfaceComponent의 마스크 판정 구간 폭
	 * (ContaminationVisualEdgeSoftness 0.55)에서 안 보이는 구간과 같다.
	 * 둘 중 하나를 바꾸면 나머지도 같이 맞춰야 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.0", UIMax = "0.3"))
	float NegligibleFilmFraction;

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
		, TransferredContamination(0.0f)
		, TransferredPaintPatternDensity(0.0f)
		, CoveredFraction(0.0f)
	{
	}

	/**
	 * 이 셀이 보유할 수 있는 현재 오염 기준량
	 *
	 * 생성 시에는 미션 시작 오염량이며 제거율의 분모로 사용한다.
	 * 플레이 중 새 오염이 처음부터 깨끗했던 셀에 묻으면 그 양까지 확장될 수 있다.
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

	/** 포화된 헤라가 새로 덧바른 오염량. 기본 오염과 별도 층으로 추적한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TransferredContamination;

	/** 전용 페인트 마스크가 사용할 고정 점묘 밀도. 0이면 새 페인트가 없다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TransferredPaintPatternDensity;

	/**
	 * 지금 얹혀 있는 오염층에서 헤라 날에 덮인 면적의 비율. 시각 전용.
	 *
	 * RemainingContamination 하나로는 "셀 전체에 얇게 남은 막"과 "셀의 일부만
	 * 덮이고 나머지는 그대로인 상태"를 구분할 수 없다. 둘은 같은 잔여량이어도
	 * 겉모습이 전혀 달라서(앞은 진한 막, 뒤는 자국 경계의 부분 피복) 한 값으로
	 * 뭉개면 막을 진하게 보이는 것과 경계를 곧게 만드는 것을 동시에 못 한다.
	 *
	 * 제거 판정은 이 값을 읽지 않는다. 오염이 다시 쌓이면 0으로 돌아간다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoveredFraction;

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

		TransferredContamination = FMath::IsFinite(TransferredContamination)
			? FMath::Clamp(TransferredContamination, 0.0f, 1.0f)
			: 0.0f;
		TransferredPaintPatternDensity =
			FMath::IsFinite(TransferredPaintPatternDensity)
			? FMath::Clamp(TransferredPaintPatternDensity, 0.0f, 1.0f)
			: 0.0f;
		if (TransferredContamination <= UE_SMALL_NUMBER)
		{
			TransferredContamination = 0.0f;
			TransferredPaintPatternDensity = 0.0f;
		}
		CoveredFraction = FMath::IsFinite(CoveredFraction)
			? FMath::Clamp(CoveredFraction, 0.0f, 1.0f)
			: 0.0f;
	}

	// 셀 하나의 깨끗함 판정과 제거 비율은 면 전체를 함께 봐야 하므로
	// FMD_ScrapeEvaluator::IsSurfaceFullyClean / CalculateCleanRatio가 담당한다.
	// 셀 단위 헬퍼를 여기에 다시 두면 판정 기준이 두 벌이 된다.
};

/**
 * 오염 가치가 현재 운반체에서 차지하는 양과 최종 보상 단위를 함께 보존하는 lot.
 *
 * CarrierAmount의 단위는 운반체마다 다르다. Surface에서는 raw cell 질량,
 * Tool에서는 Load point다. RewardUnits만 경제 가치이며 항상 double로 계산한다.
 * RewardUnits가 0인 lot은 따개비 연질 잔사처럼 관리량만 차지할 수 있다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeRecoveryLot
{
	GENERATED_BODY()

public:
	FMD_ScrapeRecoveryLot()
		: SourceId()
		, RewardClass(NAME_None)
		, CarrierAmount(0.0)
		, RewardUnits(0.0)
	{
	}

	/** 최초 발행원을 식별한다. 보상 가치가 0인 관리용 lot은 invalid Guid를 허용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	FGuid SourceId;

	/** 서로 합산 가능한 보상 종류. 현재 표면 오염은 Contamination을 사용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	FName RewardClass;

	/** 현재 운반체에서 이 lot이 차지하는 양. 유한한 0 이상 값만 허용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	double CarrierAmount;

	/** 최종 정분류로 회수 가능한 보상 단위. 유한한 0 이상 값만 허용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	double RewardUnits;
};

/**
 * 헤라 Load를 비울 때 실제로 빠진 관리량과 함께 이동한 가치 lot.
 * LIQ 구현 전에도 단일 원자 API의 출력 계약을 고정한다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeLoadTransferResult
{
	GENERATED_BODY()

public:
	FMD_ScrapeLoadTransferResult()
		: bSucceeded(false)
		, RemovedLoadAmount(0.0f)
		, RemovedRewardUnits(0.0)
	{
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	bool bSucceeded;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	float RemovedLoadAmount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	double RemovedRewardUnits;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Recovery")
	TArray<FMD_ScrapeRecoveryLot> RemovedLots;
};

/** 서버 권위 recovery lot 배열에 공통으로 쓰는 결정적·무손실 산술. */
struct MARINEDEMOLITION_API FMD_ScrapeRecoveryLedger
{
	static bool IsLotValid(const FMD_ScrapeRecoveryLot& Lot);
	static double GetTotalCarrierAmount(
		TConstArrayView<FMD_ScrapeRecoveryLot> Lots);
	static double GetTotalRewardUnits(
		TConstArrayView<FMD_ScrapeRecoveryLot> Lots);
	static bool AppendLot(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		const FMD_ScrapeRecoveryLot& Lot);
	static bool AppendScaledLots(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		TConstArrayView<FMD_ScrapeRecoveryLot> SourceLots,
		double Scale);
	static bool RebaseCarrierAmount(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		double NewTotalCarrierAmount);
	static double ExtractProportional(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		double RequestedCarrierAmount,
		TArray<FMD_ScrapeRecoveryLot>& OutExtractedLots);
	static bool AppendZeroRewardCarrier(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		double CarrierAmount);
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
 * 한 번의 표면 순제거 질량을 헤라 적재와 물리 드롭 사이에 나눈 결과.
 *
 * RequestedLoadAmount가 유효한 양수일 때만 성공한다. 성공 결과의 두 비율은
 * 항상 유한한 0~1이며 LoadOwnedRatio + PhysicalDropRatio == 1이다.
 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ScrapeMassAllocationResult
{
	GENERATED_BODY()

public:
	FMD_ScrapeMassAllocationResult()
		: bIsValid(false)
		, RequestedLoadAmount(0.0f)
		, ActualLoadAmount(0.0f)
		, LoadOwnedRatio(0.0f)
		, PhysicalDropRatio(0.0f)
	{
	}

	/** 요청량과 실제 수용량으로 안전한 배분을 계산했는지 여부. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	bool bIsValid;

	/** 이번 순제거량에서 헤라 적재로 바꾸려고 요청한 양. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	float RequestedLoadAmount;

	/** 0~100 상한을 적용한 뒤 헤라가 실제로 받아들인 양. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	float ActualLoadAmount;

	/** 순제거 질량 중 헤라 Load가 한 번 소유하는 몫. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	float LoadOwnedRatio;

	/** Load가 받지 못해 기존 물리 드롭 누적기로 보내는 나머지 몫. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	float PhysicalDropRatio;
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
		: SurfaceFace(EMD_ScrapeSurfaceFace::Front)
		, bIsInputValid(false)
		, bWasStrokeLongEnough(false)
		, bHadSurfaceContact(false)
		, ContactType(EMD_ScrapeContactType::None)
		, TouchedCellCount(0)
		, ModifiedCellCount(0)
		, NewlyCleanedCellCount(0)
		, RemovedContaminationAmount(0.0f)
		, NetRemovedContaminationAmount(0.0f)
		, RemovedBaseContaminationAmount(0.0f)
		, RemovedTransferredContaminationAmount(0.0f)
		, NetRemovedRewardUnits(0.0)
		, CleanRatioBefore(0.0f)
		, CleanRatioAfter(0.0f)
		, CleanRatioDelta(0.0f)
		, NetRemovedCleanRatioDelta(0.0f)
		, bIsSurfaceFullyClean(false)
		, bTouchedBareSurface(false)
	{
	}

	/** 이번 결과가 평가한 앞면 또는 뒷면. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape")
	EMD_ScrapeSurfaceFace SurfaceFace;

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

	/** 제거 또는 역오염 도포로 잔여 오염량이 실제로 바뀐 고유 셀 수. CellChanges.Num()과 동일한 값 */
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

	/**
	 * 같은 트랜잭션의 역오염 도포까지 뺀 실제 순제거 질량.
	 * 헤라 Load와 물리 드롭은 이 값 하나를 배분하며 gross 제거량을 다시 쓰지 않는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	float NetRemovedContaminationAmount;

	/** 서버 회계 전용 gross 기본층 제거량. RPC 피드백에는 직렬화하지 않는다. */
	float RemovedBaseContaminationAmount;

	/** 서버 회계 전용 gross 재부착층 제거량. RPC 피드백에는 직렬화하지 않는다. */
	float RemovedTransferredContaminationAmount;

	/** 역오염 재부착 뒤 현재 운반체 밖으로 실제 이동한 보상 단위. */
	double NetRemovedRewardUnits;

	/** 서버 회계 전용 provenance lot. 클라이언트 피드백 RPC에는 직렬화하지 않는다. */
	TArray<FMD_ScrapeRecoveryLot> NetRemovedRecoveryLots;

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
	 * CleanRatioDelta에서 같은 트랜잭션의 역오염 몫을 뺀 순제거율.
	 * 헤라가 요청할 Load 양은 이 값으로만 계산한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Scrape|Mass")
	float NetRemovedCleanRatioDelta;

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
