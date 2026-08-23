// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/Trash/MD_TrashBinSpillTypes.h"

#include "Engine/World.h"

namespace
{
	constexpr double SpillLiquidTolerance = 1.0e-9;
	constexpr double SpillRewardTolerance = 1.0e-12;
	constexpr double TiltDegreeTolerance = 1.0e-6;

	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X)
			&& FMath::IsFinite(Vector.Y)
			&& FMath::IsFinite(Vector.Z);
	}

	bool IsFiniteTransform(const FTransform& Transform)
	{
		if (Transform.ContainsNaN())
		{
			return false;
		}

		const FQuat Rotation = Transform.GetRotation();
		return FMath::IsFinite(Rotation.X)
			&& FMath::IsFinite(Rotation.Y)
			&& FMath::IsFinite(Rotation.Z)
			&& FMath::IsFinite(Rotation.W)
			&& Rotation.IsNormalized();
	}
}

bool FMD_TrashBinSpillTuning::IsValid() const
{
	return FMath::IsFinite(SettleSeconds)
		&& SettleSeconds > 0.0f
		&& FMath::IsFinite(SampleIntervalSeconds)
		&& SampleIntervalSeconds > 0.0f
		&& FMath::IsFinite(TiltThresholdDegrees)
		&& TiltThresholdDegrees >= 0.0f
		&& TiltThresholdDegrees <= 180.0f
		&& FMath::IsFinite(TiltHoldSeconds)
		&& TiltHoldSeconds >= 0.0f
		&& FMath::IsFinite(SpillUnitsPerPulse)
		&& SpillUnitsPerPulse > 0.0
		&& FMath::IsFinite(SustainedPulseIntervalSeconds)
		&& SustainedPulseIntervalSeconds > 0.0f
		&& FMath::IsFinite(ImpactImpulseThreshold)
		&& ImpactImpulseThreshold > 0.0
		&& FMath::IsFinite(PickupImpactIgnoreSeconds)
		&& PickupImpactIgnoreSeconds >= 0.0f
		&& FMath::IsFinite(ImpactDebounceSeconds)
		&& ImpactDebounceSeconds >= 0.0f
		&& FMath::IsFinite(FloorTraceDistanceCm)
		&& FloorTraceDistanceCm > 0.0f
		&& FMath::IsFinite(MinimumSupportUpDot)
		&& MinimumSupportUpDot >= -1.0f
		&& MinimumSupportUpDot <= 1.0f
		&& FMath::IsFinite(SettleRetryInitialBackoffSeconds)
		&& SettleRetryInitialBackoffSeconds > 0.0f
		&& FMath::IsFinite(SettleRetryMaximumBackoffSeconds)
		&& SettleRetryMaximumBackoffSeconds
			>= SettleRetryInitialBackoffSeconds;
}

bool FMD_TrashBinPerBinDedupeState::IsValid() const
{
	if (!StableBinId.IsValid())
	{
		return HighWatermark == 0 && AcceptedWindowBits == 0;
	}

	if (HighWatermark == 0 || (AcceptedWindowBits & uint64(1)) == 0)
	{
		return false;
	}

	if (HighWatermark < 64)
	{
		const uint64 ValidBits = (uint64(1) << HighWatermark) - 1;
		if ((AcceptedWindowBits & ~ValidBits) != 0)
		{
			return false;
		}
	}

	return true;
}

bool FMD_TrashBinPerBinDedupeState::WouldAccept(
	const FMD_TrashBinSpillTransactionId& TransactionId) const
{
	if (!IsValid() || !TransactionId.IsValid())
	{
		return false;
	}

	if (IsEmpty())
	{
		return true;
	}

	if (TransactionId.StableBinId != StableBinId)
	{
		return false;
	}

	if (TransactionId.Sequence > HighWatermark)
	{
		return true;
	}

	const uint64 Distance = HighWatermark - TransactionId.Sequence;
	if (Distance >= 64)
	{
		return false;
	}

	return (AcceptedWindowBits & (uint64(1) << Distance)) == 0;
}

bool FMD_TrashBinPerBinDedupeState::TryAccept(
	const FMD_TrashBinSpillTransactionId& TransactionId)
{
	if (!WouldAccept(TransactionId))
	{
		return false;
	}

	FGuid NewStableBinId = StableBinId;
	uint64 NewHighWatermark = HighWatermark;
	uint64 NewAcceptedWindowBits = AcceptedWindowBits;
	if (IsEmpty())
	{
		NewStableBinId = TransactionId.StableBinId;
		NewHighWatermark = TransactionId.Sequence;
		NewAcceptedWindowBits = 1;
	}
	else if (TransactionId.Sequence > HighWatermark)
	{
		const uint64 Advance = TransactionId.Sequence - HighWatermark;
		NewAcceptedWindowBits = Advance >= 64
			? uint64(1)
			: (AcceptedWindowBits << Advance) | uint64(1);
		NewHighWatermark = TransactionId.Sequence;
	}
	else
	{
		const uint64 Distance = HighWatermark - TransactionId.Sequence;
		NewAcceptedWindowBits |= uint64(1) << Distance;
	}

	StableBinId = NewStableBinId;
	HighWatermark = NewHighWatermark;
	AcceptedWindowBits = NewAcceptedWindowBits;
	return true;
}

bool FMD_TrashBinPerBinDedupeState::HasAccepted(
	const FMD_TrashBinSpillTransactionId& TransactionId) const
{
	if (!IsValid()
		|| IsEmpty()
		|| !TransactionId.IsValid()
		|| TransactionId.StableBinId != StableBinId
		|| TransactionId.Sequence > HighWatermark)
	{
		return false;
	}

	const uint64 Distance = HighWatermark - TransactionId.Sequence;
	return Distance >= 64
		|| (AcceptedWindowBits & (uint64(1) << Distance)) != 0;
}

void FMD_TrashBinPerBinDedupeState::Reset()
{
	StableBinId.Invalidate();
	HighWatermark = 0;
	AcceptedWindowBits = 0;
}

bool FMD_TrashBinSpillPayload::IsValid() const
{
	if (!FMath::IsFinite(LiquidUnits)
		|| LiquidUnits < 0.0
		|| !FMath::IsFinite(RewardUnits)
		|| RewardUnits < 0.0
		|| RewardUnits > LiquidUnits)
	{
		return false;
	}

	const double LotLiquidUnits =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(RecoveryLots);
	const double LotRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(RecoveryLots);
	return LotLiquidUnits >= 0.0
		&& LotRewardUnits >= 0.0
		&& FMath::IsNearlyEqual(
			LotLiquidUnits,
			LiquidUnits,
			SpillLiquidTolerance)
		&& FMath::IsNearlyEqual(
			LotRewardUnits,
			RewardUnits,
			SpillRewardTolerance);
}

bool FMD_TrashBinSpillCandidate::IsValid() const
{
	return TransactionId.IsValid()
		&& Trigger != EMD_TrashBinSpillTrigger::None
		&& ExpectedBinRevision >= 0
		&& Payload.LiquidUnits > 0.0
		&& Payload.IsValid();
}

bool FMD_TrashBinSpillContact::IsValid() const
{
	if (!IsFiniteVector(WorldLocation)
		|| !IsFiniteVector(WorldNormal)
		|| WorldNormal.IsNearlyZero()
		|| !SupportActorPath.IsValid()
		|| SupportComponentName.IsNone()
		|| !IsFiniteTransform(SupportLocalAnchor))
	{
		return false;
	}

	return FMath::IsNearlyEqual(
		WorldNormal.SizeSquared(),
		1.0,
		1.0e-4);
}

FSoftObjectPath FMD_TrashBinSpillContact::CanonicalizeSupportActorPath(
	const FSoftObjectPath& ActorPath)
{
	return FSoftObjectPath(UWorld::RemovePIEPrefix(ActorPath.ToString()));
}

bool FMD_TrashBinSpillPreparedSinkToken::IsValid() const
{
	return SinkId.IsValid()
		&& PreparationNonce > 0
		&& TransactionId.IsValid()
		&& ExpectedBinRevision >= 0
		&& FMath::IsFinite(PreparedLiquidUnits)
		&& PreparedLiquidUnits > 0.0
		&& FMath::IsFinite(PreparedRewardUnits)
		&& PreparedRewardUnits >= 0.0
		&& PreparedRewardUnits <= PreparedLiquidUnits;
}

bool FMD_TrashBinSpillPreparedSinkToken::Matches(
	const FMD_TrashBinSpillCandidate& Candidate) const
{
	return IsValid()
		&& Candidate.IsValid()
		&& TransactionId == Candidate.TransactionId
		&& ExpectedBinRevision == Candidate.ExpectedBinRevision
		&& FMath::IsNearlyEqual(
			PreparedLiquidUnits,
			Candidate.Payload.LiquidUnits,
			SpillLiquidTolerance)
		&& FMath::IsNearlyEqual(
			PreparedRewardUnits,
			Candidate.Payload.RewardUnits,
			SpillRewardTolerance);
}

double FMD_TrashBinSpillEvaluator::CalculateTiltDegrees(
	const FVector& BinUpVector)
{
	if (!IsFiniteVector(BinUpVector) || BinUpVector.IsNearlyZero())
	{
		return -1.0;
	}

	const FVector SafeUp = BinUpVector.GetSafeNormal();
	const double UpDot = FMath::Clamp(
		static_cast<double>(FVector::DotProduct(SafeUp, FVector::UpVector)),
		-1.0,
		1.0);
	return FMath::RadiansToDegrees(FMath::Acos(UpDot));
}

FMD_TrashBinTiltEvaluation FMD_TrashBinSpillEvaluator::EvaluateTilt(
	const FVector& BinUpVector,
	const double HeldSeconds,
	const FMD_TrashBinSpillTuning& Tuning)
{
	FMD_TrashBinTiltEvaluation Evaluation;
	Evaluation.TiltDegrees = CalculateTiltDegrees(BinUpVector);
	Evaluation.bInputValid = Tuning.IsValid()
		&& Evaluation.TiltDegrees >= 0.0
		&& FMath::IsFinite(HeldSeconds)
		&& HeldSeconds >= 0.0;
	if (!Evaluation.bInputValid)
	{
		Evaluation.TiltDegrees = 0.0;
		return Evaluation;
	}

	// acos round-trip이 정확한 60도를 59.999999999…로 만들어도
	// 승인 경계를 거부하지 않는 수준의 각도 오차만 허용한다.
	Evaluation.bMeetsAngle = Evaluation.TiltDegrees + TiltDegreeTolerance
		>= static_cast<double>(Tuning.TiltThresholdDegrees);
	Evaluation.bMeetsHold = HeldSeconds
		>= static_cast<double>(Tuning.TiltHoldSeconds);
	Evaluation.bCanPulse = Evaluation.bMeetsAngle && Evaluation.bMeetsHold;
	return Evaluation;
}

FMD_TrashBinImpactEvaluation FMD_TrashBinSpillEvaluator::EvaluateImpact(
	const double NormalImpulseMagnitude,
	const double SecondsSincePickup,
	const FMD_TrashBinSpillTuning& Tuning)
{
	FMD_TrashBinImpactEvaluation Evaluation;
	Evaluation.bInputValid = Tuning.IsValid()
		&& FMath::IsFinite(NormalImpulseMagnitude)
		&& NormalImpulseMagnitude >= 0.0
		&& FMath::IsFinite(SecondsSincePickup)
		&& SecondsSincePickup >= 0.0;
	if (!Evaluation.bInputValid)
	{
		return Evaluation;
	}

	Evaluation.bIgnoredAfterPickup = SecondsSincePickup
		< static_cast<double>(Tuning.PickupImpactIgnoreSeconds);
	Evaluation.bMeetsImpulse = NormalImpulseMagnitude
		>= Tuning.ImpactImpulseThreshold;
	Evaluation.bCanPulse = !Evaluation.bIgnoredAfterPickup
		&& Evaluation.bMeetsImpulse;
	return Evaluation;
}

double FMD_TrashBinSpillEvaluator::CalculateRequestedSpillUnits(
	const double CurrentLiquidUnits,
	const FMD_TrashBinSpillTuning& Tuning)
{
	if (!Tuning.IsValid()
		|| !FMath::IsFinite(CurrentLiquidUnits)
		|| CurrentLiquidUnits <= 0.0)
	{
		return 0.0;
	}

	return FMath::Min(CurrentLiquidUnits, Tuning.SpillUnitsPerPulse);
}
