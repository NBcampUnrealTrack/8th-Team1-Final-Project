// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "UObject/SoftObjectPath.h"
#include "MD_TrashBinSpillTypes.generated.h"

/** 쓰레기통 액체 쏟김의 서버 판정 값. */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_TrashBinSpillTuning
{
	GENERATED_BODY()

public:
	/** 구버전 저장 호환용 폐기 필드. Wet은 다른 상태로 굳지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "Wet 자국은 굳지 않습니다."))
	float SettleSeconds = 30.0f;

	/** Actor Tick 대신 쓰는 서버 기울기 표본 간격. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (ClampMin = "0.01", UIMin = "0.01", Units = "s"))
	float SampleIntervalSeconds = 0.05f;

	/** 통의 월드 Up과 통 Up 사이 각도가 이 값 이상이면 전량 유실 후보가 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0",
			UIMax = "180.0", Units = "deg"))
	float TiltThresholdDegrees = 70.0f;

	/** 구버전 Blueprint 직렬화 호환용 폐기 필드. 판정 코드는 읽지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "70도 상태 조건은 재무장 각도를 사용하지 않습니다."))
	float TiltRearmDegrees = 65.0f;

	/**
	 * 구버전 Blueprint 직렬화 호환용 폐기 필드. 현 계약은 임계각 이상에서
	 * 현재 원장 전량을 한 번 옮기므로 판정 코드가 이 값을 읽지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "임계각 전량 유실 계약에서는 사용하지 않습니다."))
	double SpillUnitsPerPulse = 5.0;

	/** 구버전 Blueprint 직렬화 호환용 폐기 필드. 반복 유실은 금지한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "임계각 전량 유실 계약에서는 반복하지 않습니다."))
	float SustainedPulseIntervalSeconds = 0.40f;

	/** 구버전 충격 진단 호환값. 현재 gameplay 유실 판정에는 사용하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double ImpactImpulseThreshold = 12000.0;

	/** 구버전 충격 진단 호환값. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float PickupImpactIgnoreSeconds = 0.60f;

	/** 구버전 충격 진단 호환값. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float ImpactDebounceSeconds = 0.20f;

	/** 통 아래에서 유효한 오염 부착 지지면을 찾는 최대 거리. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float FloorTraceDistanceCm = 150.0f;

	/** 구버전 Blueprint 직렬화 호환용 폐기 필드. SpillFloor hit만 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "SpillFloor hit에는 추가 경사 제한을 적용하지 않습니다."))
	float MinimumSupportUpDot = 0.50f;

	/** 구버전 저장 호환용 폐기 필드. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "Wet 정착 재시도는 사용하지 않습니다."))
	float SettleRetryInitialBackoffSeconds = 0.50f;

	/** 구버전 저장 호환용 폐기 필드. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MD|Trash Bin|Spill",
		meta = (DeprecatedProperty,
			DeprecationMessage = "Wet 정착 재시도는 사용하지 않습니다."))
	float SettleRetryMaximumBackoffSeconds = 4.0f;

	/** 현재 쏟김 판정에 쓰는 수치만 검사한다. */
	bool IsValid() const;
};

/** stable 통 하나가 발행한 한 쏟김 pulse의 영속 식별자. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinSpillTransactionId
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid StableBinId;

	/** 0은 미발행 상태이고 실제 transaction은 1부터 시작한다. */
	UPROPERTY()
	uint64 Sequence = 0;

	const FGuid& GetStableBinId() const
	{
		return StableBinId;
	}

	uint64 GetSequence() const
	{
		return Sequence;
	}

	bool IsValid() const
	{
		return StableBinId.IsValid() && Sequence > 0;
	}

	bool operator==(const FMD_TrashBinSpillTransactionId& Other) const
	{
		return StableBinId == Other.StableBinId
			&& Sequence == Other.Sequence;
	}

	bool operator!=(const FMD_TrashBinSpillTransactionId& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(
		const FMD_TrashBinSpillTransactionId& TransactionId)
	{
		return HashCombineFast(
			GetTypeHash(TransactionId.StableBinId),
			GetTypeHash(TransactionId.Sequence));
	}
};

/**
 * 한 stable 통에서 이미 적용한 transaction의 영속 재생 방지 상태.
 *
 * AcceptedWindowBits의 bit 0은 HighWatermark, bit N은 HighWatermark-N이다.
 * 창 밖으로 밀려난 옛 sequence는 "미확인"이 아니라 영구 retired로 취급해 다시
 * 적용하지 않는다. 창 안에서 늦게 도착한 아직 비어 있는 bit만 한 번 수용한다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinPerBinDedupeState
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid StableBinId;

	UPROPERTY()
	uint64 HighWatermark = 0;

	UPROPERTY()
	uint64 AcceptedWindowBits = 0;

	/** 비어 있는 기본값도 정상 상태다. */
	bool IsValid() const;

	bool IsEmpty() const
	{
		return !StableBinId.IsValid()
			&& HighWatermark == 0
			&& AcceptedWindowBits == 0;
	}

	/** 상태를 바꾸지 않고 새 ID를 한 번 수용할 수 있는지 검사한다. */
	bool WouldAccept(
		const FMD_TrashBinSpillTransactionId& TransactionId) const;

	/** 수용 가능한 ID만 기록한다. false일 때 세 필드는 모두 무변경이다. */
	bool TryAccept(
		const FMD_TrashBinSpillTransactionId& TransactionId);

	/** 창 안 중복과 창 밖 retired ID를 모두 true로 보고한다. */
	bool HasAccepted(
		const FMD_TrashBinSpillTransactionId& TransactionId) const;

	void Reset();
};

/** 한 쏟김 후보를 만든 서버 원인. */
UENUM()
enum class EMD_TrashBinSpillTrigger : uint8
{
	None,
	SustainedTilt,
	/** 구버전 식별자. 현재 후보·실행 경로는 항상 거부한다. */
	Impact
};

/** 통에서 Wet 수용측으로 원자 이동할 전체 액체·가치 원장. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinSpillPayload
{
	GENERATED_BODY()

public:
	UPROPERTY()
	double LiquidUnits = 0.0;

	UPROPERTY()
	double RewardUnits = 0.0;

	UPROPERTY()
	TArray<FMD_ScrapeRecoveryLot> RecoveryLots;

	/** aggregate 값과 전체 lot 합이 같은 유한·비음수 payload인지 검사한다. */
	bool IsValid() const;
};

/** 통 원장을 아직 바꾸지 않은 쏟김 차감 후보. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinSpillCandidate
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMD_TrashBinSpillTransactionId TransactionId;

	UPROPERTY()
	EMD_TrashBinSpillTrigger Trigger = EMD_TrashBinSpillTrigger::None;

	/** 후보를 만든 시점의 FMD_TrashBinLiquidState::Revision. */
	UPROPERTY()
	int32 ExpectedBinRevision = INDEX_NONE;

	UPROPERTY()
	FMD_TrashBinSpillPayload Payload;

	bool IsValid() const;
};

/** 유효 지지면 trace가 만든 결정적 접점과 저장 가능한 local anchor. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinSpillContact
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector WorldNormal = FVector::UpVector;

	/** PIE prefix 정규화는 접점을 만드는 trace helper가 담당한다. */
	UPROPERTY()
	FSoftObjectPath SupportActorPath;

	UPROPERTY()
	FName SupportComponentName = NAME_None;

	UPROPERTY()
	FTransform SupportLocalAnchor = FTransform::Identity;

	/** PIE world prefix를 제거해 같은 support를 world instance와 무관하게 비교한다. */
	static FSoftObjectPath CanonicalizeSupportActorPath(
		const FSoftObjectPath& ActorPath);

	bool IsValid() const;
};

/**
 * Wet 수용측이 후보를 준비했다는 일회성 증표.
 *
 * UObject 주소를 싣지 않고 저장 가능한 sink ID와 nonce를 사용한다. 실제 commit은
 * 이 token이 transaction·revision·payload와 모두 맞을 때만 시도한다.
 */
USTRUCT()
struct MARINEDEMOLITION_API FMD_TrashBinSpillPreparedSinkToken
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FGuid SinkId;

	UPROPERTY()
	uint64 PreparationNonce = 0;

	UPROPERTY()
	FMD_TrashBinSpillTransactionId TransactionId;

	UPROPERTY()
	int32 ExpectedBinRevision = INDEX_NONE;

	UPROPERTY()
	double PreparedLiquidUnits = 0.0;

	UPROPERTY()
	double PreparedRewardUnits = 0.0;

	bool IsValid() const;

	bool Matches(const FMD_TrashBinSpillCandidate& Candidate) const;
};

/** 서버의 현재 통 Up과 hold 시간을 순수 계산한 결과. */
struct MARINEDEMOLITION_API FMD_TrashBinTiltEvaluation
{
	bool bInputValid = false;
	double TiltDegrees = 0.0;
	bool bMeetsAngle = false;
	bool bMeetsHold = false;
	bool bCanPulse = false;
};

/** 구버전 Hit 진단 결과. bCanPulse는 현재 계약에서 항상 false다. */
struct MARINEDEMOLITION_API FMD_TrashBinImpactEvaluation
{
	bool bInputValid = false;
	bool bIgnoredAfterPickup = false;
	bool bMeetsImpulse = false;
	bool bCanPulse = false;
};

/** 월드 상태를 참조하거나 바꾸지 않는 쏟김 판정 수학. */
class MARINEDEMOLITION_API FMD_TrashBinSpillEvaluator final
{
public:
	/** 유효하지 않은 Up vector이면 -1을 반환한다. */
	static double CalculateTiltDegrees(const FVector& BinUpVector);

	static FMD_TrashBinTiltEvaluation EvaluateTilt(
		const FVector& BinUpVector,
		double HeldSeconds,
		const FMD_TrashBinSpillTuning& Tuning);

	static FMD_TrashBinImpactEvaluation EvaluateImpact(
		double NormalImpulseMagnitude,
		double SecondsSincePickup,
		const FMD_TrashBinSpillTuning& Tuning);

	/** 현재 통의 유한·양수 액체 전량을 반환한다. */
	static double CalculateRequestedSpillUnits(
		double CurrentLiquidUnits,
		const FMD_TrashBinSpillTuning& Tuning);

private:
	FMD_TrashBinSpillEvaluator() = delete;
};
