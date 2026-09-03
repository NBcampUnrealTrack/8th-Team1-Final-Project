// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Gameplay/Scrape/MD_ScrapeBladeAxis.h"
#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"

void UMD_ScrapeToolComponent::BeginStrokeTracking(
	UMD_ScrapeSurfaceComponent* Surface,
	const FVector2D& SurfacePositionCm,
	const EMD_ScrapeSurfaceFace SurfaceFace)
{
	FlushPendingScrapePathApplication();
	FlushPendingContaminationPaintTail();
	StrokePath.bHasPreviousSurfacePosition = true;
	StrokePath.bHasLockedStrokeAxis = false;
	StrokePath.PreviousSurfacePositionCm = SurfacePositionCm;
	StrokePath.PreviousSurfaceFace = SurfaceFace;
	StrokePath.StrokeAnchorPositionCm = SurfacePositionCm;
	StrokePath.FurthestSurfacePositionCm = SurfacePositionCm;
	StrokePath.LockedStrokeAxis = FVector2D::ZeroVector;
	StrokePath.PreviousSurface = Surface;
	// 최초 anchor는 현재 generation을 그대로 쓴다. 표면·면 hard 전환은
	// 이전 tracking을 정리하는 경로에서 새 generation을 이미 발급한다.
	ResetScrapePathAccumulator(SurfacePositionCm);
}

bool UMD_ScrapeToolComponent::ConsumeRawScrapePathPoint(
	const FVector2D& RawSurfacePositionCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	OutSegments.Reset();
	if (!StrokePath.bHasPreviousSurfacePosition || RawSurfacePositionCm.ContainsNaN())
	{
		return false;
	}

	const FVector2D RawDelta =
		RawSurfacePositionCm - StrokePath.PreviousSurfacePositionCm;
	const double RawTravelCm = RawDelta.Size();
	if (!FMath::IsFinite(RawTravelCm) || RawTravelCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D RawTravelTangent = RawDelta / RawTravelCm;
	if (StrokePath.bHasLastRawTravelTangent)
	{
		const double SignedDirectionDot = FMath::Clamp(
			FVector2D::DotProduct(
				StrokePath.LastRawTravelTangent,
				RawTravelTangent),
			-1.0,
			1.0);
		const double EquivalentDirectionDot = FMath::Clamp(
			FMath::Abs(SignedDirectionDot),
			0.0,
			1.0);
		const double CornerDegrees = FMath::RadiansToDegrees(
			FMath::Acos(EquivalentDirectionDot));
		if (CornerDegrees >= FMath::Clamp(
				static_cast<double>(LargeCornerSplitDegrees),
				0.0,
				90.0))
		{
			if (!StrokePath.bHasStableBladeDepthAxis
				&& StrokePath.PendingScrapePolylineCm.Num() >= 2)
			{
				// threshold보다 짧아도 명시 basis 계약으로 이전 raw 선분을 먼저 보존한다.
				ResolvePendingScrapePath(OutSegments);
			}
			FlushResolvedScrapePathTail(
				StrokePath.PreviousSurfacePositionCm,
				OutSegments);
			// 공통 꼭짓점에서 상태만 나눈다. 이전/다음 점을 잇는 대각선은 만들지 않는다.
			FMD_ScrapeToolPathSegment& Boundary =
				OutSegments.AddDefaulted_GetRef();
			Boundary.StartPositionCm = StrokePath.PreviousSurfacePositionCm;
			Boundary.EndPositionCm = StrokePath.PreviousSurfacePositionCm;
			Boundary.StartBladeDepthAxis = StrokePath.StableBladeDepthAxis;
			Boundary.EndBladeDepthAxis = StrokePath.StableBladeDepthAxis;
			Boundary.bStartsNewSubStroke = true;
			ResetScrapePathAccumulator(StrokePath.PreviousSurfacePositionCm);
		}
		else if (SignedDirectionDot < -UE_KINDA_SMALL_NUMBER)
		{
			// 180도 왕복은 같은 sub-stroke지만 꼭짓점을 건너 chord로 펴면 안 된다.
			FlushResolvedScrapePathTail(
				StrokePath.PreviousSurfacePositionCm,
				OutSegments);
		}
	}

	StrokePath.LastRawTravelTangent = RawTravelTangent;
	StrokePath.bHasLastRawTravelTangent = true;
	if (!StrokePath.bHasStableBladeDepthAxis)
	{
		if (StrokePath.PendingScrapePolylineCm.IsEmpty())
		{
			StrokePath.PendingScrapePolylineCm.Add(StrokePath.PreviousSurfacePositionCm);
		}
		StrokePath.PendingScrapePolylineCm.Add(RawSurfacePositionCm);
		StrokePath.PendingScrapePathArcLengthCm += RawTravelCm;
		if (StrokePath.PendingScrapePathArcLengthCm < FMath::Max(
				static_cast<double>(DirectionLockThresholdCm),
				static_cast<double>(UE_SMALL_NUMBER)))
		{
			return !OutSegments.IsEmpty();
		}

		ResolvePendingScrapePath(OutSegments);
		return !OutSegments.IsEmpty();
	}

	AppendResolvedScrapePathSegment(
		StrokePath.PreviousSurfacePositionCm,
		RawSurfacePositionCm,
		OutSegments);
	return !OutSegments.IsEmpty();
}

void UMD_ScrapeToolComponent::ResolvePendingScrapePath(
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	if (StrokePath.PendingScrapePolylineCm.Num() < 2)
	{
		return;
	}

	const FVector2D InitialAxis =
		(StrokePath.PendingScrapePolylineCm.Last() - StrokePath.PendingScrapePolylineCm[0])
		.GetSafeNormal();
	StrokePath.StableBladeDepthAxis = InitialAxis.IsNearlyZero()
		? StrokePath.LastRawTravelTangent.GetSafeNormal()
		: InitialAxis;
	if (StrokePath.StableBladeDepthAxis.IsNearlyZero())
	{
		return;
	}

	StrokePath.bHasStableBladeDepthAxis = true;
	StrokePath.LockedStrokeAxis = StrokePath.StableBladeDepthAxis;
	StrokePath.bHasLockedStrokeAxis = true;
	StrokePath.StrokeAnchorPositionCm = StrokePath.PendingScrapePolylineCm[0];
	StrokePath.FurthestSurfacePositionCm = StrokePath.StrokeAnchorPositionCm;
	StrokePath.BladeAxisWindow.Reset();
	StrokePath.ScrapePathArcLengthCm = 0.0;
	StrokePath.BladeAxisWindow.AddSample(
		StrokePath.PendingScrapePolylineCm[0],
		0.0,
		BladeDirectionWindowCm,
		PathDirectionResampleSpacingCm);
	StrokePath.LastEmittedScrapePathArcLengthCm = 0.0;
	StrokePath.LastEmittedScrapePathPositionCm = StrokePath.PendingScrapePolylineCm[0];
	StrokePath.PendingResolvedScrapePathPointsCm.Reset();
	StrokePath.PendingResolvedScrapePathPointsCm.Add(StrokePath.PendingScrapePolylineCm[0]);
	StrokePath.PendingResolvedScrapePathTravelCm = 0.0;
	StrokePath.LastEmittedBladeDepthAxis = StrokePath.StableBladeDepthAxis;

	const TArray<FVector2D> BufferedPolyline = StrokePath.PendingScrapePolylineCm;
	StrokePath.PendingScrapePolylineCm.Reset();
	StrokePath.PendingScrapePathArcLengthCm = 0.0;
	for (int32 PointIndex = 1; PointIndex < BufferedPolyline.Num(); ++PointIndex)
	{
		AppendResolvedScrapePathSegment(
			BufferedPolyline[PointIndex - 1],
			BufferedPolyline[PointIndex],
			OutSegments);
	}
}

void UMD_ScrapeToolComponent::RestartPendingResolvedScrapePath(
	const FVector2D& StartPositionCm,
	const double SegmentStartArcCm)
{
	if (StrokePath.PendingResolvedScrapePathPointsCm.IsEmpty())
	{
		StrokePath.PendingResolvedScrapePathPointsCm.Add(StartPositionCm);
	}
	else if (!StrokePath.PendingResolvedScrapePathPointsCm.Last().Equals(
		StartPositionCm,
		UE_SMALL_NUMBER))
	{
		// 직접 호출자가 불연속 지점을 넘겨도 하나의 긁기 선분으로 잇지 않는다.
		// 아직 방출하지 않은 꼬리만 버리고 이 지점에서 다시 시작한다.
		StrokePath.PendingResolvedScrapePathPointsCm.Reset();
		StrokePath.PendingResolvedScrapePathPointsCm.Add(StartPositionCm);
		StrokePath.PendingResolvedScrapePathTravelCm = 0.0;
		StrokePath.LastEmittedScrapePathPositionCm = StartPositionCm;
		StrokePath.LastEmittedScrapePathArcLengthCm = SegmentStartArcCm;
		StrokePath.LastEmittedBladeDepthAxis = StrokePath.StableBladeDepthAxis;
	}
}

FVector2D UMD_ScrapeToolComponent::ConsumePendingResolvedScrapePathChunk(
	const double SpacingCm,
	TArray<FVector2D, TInlineAllocator<8>>& OutConsumedPointsCm)
{
	double RemainingSampleTravelCm = SpacingCm;
	FVector2D SamplePositionCm = StrokePath.PendingResolvedScrapePathPointsCm[0];
	OutConsumedPointsCm.Reset();
	OutConsumedPointsCm.Add(SamplePositionCm);
	while (RemainingSampleTravelCm > UE_SMALL_NUMBER
		&& StrokePath.PendingResolvedScrapePathPointsCm.Num() >= 2)
	{
		const FVector2D LegStartCm = StrokePath.PendingResolvedScrapePathPointsCm[0];
		const FVector2D LegEndCm = StrokePath.PendingResolvedScrapePathPointsCm[1];
		const double LegTravelCm = FVector2D::Distance(
			LegStartCm,
			LegEndCm);
		if (LegTravelCm <= UE_SMALL_NUMBER)
		{
			StrokePath.PendingResolvedScrapePathPointsCm.RemoveAt(
				0,
				1,
				EAllowShrinking::No);
			continue;
		}
		if (LegTravelCm <= RemainingSampleTravelCm + UE_SMALL_NUMBER)
		{
			SamplePositionCm = LegEndCm;
			OutConsumedPointsCm.Add(SamplePositionCm);
			RemainingSampleTravelCm = FMath::Max(
				RemainingSampleTravelCm - LegTravelCm,
				0.0);
			StrokePath.PendingResolvedScrapePathPointsCm.RemoveAt(
				0,
				1,
				EAllowShrinking::No);
		}
		else
		{
			const double Alpha = RemainingSampleTravelCm / LegTravelCm;
			SamplePositionCm = FMath::Lerp(LegStartCm, LegEndCm, Alpha);
			OutConsumedPointsCm.Add(SamplePositionCm);
			StrokePath.PendingResolvedScrapePathPointsCm[0] = SamplePositionCm;
			RemainingSampleTravelCm = 0.0;
		}
	}
	return SamplePositionCm;
}

void UMD_ScrapeToolComponent::EmitResolvedScrapePathParts(
	const TConstArrayView<FVector2D> ConsumedPointsCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	FVector2D PartStartPositionCm = StrokePath.LastEmittedScrapePathPositionCm;
	for (int32 ConsumedIndex = 1;
		ConsumedIndex < ConsumedPointsCm.Num();
		++ConsumedIndex)
	{
		const FVector2D PartEndPositionCm = ConsumedPointsCm[ConsumedIndex];
		const double PartTravelCm = FVector2D::Distance(
			PartStartPositionCm,
			PartEndPositionCm);
		if (PartTravelCm > UE_SMALL_NUMBER)
		{
			FMD_ScrapeToolPathSegment& Part =
				OutSegments.AddDefaulted_GetRef();
			Part.StartPositionCm = PartStartPositionCm;
			Part.EndPositionCm = PartEndPositionCm;
			Part.StartBladeDepthAxis = StrokePath.LastEmittedBladeDepthAxis;
			Part.EndBladeDepthAxis = StrokePath.StableBladeDepthAxis;
			Part.TravelDistanceCm = static_cast<float>(PartTravelCm);
		}
		PartStartPositionCm = PartEndPositionCm;
	}
}

void UMD_ScrapeToolComponent::AppendResolvedScrapePathSegment(
	const FVector2D& StartPositionCm,
	const FVector2D& EndPositionCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	const FVector2D Delta = EndPositionCm - StartPositionCm;
	const double SegmentTravelCm = Delta.Size();
	if (!FMath::IsFinite(SegmentTravelCm) || SegmentTravelCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	const double SegmentStartArcCm = StrokePath.ScrapePathArcLengthCm;
	const double SegmentEndArcCm = SegmentStartArcCm + SegmentTravelCm;
	const double SafeSpacingCm = FMath::Max(
		static_cast<double>(PathDirectionResampleSpacingCm),
		0.01);

	RestartPendingResolvedScrapePath(StartPositionCm, SegmentStartArcCm);

	// Raw aim frequency must not become an evaluator boundary. Extend a pending
	// leg while travel continues in the same direction, but keep turnarounds as
	// explicit points so sub-spacing A-B-A movement is not chorded away.
	bool bExtendedPendingLeg = false;
	if (StrokePath.PendingResolvedScrapePathPointsCm.Num() >= 2
		&& StrokePath.PendingResolvedScrapePathPointsCm.Last().Equals(
			StartPositionCm,
			UE_SMALL_NUMBER))
	{
		const FVector2D PendingLeg =
			StrokePath.PendingResolvedScrapePathPointsCm.Last()
			- StrokePath.PendingResolvedScrapePathPointsCm[
				StrokePath.PendingResolvedScrapePathPointsCm.Num() - 2];
		if (FVector2D::DotProduct(PendingLeg, Delta)
			> UE_DOUBLE_SMALL_NUMBER)
		{
			StrokePath.PendingResolvedScrapePathPointsCm.Last() = EndPositionCm;
			bExtendedPendingLeg = true;
		}
	}
	if (!bExtendedPendingLeg)
	{
		StrokePath.PendingResolvedScrapePathPointsCm.Add(EndPositionCm);
	}
	StrokePath.PendingResolvedScrapePathTravelCm += SegmentTravelCm;

	while (StrokePath.PendingResolvedScrapePathTravelCm
		>= SafeSpacingCm - UE_SMALL_NUMBER
		&& StrokePath.PendingResolvedScrapePathPointsCm.Num() >= 2)
	{
		TArray<FVector2D, TInlineAllocator<8>> ConsumedChunkPointsCm;
		const FVector2D SamplePositionCm = ConsumePendingResolvedScrapePathChunk(
			SafeSpacingCm,
			ConsumedChunkPointsCm);

		const double SampleArcCm = StrokePath.LastEmittedScrapePathArcLengthCm
			+ SafeSpacingCm;
		StrokePath.BladeAxisWindow.AddSample(
			SamplePositionCm,
			SampleArcCm,
			BladeDirectionWindowCm,
			PathDirectionResampleSpacingCm);
		UpdateStableBladeAxis(SafeSpacingCm);

		EmitResolvedScrapePathParts(ConsumedChunkPointsCm, OutSegments);
		StrokePath.LastEmittedScrapePathPositionCm = SamplePositionCm;
		StrokePath.LastEmittedScrapePathArcLengthCm = SampleArcCm;
		StrokePath.LastEmittedBladeDepthAxis = StrokePath.StableBladeDepthAxis;
		StrokePath.PendingResolvedScrapePathTravelCm = FMath::Max(
			StrokePath.PendingResolvedScrapePathTravelCm - SafeSpacingCm,
			0.0);
	}
	StrokePath.ScrapePathArcLengthCm = SegmentEndArcCm;
}

void UMD_ScrapeToolComponent::FlushResolvedScrapePathTail(
	const FVector2D& EndPositionCm,
	TArray<FMD_ScrapeToolPathSegment>& OutSegments)
{
	if (!StrokePath.bHasStableBladeDepthAxis || EndPositionCm.ContainsNaN())
	{
		return;
	}

	const double TailTravelCm =
		StrokePath.ScrapePathArcLengthCm - StrokePath.LastEmittedScrapePathArcLengthCm;
	if (TailTravelCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	const int32 InitialSegmentCount = OutSegments.Num();
	FVector2D TailStartPositionCm = StrokePath.LastEmittedScrapePathPositionCm;
	FVector2D TailStartBladeAxis = StrokePath.LastEmittedBladeDepthAxis;
	for (int32 PointIndex = 1;
		PointIndex < StrokePath.PendingResolvedScrapePathPointsCm.Num();
		++PointIndex)
	{
		const FVector2D TailEndPositionCm =
			StrokePath.PendingResolvedScrapePathPointsCm[PointIndex];
		const double LegTravelCm = FVector2D::Distance(
			TailStartPositionCm,
			TailEndPositionCm);
		if (LegTravelCm <= UE_SMALL_NUMBER)
		{
			continue;
		}

		FMD_ScrapeToolPathSegment& Tail = OutSegments.AddDefaulted_GetRef();
		Tail.StartPositionCm = TailStartPositionCm;
		Tail.EndPositionCm = TailEndPositionCm;
		Tail.StartBladeDepthAxis = TailStartBladeAxis;
		Tail.EndBladeDepthAxis = StrokePath.StableBladeDepthAxis;
		Tail.TravelDistanceCm = static_cast<float>(LegTravelCm);
		TailStartPositionCm = TailEndPositionCm;
		TailStartBladeAxis = StrokePath.StableBladeDepthAxis;
	}
	if (!TailStartPositionCm.Equals(EndPositionCm, UE_SMALL_NUMBER))
	{
		const double FinalLegTravelCm = FVector2D::Distance(
			TailStartPositionCm,
			EndPositionCm);
		if (FinalLegTravelCm > UE_SMALL_NUMBER)
		{
			FMD_ScrapeToolPathSegment& Tail = OutSegments.AddDefaulted_GetRef();
			Tail.StartPositionCm = TailStartPositionCm;
			Tail.EndPositionCm = EndPositionCm;
			Tail.StartBladeDepthAxis = TailStartBladeAxis;
			Tail.EndBladeDepthAxis = StrokePath.StableBladeDepthAxis;
			Tail.TravelDistanceCm = static_cast<float>(FinalLegTravelCm);
		}
	}
	if (OutSegments.Num() == InitialSegmentCount)
	{
		return;
	}

	StrokePath.LastEmittedScrapePathPositionCm = EndPositionCm;
	StrokePath.LastEmittedScrapePathArcLengthCm = StrokePath.ScrapePathArcLengthCm;
	StrokePath.LastEmittedBladeDepthAxis = StrokePath.StableBladeDepthAxis;
	StrokePath.PendingResolvedScrapePathPointsCm.Reset();
	StrokePath.PendingResolvedScrapePathPointsCm.Add(EndPositionCm);
	StrokePath.PendingResolvedScrapePathTravelCm = 0.0;
}

void UMD_ScrapeToolComponent::FlushPendingScrapePathApplication()
{
	UMD_ScrapeSurfaceComponent* const Surface = StrokePath.PreviousSurface.Get();
	if (!StrokePath.bHasPreviousSurfacePosition
		|| !StrokePath.bHasLastScrapeSurfaceHit
		|| !IsValid(Surface))
	{
		return;
	}

	TArray<FMD_ScrapeToolPathSegment> TailSegments;
	FlushResolvedScrapePathTail(StrokePath.PreviousSurfacePositionCm, TailSegments);
	for (const FMD_ScrapeToolPathSegment& TailSegment : TailSegments)
	{
		ApplyScrapePathSegment(
			Surface,
			StrokePath.PreviousSurfaceFace,
			StrokePath.LastScrapeSurfaceHit,
			TailSegment);
	}
}

void UMD_ScrapeToolComponent::SplitScrapeSubStrokeAtCorner(
	const FVector2D& CornerPositionCm)
{
	FlushPendingContaminationPaintTail();
	AdvanceStage3SubStrokeGeneration();
	StrokePath.StrokeAnchorPositionCm = CornerPositionCm;
	StrokePath.FurthestSurfacePositionCm = CornerPositionCm;
}

void UMD_ScrapeToolComponent::ResetScrapePathAccumulator(
	const FVector2D& StartPositionCm)
{
	StrokePath.PendingScrapePolylineCm.Reset();
	StrokePath.PendingScrapePolylineCm.Add(StartPositionCm);
	StrokePath.PendingScrapePathArcLengthCm = 0.0;
	StrokePath.BladeAxisWindow.Reset();
	StrokePath.ScrapePathArcLengthCm = 0.0;
	StrokePath.LastEmittedScrapePathArcLengthCm = 0.0;
	StrokePath.PendingResolvedScrapePathPointsCm.Reset();
	StrokePath.PendingResolvedScrapePathPointsCm.Add(StartPositionCm);
	StrokePath.PendingResolvedScrapePathTravelCm = 0.0;
	StrokePath.LastEmittedScrapePathPositionCm = StartPositionCm;
	StrokePath.LastEmittedBladeDepthAxis = FVector2D::ZeroVector;
	StrokePath.StableBladeDepthAxis = FVector2D::ZeroVector;
	StrokePath.LastRawTravelTangent = FVector2D::ZeroVector;
	StrokePath.bHasStableBladeDepthAxis = false;
	StrokePath.bHasLastRawTravelTangent = false;
}

void UMD_ScrapeToolComponent::UpdateStableBladeAxis(const double ArcStepCm)
{
	// 창이 아직 방향을 못 만들면 직전 축을 그대로 목표로 삼아 회전량이 0이 된다.
	FVector2D Candidate = StrokePath.StableBladeDepthAxis;
	StrokePath.BladeAxisWindow.TryCalculateWindowedAxis(
		BladeDirectionWindowCm,
		Candidate);
	Candidate = Candidate.GetSafeNormal();
	if (Candidate.IsNearlyZero())
	{
		return;
	}

	const double MaximumRadians = FMath::DegreesToRadians(
		FMath::Max(static_cast<double>(MaximumBladeAxisTurnDegreesPerCm), 0.0)
		* FMath::Max(ArcStepCm, 0.0));
	StrokePath.StableBladeDepthAxis = FMD_ScrapeBladeAxis::RotateTowards(
		StrokePath.StableBladeDepthAxis,
		Candidate,
		MaximumRadians);
	StrokePath.LockedStrokeAxis = StrokePath.StableBladeDepthAxis;
	StrokePath.bHasLockedStrokeAxis = true;
}

void UMD_ScrapeToolComponent::ApplyAuthoritativeScrapeStroke(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FMD_ScrapeToolPathSegment& PathSegment,
	const FMD_ScrapeStroke& Stroke,
	const FVector& FeedbackLocation)
{
	// Exact Stage3 transfer is completed inside the Surface transaction. The
	// ordered-path argument remains part of the caller contract for consistency.
	(void)PathSegment;

	const double CurrentRecoveryCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			ScrapeLoadRecoveryLots);
	const double CurrentRecoveryRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			ScrapeLoadRecoveryLots);
	if (!IsValid(Surface)
		|| SurfaceFace != Stroke.SurfaceFace
		|| !FMath::IsFinite(ScrapeLoad)
		|| ScrapeLoad < 0.0f
		|| ScrapeLoad > MaximumScrapeLoad
		|| CurrentRecoveryCarrierAmount < 0.0
		|| CurrentRecoveryRewardUnits < 0.0
		|| !FMath::IsNearlyEqual(
			CurrentRecoveryCarrierAmount,
			static_cast<double>(ScrapeLoad),
			1.0e-9)
		|| !Surface->CanAcceptRecoveryTransaction(SurfaceFace))
	{
		return;
	}

	const FMD_ScrapeResult Result =
		ApplyScrapeStrokeForCurrentLoad(Surface, Stroke);
	const bool bWasSurfaceTransactionApplied =
		Result.bIsInputValid && Result.bWasStrokeLongEnough;
	if (!bWasSurfaceTransactionApplied)
	{
		return;
	}

	const double ResultCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			Result.NetRemovedRecoveryLots);
	const double ResultRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			Result.NetRemovedRecoveryLots);
	const double ExpectedCarrierAmount =
		static_cast<double>(Result.NetRemovedContaminationAmount);
	const double CarrierTolerance = FMath::Max(
		1.0e-9,
		FMath::Abs(ExpectedCarrierAmount) * 1.0e-6);
	const double RewardTolerance = FMath::Max(
		1.0e-12,
		FMath::Abs(Result.NetRemovedRewardUnits) * 1.0e-9);
	if (!FMath::IsFinite(Result.NetRemovedCleanRatioDelta)
		|| !FMath::IsFinite(ExpectedCarrierAmount)
		|| ExpectedCarrierAmount < 0.0
		|| !FMath::IsFinite(Result.NetRemovedRewardUnits)
		|| Result.NetRemovedRewardUnits < 0.0
		|| ResultCarrierAmount < 0.0
		|| ResultRewardUnits < 0.0
		|| !FMath::IsNearlyEqual(
			ResultCarrierAmount,
			ExpectedCarrierAmount,
			CarrierTolerance)
		|| !FMath::IsNearlyEqual(
			ResultRewardUnits,
			Result.NetRemovedRewardUnits,
			RewardTolerance))
	{
		ensureMsgf(
			false,
			TEXT("Scrape Surface returned an invalid recovery provenance ledger."));
		return;
	}

	const float RequestedLoadAmount =
		CalculateScrapeLoadGainFromCleanRatioDelta(
			Result.NetRemovedCleanRatioDelta,
			CanonicalScrapeLoadPerRemovedPercent);
	const float PreviousScrapeLoad = ScrapeLoad;
	TArray<FMD_ScrapeRecoveryLot> PreviousRecoveryLots =
		ScrapeLoadRecoveryLots;
	// Load 값은 먼저 확정하되 delegate/복제 알림은 물리 잔여 몫까지 같은
	// transaction에 기록한 뒤 보낸다. 알림 콜백이 중간 상태를 볼 수 없다.
	float ActualLoadAmount = 0.0f;
	if (RequestedLoadAmount > 0.0f
		&& !TryAddScrapeLoadWithRecoveryInternal(
			RequestedLoadAmount,
			Result.NetRemovedRecoveryLots,
			false,
			ActualLoadAmount))
	{
		ensureMsgf(
			false,
			TEXT("Scrape Load rejected a valid Surface recovery ledger."));
		return;
	}
	FMD_ScrapeMassAllocationResult MassAllocation;
	if (RequestedLoadAmount > 0.0f)
	{
		MassAllocation = CalculateScrapeMassAllocation(
			RequestedLoadAmount,
			ActualLoadAmount);
	}
	else
	{
		// 초기 오염량이 0인 transferred-only 등 Load 백분율 분모를 만들 수
		// 없는 정상 제거는 invalid 결과와 섞지 않고 명시적으로 물리 100%다.
		MassAllocation.bIsValid = true;
		MassAllocation.PhysicalDropRatio = 1.0f;
	}

	// MASS-012: Surface 결과의 동일 provenance lot은 LoadOwnedRatio만 Tool에
	// 들어왔고, 아래 커밋이 나머지 PhysicalDropRatio를 물리 원장에 넘긴다.
	const UMD_ScrapeSurfaceComponent::FPhysicalCommitResult CommitResult =
		Surface->CommitContaminationPhysicalDrop(
			Result,
			MassAllocation,
			Stroke.EndPositionCm);
	if (!ensureMsgf(
		CommitResult.IsAccepted(),
		TEXT("Scrape physical mass was rejected after the Surface transaction.")))
	{
		// Tool은 아직 외부 알림을 보내지 않았으므로 원장과 float를 함께 되돌린다.
		ScrapeLoad = PreviousScrapeLoad;
		ScrapeLoadRecoveryLots = MoveTemp(PreviousRecoveryLots);
		return;
	}
	if (ActualLoadAmount > 0.0f)
	{
		NotifyScrapeLoadChangedAndForceNetUpdate();
	}
	Surface->FinalizeLocalContaminationPath();
	DispatchAuthoritativeScrapeResult(
		Surface,
		Result,
		FeedbackLocation);
}

FMD_ScrapeResult UMD_ScrapeToolComponent::ApplyScrapeStrokeForCurrentLoad(
	UMD_ScrapeSurfaceComponent* const Surface,
	const FMD_ScrapeStroke& Stroke) const
{
	if (!IsValid(Surface))
	{
		return FMD_ScrapeResult();
	}

	if (GetScrapeLoadStage() == EMD_ScrapeLoadStage::Stage3)
	{
		float GrossRemovedCleanRatioDelta = 0.0f;
		return Surface->ApplyLocalScrapeStrokeWithContaminationStamp(
			Stroke,
			ScrapeToolSpec,
			SaturatedReverseContaminationRatio,
			GrossRemovedCleanRatioDelta,
			true);
	}

	return Surface->ApplyLocalScrapeStrokeWithDeferredCompletion(
		Stroke,
		ScrapeToolSpec);
}

void UMD_ScrapeToolComponent::ApplyScrapePathSegment(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FHitResult& CurrentHit,
	const FMD_ScrapeToolPathSegment& PathSegment)
{
	if (!IsValid(Surface))
	{
		return;
	}

	FMD_ScrapeStroke Stroke;
	Stroke.SurfaceFace = SurfaceFace;
	Stroke.StartPositionCm = PathSegment.StartPositionCm;
	Stroke.EndPositionCm = PathSegment.EndPositionCm;
	Stroke.TravelDistanceCm = PathSegment.TravelDistanceCm;
	Stroke.StartBladeBasis = FMD_ScrapeBladeAxis::MakeBasis(
		PathSegment.StartBladeDepthAxis);
	Stroke.EndBladeBasis = FMD_ScrapeBladeAxis::MakeBasis(
		PathSegment.EndBladeDepthAxis);
	const double SegmentChordCm = FVector2D::Distance(
		PathSegment.StartPositionCm,
		PathSegment.EndPositionCm);
	StrokePath.LockedStrokeAxis = PathSegment.EndBladeDepthAxis;
	StrokePath.bHasLockedStrokeAxis = !StrokePath.LockedStrokeAxis.IsNearlyZero();

	FVector EndWorldPosition = CurrentHit.ImpactPoint;
	const bool bHasEndWorldPosition =
		Surface->TryGetScrapeSurfaceWorldPositionAtDepth(
			PathSegment.EndPositionCm,
			SurfaceFace,
			CurrentHit.ImpactPoint,
			EndWorldPosition);

	if (SegmentChordCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	if (StrokePath.bHasLockedStrokeAxis
		&& FVector2D::DotProduct(
			PathSegment.EndPositionCm - StrokePath.StrokeAnchorPositionCm,
			StrokePath.LockedStrokeAxis)
			> FVector2D::DotProduct(
				StrokePath.FurthestSurfacePositionCm - StrokePath.StrokeAnchorPositionCm,
				StrokePath.LockedStrokeAxis))
	{
		StrokePath.FurthestSurfacePositionCm = PathSegment.EndPositionCm;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ApplyAuthoritativeScrapeStroke(
			Surface,
			SurfaceFace,
			PathSegment,
			Stroke,
			bHasEndWorldPosition
				? EndWorldPosition
				: FVector(CurrentHit.ImpactPoint));
	}
	else if (GetScrapeLoadStage() != EMD_ScrapeLoadStage::Stage3
		&& ActiveLocalScrapePredictionSessionId != 0
		&& ActiveLocalScrapePredictionAimSequence != 0
		&& Surface->ApplyLocalScrapePrediction(
			this,
			ActiveLocalScrapePredictionSessionId,
			ActiveLocalScrapePredictionAimSequence,
			GetScrapeMonotonicSeconds(),
			Stroke,
			ScrapeToolSpec,
			true))
	{
		LocalPredictedScrapeSurfaces.AddUnique(Surface);
	}
}
void UMD_ScrapeToolComponent::StartStage3PathAccumulator(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& StartPositionCm,
	const FVector2D& StartBladeDepthAxis)
{
	if (!IsValid(Surface)
		|| StartPositionCm.ContainsNaN()
		|| StartBladeDepthAxis.ContainsNaN())
	{
		return;
	}
	if (ContaminationPaint.bStage3PathActive)
	{
		FlushStage3PathTail();
	}
	if (LmbStrokeGeneration == 0)
	{
		LmbStrokeGeneration = 1;
	}
	if (ContaminationPaint.Stage3SubStrokeGeneration == 0)
	{
		ContaminationPaint.Stage3SubStrokeGeneration = 1;
	}
	if (ScrapeToolSpecGeneration == 0)
	{
		ScrapeToolSpecGeneration = 1;
	}

	ContaminationPaint.bStage3PathActive = true;
	ContaminationPaint.Stage3PathArcLengthCm = 0.0;
	ContaminationPaint.Stage3NextDistanceBin = 0;
	ContaminationPaint.Stage3PathSurface = Surface;
	ContaminationPaint.Stage3PathSurfaceFace = SurfaceFace;
	ContaminationPaint.Stage3PathToolSpec = ScrapeToolSpec;
	ContaminationPaint.Stage3PathStartPositionCm = StartPositionCm;
	ContaminationPaint.Stage3PathStartBladeBasis = FMD_ScrapeBladeAxis::MakeBasis(StartBladeDepthAxis);
	ContaminationPaint.Stage3ProtectionDistanceCm =
		Surface->GetContaminationPaintProtectionDistanceCm(
			ContaminationPaint.Stage3PathToolSpec,
			GetSaturatedContaminationStampDepthCm());
	ContaminationPaint.PendingStage3Dabs.Reset();
}

void UMD_ScrapeToolComponent::AccumulateStage3PathSegment(
	UMD_ScrapeSurfaceComponent* const Surface,
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FMD_ScrapeToolPathSegment& PathSegment)
{
	if (!IsValid(Surface) || PathSegment.bStartsNewSubStroke)
	{
		return;
	}
	if (ContaminationPaint.bStage3PathActive
		&& (ContaminationPaint.Stage3PathSurface.Get() != Surface
			|| ContaminationPaint.Stage3PathSurfaceFace != SurfaceFace))
	{
		FlushStage3PathTail();
		AdvanceStage3SubStrokeGeneration();
	}
	if (!ContaminationPaint.bStage3PathActive)
	{
		StartStage3PathAccumulator(
			Surface,
			SurfaceFace,
			PathSegment.StartPositionCm,
			PathSegment.StartBladeDepthAxis);
	}
	if (!ContaminationPaint.bStage3PathActive)
	{
		return;
	}

	const double SegmentTravelCm =
		static_cast<double>(PathSegment.TravelDistanceCm);
	if (!FMath::IsFinite(SegmentTravelCm)
		|| SegmentTravelCm <= UE_SMALL_NUMBER)
	{
		return;
	}

	const double SegmentStartArcCm = ContaminationPaint.Stage3PathArcLengthCm;
	const double SegmentEndArcCm = SegmentStartArcCm + SegmentTravelCm;
	++ContaminationPaint.Stage3SampleSequence;
	if (ContaminationPaint.Stage3SampleSequence == 0)
	{
		++ContaminationPaint.Stage3SampleSequence;
	}
	const int32 ResolvedPatternSeed =
		Surface->ResolveContaminationPaintPatternSeed(
			SurfaceFace,
			ContaminationPaint.SaturatedContaminationStampPatternSeed);
	const float StampDepthCm = GetSaturatedContaminationStampDepthCm();
	const float StampSpacingCm = GetSaturatedContaminationStampSpacingCm();

	while (ContaminationPaint.Stage3NextDistanceBin < MAX_int32)
	{
		FMD_ScrapeContaminationStampIdentity Identity;
		Identity.FocusSessionGeneration = GetStage3FocusSessionIdentity();
		Identity.LmbStrokeGeneration = LmbStrokeGeneration;
		Identity.SubStrokeGeneration = ContaminationPaint.Stage3SubStrokeGeneration;
		Identity.DistanceBin = ContaminationPaint.Stage3NextDistanceBin;
		const double CandidateArcCm =
			FMD_ScrapeEvaluator::CalculateContaminationDabArcLengthCm(
				Identity,
				StampDepthCm,
				StampSpacingCm,
				ResolvedPatternSeed);
		if (!FMath::IsFinite(CandidateArcCm)
			|| CandidateArcCm > SegmentEndArcCm + UE_KINDA_SMALL_NUMBER)
		{
			break;
		}

		const int32 CandidateDistanceBin = ContaminationPaint.Stage3NextDistanceBin;
		++ContaminationPaint.Stage3NextDistanceBin;
		// 시작점 제외/끝점 포함으로 frame 경계의 중복을 막는다.
		if (CandidateArcCm
			<= SegmentStartArcCm + UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		Identity.DistanceBin = CandidateDistanceBin;
		if (!FMD_ScrapeEvaluator::ShouldSelectContaminationDab(
				Identity,
				GetSaturatedContaminationStampCoverage(),
				ResolvedPatternSeed))
		{
			continue;
		}

		FMD_PendingScrapeContaminationDab& Pending =
			ContaminationPaint.PendingStage3Dabs.AddDefaulted_GetRef();
		Pending.Dab = BuildStage3DabAtArcLength(
			PathSegment,
			SegmentStartArcCm,
			SegmentTravelCm,
			CandidateArcCm,
			CandidateDistanceBin,
			SurfaceFace);
		Pending.Surface = Surface;
		Pending.ToolSpec = ContaminationPaint.Stage3PathToolSpec;
		Pending.StampAmount = GetSaturatedContaminationStampRatio();
		Pending.StampDepthCm = StampDepthCm;
	}

	ContaminationPaint.Stage3PathArcLengthCm = SegmentEndArcCm;
	CommitProtectedStage3Dabs();
}

FMD_ScrapeContaminationDab
UMD_ScrapeToolComponent::BuildStage3DabAtArcLength(
	const FMD_ScrapeToolPathSegment& PathSegment,
	const double SegmentStartArcCm,
	const double SegmentTravelCm,
	const double CandidateArcCm,
	const int32 DistanceBin,
	const EMD_ScrapeSurfaceFace SurfaceFace)
{
	const double Alpha = SegmentTravelCm > UE_SMALL_NUMBER
		? FMath::Clamp(
			(CandidateArcCm - SegmentStartArcCm) / SegmentTravelCm,
			0.0,
			1.0)
		: 0.0;
	const FVector2D BladeDepthAxis = FMD_ScrapeBladeAxis::Interpolate(
		PathSegment.StartBladeDepthAxis,
		PathSegment.EndBladeDepthAxis,
		Alpha);

	FMD_ScrapeContaminationDab Dab;
	Dab.Identity.FocusSessionGeneration = GetStage3FocusSessionIdentity();
	Dab.Identity.LmbStrokeGeneration = LmbStrokeGeneration;
	Dab.Identity.SubStrokeGeneration = ContaminationPaint.Stage3SubStrokeGeneration;
	Dab.Identity.DistanceBin = DistanceBin;
	Dab.PositionCm = FMath::Lerp(
		PathSegment.StartPositionCm,
		PathSegment.EndPositionCm,
		Alpha);
	Dab.BladeBasis = FMD_ScrapeBladeAxis::MakeBasis(BladeDepthAxis);
	Dab.SurfaceFace = SurfaceFace;
	Dab.ArcLengthCm = CandidateArcCm;
	Dab.SampleSequence = ContaminationPaint.Stage3SampleSequence;
	Dab.ToolSpecGeneration = ScrapeToolSpecGeneration;
	return Dab;
}

void UMD_ScrapeToolComponent::CommitProtectedStage3Dabs()
{
	const double CommitThroughArcCm =
		ContaminationPaint.Stage3PathArcLengthCm - FMath::Max(ContaminationPaint.Stage3ProtectionDistanceCm, 0.0);
	int32 CommitCount = 0;
	while (CommitCount < ContaminationPaint.PendingStage3Dabs.Num()
		&& ContaminationPaint.PendingStage3Dabs[CommitCount].Dab.ArcLengthCm
			<= CommitThroughArcCm + UE_KINDA_SMALL_NUMBER)
	{
		FMD_PendingScrapeContaminationDab& Pending =
			ContaminationPaint.PendingStage3Dabs[CommitCount];
		if (UMD_ScrapeSurfaceComponent* const Surface = Pending.Surface.Get())
		{
			Surface->ApplyLocalContaminationDab(
				Pending.Dab,
				Pending.ToolSpec,
				Pending.StampAmount,
				Pending.StampDepthCm);
		}
		ContaminationPaint.CommittedStage3Dabs.Add(Pending.Dab);
		++CommitCount;
	}
	if (CommitCount > 0)
	{
		ContaminationPaint.PendingStage3Dabs.RemoveAt(
			0,
			CommitCount,
			EAllowShrinking::No);
	}
}

void UMD_ScrapeToolComponent::FlushStage3PathTail()
{
	if (!ContaminationPaint.bStage3PathActive)
	{
		return;
	}
	UMD_ScrapeSurfaceComponent* const FinalizedSurface =
		ContaminationPaint.Stage3PathSurface.Get();
	for (FMD_PendingScrapeContaminationDab& Pending : ContaminationPaint.PendingStage3Dabs)
	{
		if (UMD_ScrapeSurfaceComponent* const Surface = Pending.Surface.Get())
		{
			Surface->ApplyLocalContaminationDab(
				Pending.Dab,
				Pending.ToolSpec,
				Pending.StampAmount,
				Pending.StampDepthCm);
		}
		ContaminationPaint.CommittedStage3Dabs.Add(Pending.Dab);
	}
	if (IsValid(FinalizedSurface))
	{
		FinalizedSurface->FinalizeLocalContaminationPath();
	}
	ResetStage3PathAccumulator();
}

void UMD_ScrapeToolComponent::ResetStage3PathAccumulator()
{
	ContaminationPaint.bStage3PathActive = false;
	ContaminationPaint.Stage3PathArcLengthCm = 0.0;
	ContaminationPaint.Stage3NextDistanceBin = 0;
	ContaminationPaint.Stage3ProtectionDistanceCm = 0.0;
	ContaminationPaint.Stage3PathSurface.Reset();
	ContaminationPaint.Stage3PathSurfaceFace = EMD_ScrapeSurfaceFace::Front;
	ContaminationPaint.Stage3PathToolSpec = FMD_ScrapeToolSpec();
	ContaminationPaint.Stage3PathStartPositionCm = FVector2D::ZeroVector;
	ContaminationPaint.Stage3PathStartBladeBasis = FMD_ScrapeBladeBasis();
	ContaminationPaint.PendingStage3Dabs.Reset();
}

void UMD_ScrapeToolComponent::AdvanceStage3SubStrokeGeneration()
{
	++ContaminationPaint.Stage3SubStrokeGeneration;
	if (ContaminationPaint.Stage3SubStrokeGeneration == 0)
	{
		++ContaminationPaint.Stage3SubStrokeGeneration;
	}
}

void UMD_ScrapeToolComponent::BreakStage3SubStroke()
{
	if (!ContaminationPaint.bStage3PathActive)
	{
		return;
	}
	FlushStage3PathTail();
	AdvanceStage3SubStrokeGeneration();
}

uint64 UMD_ScrapeToolComponent::GetStage3FocusSessionIdentity() const
{
	if (ContaminationPaint.Stage3FocusSessionIdentityOverride != 0)
	{
		return ContaminationPaint.Stage3FocusSessionIdentityOverride;
	}

	// TASK 7의 서버 권위 session id가 붙기 전에도 다른 플레이어의 로컬
	// generation이 같은 Surface dedupe key로 충돌하지 않도록 source를 상위 32bit에 싣는다.
	const UObject* const IdentitySource = GetOwner()
		? static_cast<const UObject*>(GetOwner())
		: static_cast<const UObject*>(this);
	const uint32 SourceIdentity = IdentitySource
		? static_cast<uint32>(IdentitySource->GetUniqueID())
		: 1u;
	const uint32 LocalGeneration = ScrapeFocusSession.ScrapeFocusGeneration != 0
		? ScrapeFocusSession.ScrapeFocusGeneration
		: 1u;
	return (static_cast<uint64>(SourceIdentity) << 32)
		| static_cast<uint64>(LocalGeneration);
}

void UMD_ScrapeToolComponent::FlushPendingContaminationPaintTail()
{
	FlushStage3PathTail();
}
