#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "World/Contamination/MD_Barnacle.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 긁기 스트로크 적용 · 제거율 조회 · 따개비 경로 차단

using namespace MD_ScrapeSurfaceComponentInternal;

namespace
{
	constexpr double MaximumObstacleBladeRotationRadiansPerStep =
		5.0 * UE_PI / 180.0;
	constexpr double MaximumObstacleBladeTravelCmPerStep = 0.5;
	constexpr double RecoveryCarrierComparisonTolerance = 1.0e-9;

	bool TryAppendRecoveryLots(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		const TConstArrayView<FMD_ScrapeRecoveryLot> LotsToAppend)
	{
		TArray<FMD_ScrapeRecoveryLot> WorkingLots = InOutLots;
		for (const FMD_ScrapeRecoveryLot& Lot : LotsToAppend)
		{
			if (!FMD_ScrapeRecoveryLedger::AppendLot(WorkingLots, Lot))
			{
				return false;
			}
		}
		InOutLots = MoveTemp(WorkingLots);
		return true;
	}

	bool TryExtractRecoveryLots(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		const double RequestedCarrierAmount,
		TArray<FMD_ScrapeRecoveryLot>& OutExtractedLots)
	{
		OutExtractedLots.Reset();
		if (!FMath::IsFinite(RequestedCarrierAmount)
			|| RequestedCarrierAmount < 0.0)
		{
			return false;
		}
		if (RequestedCarrierAmount == 0.0)
		{
			return true;
		}

		const double AvailableCarrierAmount =
			FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(InOutLots);
		if (AvailableCarrierAmount < 0.0)
		{
			return false;
		}
		if (AvailableCarrierAmount < RequestedCarrierAmount
			&& !FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				InOutLots,
				RequestedCarrierAmount - AvailableCarrierAmount))
		{
			// 오래된/외부 상태에 provenance가 없으면 부족분만 reward 0으로
			// 보충한다. 기존 canonical unit을 새로 발행하지 않는다.
			return false;
		}

		const double ExtractedCarrierAmount =
			FMD_ScrapeRecoveryLedger::ExtractProportional(
				InOutLots,
				RequestedCarrierAmount,
				OutExtractedLots);
		return FMath::Abs(
			ExtractedCarrierAmount - RequestedCarrierAmount)
			<= RecoveryCarrierComparisonTolerance
				* FMath::Max(1.0, RequestedCarrierAmount);
	}

	bool TryReconcileRecoveryLayer(
		TArray<FMD_ScrapeRecoveryLot>& InOutRemainingLots,
		const double ExpectedRemainingCarrierAmount,
		TArray<FMD_ScrapeRecoveryLot>& InOutRemovedLots)
	{
		if (!FMath::IsFinite(ExpectedRemainingCarrierAmount)
			|| ExpectedRemainingCarrierAmount < 0.0)
		{
			return false;
		}
		const double CurrentRemainingCarrierAmount =
			FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
				InOutRemainingLots);
		if (CurrentRemainingCarrierAmount < 0.0)
		{
			return false;
		}

		if (CurrentRemainingCarrierAmount
			> ExpectedRemainingCarrierAmount)
		{
			TArray<FMD_ScrapeRecoveryLot> NumericallyRemovedLots;
			if (!TryExtractRecoveryLots(
					InOutRemainingLots,
					CurrentRemainingCarrierAmount
						- ExpectedRemainingCarrierAmount,
					NumericallyRemovedLots)
				|| !TryAppendRecoveryLots(
					InOutRemovedLots,
					NumericallyRemovedLots))
			{
				return false;
			}
		}
		else if (CurrentRemainingCarrierAmount
			< ExpectedRemainingCarrierAmount)
		{
			// legacy cell raw에 lot이 없던 부족분은 canonical 발행 없이 보충한다.
			if (!FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
					InOutRemainingLots,
					ExpectedRemainingCarrierAmount
						- CurrentRemainingCarrierAmount))
			{
				return false;
			}
		}

		return FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			InOutRemainingLots,
			ExpectedRemainingCarrierAmount);
	}

	struct FResolvedObstacleBladeBasis
	{
		FVector2D DepthAxis = FVector2D(1.0, 0.0);
		FVector2D WidthAxis = FVector2D(0.0, 1.0);
	};

	uint32 BuildStableContaminationPaintSurfaceSalt(
		const UMD_ScrapeSurfaceComponent* const Surface,
		const EMD_ScrapeSurfaceFace Face)
	{
		const UObject* const StableOwner = Surface && Surface->GetOwner()
			? static_cast<const UObject*>(Surface->GetOwner())
			: static_cast<const UObject*>(Surface);
		return HashCombineFast(
			StableOwner ? GetTypeHash(StableOwner->GetFName()) : 0u,
			GetTypeHash(static_cast<uint8>(Face)));
	}

	bool TryNormalizeObstacleBladeBasis(
		const FMD_ScrapeBladeBasis& Basis,
		FResolvedObstacleBladeBasis& OutBasis)
	{
		if (Basis.IsUnset()
			|| Basis.DepthAxis.ContainsNaN()
			|| Basis.WidthAxis.ContainsNaN())
		{
			return false;
		}

		const FVector2D DepthAxis = Basis.DepthAxis.GetSafeNormal();
		FVector2D WidthAxis = Basis.WidthAxis
			- DepthAxis * FVector2D::DotProduct(Basis.WidthAxis, DepthAxis);
		WidthAxis.Normalize();
		if (DepthAxis.IsNearlyZero() || WidthAxis.IsNearlyZero())
		{
			return false;
		}

		const double Handedness = DepthAxis.X * WidthAxis.Y
			- DepthAxis.Y * WidthAxis.X;
		if (FMath::Abs(Handedness) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		OutBasis.DepthAxis = DepthAxis;
		OutBasis.WidthAxis = FVector2D(-DepthAxis.Y, DepthAxis.X)
			* (Handedness < 0.0 ? -1.0 : 1.0);
		return true;
	}

	bool TryResolveObstacleBladeBases(
		const FMD_ScrapeStroke& Stroke,
		FResolvedObstacleBladeBasis& OutStartBasis,
		FResolvedObstacleBladeBasis& OutEndBasis,
		double& OutSignedRotationRadians)
	{
		FResolvedObstacleBladeBasis StartBasis;
		FResolvedObstacleBladeBasis EndBasis;
		const bool bHasStartBasis = TryNormalizeObstacleBladeBasis(
			Stroke.StartBladeBasis,
			StartBasis);
		const bool bHasEndBasis = TryNormalizeObstacleBladeBasis(
			Stroke.EndBladeBasis,
			EndBasis);
		if (!bHasStartBasis && !bHasEndBasis)
		{
			return false;
		}

		if (!bHasStartBasis)
		{
			StartBasis = EndBasis;
		}
		if (!bHasEndBasis)
		{
			EndBasis = StartBasis;
		}

		// Rectangle 방향은 180도 뒤집혀도 같은 footprint다. 가장 짧은
		// 등가 회전으로 맞춰 불필요한 반 바퀴 sweep을 만들지 않는다.
		if (FVector2D::DotProduct(StartBasis.DepthAxis, EndBasis.DepthAxis) < 0.0)
		{
			EndBasis.DepthAxis *= -1.0;
			EndBasis.WidthAxis *= -1.0;
		}

		const double StartHandedness = StartBasis.DepthAxis.X * StartBasis.WidthAxis.Y
			- StartBasis.DepthAxis.Y * StartBasis.WidthAxis.X;
		const double EndHandedness = EndBasis.DepthAxis.X * EndBasis.WidthAxis.Y
			- EndBasis.DepthAxis.Y * EndBasis.WidthAxis.X;
		if (StartHandedness * EndHandedness <= 0.0)
		{
			return false;
		}

		const double Dot = FMath::Clamp(
			FVector2D::DotProduct(StartBasis.DepthAxis, EndBasis.DepthAxis),
			-1.0,
			1.0);
		const double Cross = StartBasis.DepthAxis.X * EndBasis.DepthAxis.Y
			- StartBasis.DepthAxis.Y * EndBasis.DepthAxis.X;
		OutSignedRotationRadians = FMath::Atan2(Cross, Dot);
		OutStartBasis = StartBasis;
		OutEndBasis = EndBasis;
		return true;
	}

	FResolvedObstacleBladeBasis InterpolateObstacleBladeBasis(
		const FResolvedObstacleBladeBasis& StartBasis,
		const double SignedRotationRadians,
		const double Alpha)
	{
		const double AppliedRadians = SignedRotationRadians
			* FMath::Clamp(Alpha, 0.0, 1.0);
		const double CosAngle = FMath::Cos(AppliedRadians);
		const double SinAngle = FMath::Sin(AppliedRadians);
		FResolvedObstacleBladeBasis Result;
		Result.DepthAxis = FVector2D(
			StartBasis.DepthAxis.X * CosAngle
				- StartBasis.DepthAxis.Y * SinAngle,
			StartBasis.DepthAxis.X * SinAngle
				+ StartBasis.DepthAxis.Y * CosAngle);
		Result.WidthAxis = FVector2D(
			StartBasis.WidthAxis.X * CosAngle
				- StartBasis.WidthAxis.Y * SinAngle,
			StartBasis.WidthAxis.X * SinAngle
				+ StartBasis.WidthAxis.Y * CosAngle);
		return Result;
	}

	double CalculateObstacleBladeClearanceCm(
		const FVector2D& BladeCenterCm,
		const FResolvedObstacleBladeBasis& Basis,
		const double BladeHalfDepthCm,
		const double BladeHalfWidthCm,
		const FVector2D& ObstacleCenterCm,
		const double ObstacleRadiusCm)
	{
		const FVector2D CenterToObstacle = ObstacleCenterCm - BladeCenterCm;
		const double LocalDepth = FMath::Abs(FVector2D::DotProduct(
			CenterToObstacle,
			Basis.DepthAxis)) - BladeHalfDepthCm;
		const double LocalWidth = FMath::Abs(FVector2D::DotProduct(
			CenterToObstacle,
			Basis.WidthAxis)) - BladeHalfWidthCm;
		const double OutsideDepth = FMath::Max(LocalDepth, 0.0);
		const double OutsideWidth = FMath::Max(LocalWidth, 0.0);
		const double OutsideDistance = FMath::Sqrt(
			OutsideDepth * OutsideDepth + OutsideWidth * OutsideWidth);
		const double InsideDistance = FMath::Min(
			FMath::Max(LocalDepth, LocalWidth),
			0.0);
		return OutsideDistance + InsideDistance - ObstacleRadiusCm;
	}

	bool FindSegmentAabbEntryAlpha(
		const FVector2D& Start,
		const FVector2D& End,
		const FVector2D& Minimum,
		const FVector2D& Maximum,
		double& OutEntryAlpha)
	{
		double EntryAlpha = 0.0;
		double ExitAlpha = 1.0;
		const FVector2D Delta = End - Start;
		for (int32 AxisIndex = 0; AxisIndex < 2; ++AxisIndex)
		{
			const double AxisStart = AxisIndex == 0 ? Start.X : Start.Y;
			const double AxisDelta = AxisIndex == 0 ? Delta.X : Delta.Y;
			const double AxisMinimum = AxisIndex == 0 ? Minimum.X : Minimum.Y;
			const double AxisMaximum = AxisIndex == 0 ? Maximum.X : Maximum.Y;
			if (FMath::Abs(AxisDelta) <= UE_DOUBLE_SMALL_NUMBER)
			{
				if (AxisStart < AxisMinimum || AxisStart > AxisMaximum)
				{
					return false;
				}
				continue;
			}

			double AxisEntry = (AxisMinimum - AxisStart) / AxisDelta;
			double AxisExit = (AxisMaximum - AxisStart) / AxisDelta;
			if (AxisEntry > AxisExit)
			{
				Swap(AxisEntry, AxisExit);
			}
			EntryAlpha = FMath::Max(EntryAlpha, AxisEntry);
			ExitAlpha = FMath::Min(ExitAlpha, AxisExit);
			if (EntryAlpha > ExitAlpha)
			{
				return false;
			}
		}

		if (ExitAlpha < 0.0 || EntryAlpha > 1.0)
		{
			return false;
		}
		OutEntryAlpha = FMath::Clamp(EntryAlpha, 0.0, 1.0);
		return true;
	}

	bool FindSegmentCircleEntryAlpha(
		const FVector2D& Start,
		const FVector2D& End,
		const FVector2D& CircleCenter,
		const double RadiusCm,
		double& OutEntryAlpha)
	{
		const FVector2D StartToCenter = Start - CircleCenter;
		const FVector2D Delta = End - Start;
		const double RadiusSquared = RadiusCm * RadiusCm;
		if (StartToCenter.SizeSquared() <= RadiusSquared)
		{
			OutEntryAlpha = 0.0;
			return true;
		}

		const double A = Delta.SizeSquared();
		if (A <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		const double B = FVector2D::DotProduct(StartToCenter, Delta);
		const double C = StartToCenter.SizeSquared() - RadiusSquared;
		const double Discriminant = B * B - A * C;
		if (Discriminant < 0.0)
		{
			return false;
		}

		const double EntryAlpha = (-B - FMath::Sqrt(Discriminant)) / A;
		if (EntryAlpha < 0.0 || EntryAlpha > 1.0)
		{
			return false;
		}
		OutEntryAlpha = EntryAlpha;
		return true;
	}

	bool FindSegmentRoundedRectangleEntryAlpha(
		const FVector2D& WorldStart,
		const FVector2D& WorldEnd,
		const FVector2D& ObstacleCenterCm,
		const FResolvedObstacleBladeBasis& Basis,
		const double BladeHalfDepthCm,
		const double BladeHalfWidthCm,
		const double ExpandedObstacleRadiusCm,
		double& OutEntryAlpha)
	{
		const auto ToBladeLocal = [&Basis, &ObstacleCenterCm](const FVector2D& Point)
		{
			const FVector2D ObstacleToPoint = ObstacleCenterCm - Point;
			return FVector2D(
				FVector2D::DotProduct(ObstacleToPoint, Basis.DepthAxis),
				FVector2D::DotProduct(ObstacleToPoint, Basis.WidthAxis));
		};
		const FVector2D Start = ToBladeLocal(WorldStart);
		const FVector2D End = ToBladeLocal(WorldEnd);
		double EarliestAlpha = TNumericLimits<double>::Max();
		double CandidateAlpha = 0.0;
		if (FindSegmentAabbEntryAlpha(
				Start,
				End,
				FVector2D(-BladeHalfDepthCm, -BladeHalfWidthCm - ExpandedObstacleRadiusCm),
				FVector2D(BladeHalfDepthCm, BladeHalfWidthCm + ExpandedObstacleRadiusCm),
				CandidateAlpha))
		{
			EarliestAlpha = FMath::Min(EarliestAlpha, CandidateAlpha);
		}
		if (FindSegmentAabbEntryAlpha(
				Start,
				End,
				FVector2D(-BladeHalfDepthCm - ExpandedObstacleRadiusCm, -BladeHalfWidthCm),
				FVector2D(BladeHalfDepthCm + ExpandedObstacleRadiusCm, BladeHalfWidthCm),
				CandidateAlpha))
		{
			EarliestAlpha = FMath::Min(EarliestAlpha, CandidateAlpha);
		}

		for (const double DepthSign : {-1.0, 1.0})
		{
			for (const double WidthSign : {-1.0, 1.0})
			{
				if (FindSegmentCircleEntryAlpha(
						Start,
						End,
						FVector2D(
							DepthSign * BladeHalfDepthCm,
							WidthSign * BladeHalfWidthCm),
						ExpandedObstacleRadiusCm,
						CandidateAlpha))
				{
					EarliestAlpha = FMath::Min(EarliestAlpha, CandidateAlpha);
				}
			}
		}

		if (EarliestAlpha == TNumericLimits<double>::Max())
		{
			return false;
		}
		OutEntryAlpha = EarliestAlpha;
		return true;
	}
}

bool UMD_ScrapeSurfaceComponent::RegisterScrapeObstacle(AMD_Barnacle* const Barnacle)
{
	if (!bContaminationEnabled || !IsValid(Barnacle))
	{
		return false;
	}

	const float RadiusCm = Barnacle->GetScrapeBlockRadiusCm();
	if (!FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0f)
	{
		return false;
	}

	// 따개비의 Up이 곧 부착면의 법선이다. 배치기가 그렇게 세워 붙인다.
	FMD_ScrapeSurfaceObstacle Obstacle;
	Obstacle.Barnacle = Barnacle;
	Obstacle.RadiusCm = RadiusCm;
	if (!TryGetScrapeSurfacePositionFromWorldPoint(
			Barnacle->GetActorLocation(),
			Barnacle->GetActorUpVector(),
			Obstacle.CenterCm,
			Obstacle.SurfaceFace))
	{
		return false;
	}

	// 이미 등록된 것을 다시 넣으면 같은 자리를 두 번 막는다.
	for (FMD_ScrapeSurfaceObstacle& Existing : ScrapeObstacles)
	{
		if (Existing.Barnacle.Get() == Barnacle)
		{
			Existing = Obstacle;
			return true;
		}
	}

	ScrapeObstacles.Add(Obstacle);
	return true;
}


void UMD_ScrapeSurfaceComponent::UnregisterScrapeObstacle(AMD_Barnacle* const Barnacle)
{
	// 죽은 참조도 함께 정리한다. 따개비는 Destroy로 사라지므로 등록만 남기 쉽다.
	ScrapeObstacles.RemoveAll(
		[Barnacle](const FMD_ScrapeSurfaceObstacle& Obstacle)
		{
			const AMD_Barnacle* const Registered = Obstacle.Barnacle.Get();
			return Registered == nullptr || Registered == Barnacle;
		});
}


bool UMD_ScrapeSurfaceComponent::TryClampScrapeStrokeAgainstObstacles(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	FVector2D& OutClampedEndPositionCm,
	AMD_Barnacle*& OutBlockingBarnacle) const
{
	OutClampedEndPositionCm = Stroke.EndPositionCm;
	OutBlockingBarnacle = nullptr;

	// 원형 업그레이드는 기존 계약대로 BladeWidth를 지름으로 사용한다.
	if (ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle)
	{
		return TryClampScrapeStrokeAgainstObstacles(
			Stroke.SurfaceFace,
			Stroke.StartPositionCm,
			Stroke.EndPositionCm,
			ToolSpec.BladeWidthCm * 0.5f,
			OutClampedEndPositionCm,
			OutBlockingBarnacle);
	}

	if (!bContaminationEnabled
		|| ScrapeObstacles.IsEmpty()
		|| Stroke.StartPositionCm.ContainsNaN()
		|| Stroke.EndPositionCm.ContainsNaN()
		|| !FMath::IsFinite(ToolSpec.BladeWidthCm)
		|| !FMath::IsFinite(ToolSpec.BladeDepthCm)
		|| ToolSpec.BladeWidthCm <= UE_SMALL_NUMBER
		|| ToolSpec.BladeDepthCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D StrokeDelta =
		Stroke.EndPositionCm - Stroke.StartPositionCm;
	const double StrokeLengthCm = StrokeDelta.Size();
	if (!FMath::IsFinite(StrokeLengthCm)
		|| StrokeLengthCm <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	FResolvedObstacleBladeBasis StartBasis;
	FResolvedObstacleBladeBasis EndBasis;
	double SignedRotationRadians = 0.0;
	if (!TryResolveObstacleBladeBases(
			Stroke,
			StartBasis,
			EndBasis,
			SignedRotationRadians))
	{
		// 명시 basis가 없는 구형 Stroke도 새 오버로드로 들어올 수 있다.
		// 이 경우에는 종전 원형 근사를 유지해 호출 호환성을 보장한다.
		return TryClampScrapeStrokeAgainstObstacles(
			Stroke.SurfaceFace,
			Stroke.StartPositionCm,
			Stroke.EndPositionCm,
			ToolSpec.BladeWidthCm * 0.5f,
			OutClampedEndPositionCm,
			OutBlockingBarnacle);
	}

	const double BladeHalfDepthCm =
		static_cast<double>(ToolSpec.BladeDepthCm) * 0.5;
	const double BladeHalfWidthCm =
		static_cast<double>(ToolSpec.BladeWidthCm) * 0.5;
	const double BladeCornerRadiusCm = FMath::Sqrt(
		BladeHalfDepthCm * BladeHalfDepthCm
			+ BladeHalfWidthCm * BladeHalfWidthCm);
	const int32 TravelStepCount = FMath::Max(
		1,
		FMath::CeilToInt(StrokeLengthCm / MaximumObstacleBladeTravelCmPerStep));
	const int32 RotationStepCount = FMath::Max(
		1,
		FMath::CeilToInt(
			FMath::Abs(SignedRotationRadians)
				/ MaximumObstacleBladeRotationRadiansPerStep));
	const int32 StepCount = FMath::Max(TravelStepCount, RotationStepCount);

	double EarliestHitAlpha = TNumericLimits<double>::Max();
	AMD_Barnacle* EarliestBarnacle = nullptr;
	for (const FMD_ScrapeSurfaceObstacle& Obstacle : ScrapeObstacles)
	{
		AMD_Barnacle* const Barnacle = Obstacle.Barnacle.Get();
		if (!IsValid(Barnacle)
			|| Obstacle.SurfaceFace != Stroke.SurfaceFace
			|| !FMath::IsFinite(Obstacle.RadiusCm)
			|| Obstacle.RadiusCm <= 0.0f)
		{
			continue;
		}

		// 기존 판정과 같이 이미 겹친 상태에서 빠져나가는 동작은 허용한다.
		// 회전까지 포함한 signed clearance의 초기 변화로 진입/이탈을 구분한다.
		const double StartClearanceCm = CalculateObstacleBladeClearanceCm(
			Stroke.StartPositionCm,
			StartBasis,
			BladeHalfDepthCm,
			BladeHalfWidthCm,
			Obstacle.CenterCm,
			Obstacle.RadiusCm);
		bool bEscapingInitialOverlap = StartClearanceCm <= 0.0;
		if (bEscapingInitialOverlap)
		{
			const double ProbeAlpha = FMath::Min(
				1.0,
				FMath::Max(1.0e-4, 0.01 / static_cast<double>(StepCount)));
			const FResolvedObstacleBladeBasis ProbeBasis =
				InterpolateObstacleBladeBasis(
					StartBasis,
					SignedRotationRadians,
					ProbeAlpha);
			const double ProbeClearanceCm = CalculateObstacleBladeClearanceCm(
				Stroke.StartPositionCm + StrokeDelta * ProbeAlpha,
				ProbeBasis,
				BladeHalfDepthCm,
				BladeHalfWidthCm,
				Obstacle.CenterCm,
				Obstacle.RadiusCm);
			if (ProbeClearanceCm < StartClearanceCm - UE_DOUBLE_SMALL_NUMBER)
			{
				if (0.0 < EarliestHitAlpha)
				{
					EarliestHitAlpha = 0.0;
					EarliestBarnacle = Barnacle;
				}
				continue;
			}
		}

		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const double StartAlpha = static_cast<double>(StepIndex)
				/ static_cast<double>(StepCount);
			const double EndAlpha = static_cast<double>(StepIndex + 1)
				/ static_cast<double>(StepCount);
			const FVector2D StepStartPosition =
				Stroke.StartPositionCm + StrokeDelta * StartAlpha;
			const FVector2D StepEndPosition =
				Stroke.StartPositionCm + StrokeDelta * EndAlpha;
			const FResolvedObstacleBladeBasis StepEndBasis =
				InterpolateObstacleBladeBasis(
					StartBasis,
					SignedRotationRadians,
					EndAlpha);

			if (bEscapingInitialOverlap)
			{
				const double EndClearanceCm = CalculateObstacleBladeClearanceCm(
					StepEndPosition,
					StepEndBasis,
					BladeHalfDepthCm,
					BladeHalfWidthCm,
					Obstacle.CenterCm,
					Obstacle.RadiusCm);
				if (EndClearanceCm <= 0.0)
				{
					continue;
				}
				bEscapingInitialOverlap = false;
				// 이 작은 구간은 최초 겹침에서 빠져나가는 부분이므로 다음
				// 구간부터 재진입을 검사한다.
				continue;
			}

			const double MidAlpha = (StartAlpha + EndAlpha) * 0.5;
			const FResolvedObstacleBladeBasis MidBasis =
				InterpolateObstacleBladeBasis(
					StartBasis,
					SignedRotationRadians,
					MidAlpha);
			const double StepRotationRadians =
				FMath::Abs(SignedRotationRadians) / StepCount;
			// 중간 자세에서 각 끝 자세까지 회전한 모든 꼭짓점을 포함하는
			// Hausdorff 여유. 각도당 최대 5도라 회전 중간을 놓치지 않는다.
			const double RotationPaddingCm = 2.0 * BladeCornerRadiusCm
				* FMath::Sin(StepRotationRadians * 0.25);
			double StepHitAlpha = 0.0;
			if (!FindSegmentRoundedRectangleEntryAlpha(
					StepStartPosition,
					StepEndPosition,
					Obstacle.CenterCm,
					MidBasis,
					BladeHalfDepthCm,
					BladeHalfWidthCm,
					static_cast<double>(Obstacle.RadiusCm) + RotationPaddingCm,
					StepHitAlpha))
			{
				continue;
			}

			const double HitAlpha = FMath::Lerp(
				StartAlpha,
				EndAlpha,
				StepHitAlpha);
			if (HitAlpha < EarliestHitAlpha)
			{
				EarliestHitAlpha = HitAlpha;
				EarliestBarnacle = Barnacle;
			}
			break;
		}
	}

	if (!EarliestBarnacle)
	{
		return false;
	}

	const double MarginAlpha = ScrapeObstacleContactMarginCm / StrokeLengthCm;
	const double ClampedAlpha = FMath::Clamp(
		EarliestHitAlpha - MarginAlpha,
		0.0,
		1.0);
	OutClampedEndPositionCm =
		Stroke.StartPositionCm + StrokeDelta * ClampedAlpha;
	OutBlockingBarnacle = EarliestBarnacle;
	return true;
}


bool UMD_ScrapeSurfaceComponent::TryClampScrapeStrokeAgainstObstacles(
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& StartPositionCm,
	const FVector2D& EndPositionCm,
	const float ToolHalfWidthCm,
	FVector2D& OutClampedEndPositionCm,
	AMD_Barnacle*& OutBlockingBarnacle) const
{
	OutClampedEndPositionCm = EndPositionCm;
	OutBlockingBarnacle = nullptr;

	if (!bContaminationEnabled
		|| ScrapeObstacles.IsEmpty()
		|| StartPositionCm.ContainsNaN()
		|| EndPositionCm.ContainsNaN()
		|| !FMath::IsFinite(ToolHalfWidthCm))
	{
		return false;
	}

	const FVector2D StrokeDelta = EndPositionCm - StartPositionCm;
	const double StrokeLengthSquared = StrokeDelta.SizeSquared();
	if (StrokeLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeHalfWidthCm = FMath::Max(0.0f, ToolHalfWidthCm);

	// 여러 부착물이 경로에 걸리면 가장 먼저 만나는 것이 막는다.
	double EarliestHitAlpha = TNumericLimits<double>::Max();
	AMD_Barnacle* EarliestBarnacle = nullptr;

	for (const FMD_ScrapeSurfaceObstacle& Obstacle : ScrapeObstacles)
	{
		AMD_Barnacle* const Barnacle = Obstacle.Barnacle.Get();
		if (!IsValid(Barnacle) || Obstacle.SurfaceFace != SurfaceFace)
		{
			continue;
		}

		const double BlockRadius =
			static_cast<double>(Obstacle.RadiusCm) + SafeHalfWidthCm;
		if (BlockRadius <= 0.0)
		{
			continue;
		}

		const FVector2D StartToCenter = StartPositionCm - Obstacle.CenterCm;
		const double StartDistanceSquared = StartToCenter.SizeSquared();
		const double ApproachDot = FVector2D::DotProduct(StartToCenter, StrokeDelta);

		if (StartDistanceSquared <= BlockRadius * BlockRadius)
		{
			// 시작점이 이미 안쪽이다. 멀어지는 방향은 그대로 두고, 더 파고드는
			// 방향만 그 자리에 묶는다. 둘 다 막으면 스트로크가 영영 풀리지 않는다.
			if (ApproachDot < 0.0 && 0.0 < EarliestHitAlpha)
			{
				EarliestHitAlpha = 0.0;
				EarliestBarnacle = Barnacle;
			}
			continue;
		}

		// 선분 대 원. 바깥에서 출발했으므로 먼저 닿는 해만 의미가 있다.
		const double QuadraticB = 2.0 * ApproachDot;
		const double QuadraticC =
			StartDistanceSquared - BlockRadius * BlockRadius;
		const double Discriminant =
			QuadraticB * QuadraticB - 4.0 * StrokeLengthSquared * QuadraticC;
		if (Discriminant < 0.0)
		{
			continue;
		}

		const double HitAlpha =
			(-QuadraticB - FMath::Sqrt(Discriminant)) / (2.0 * StrokeLengthSquared);
		if (HitAlpha < 0.0 || HitAlpha > 1.0 || HitAlpha >= EarliestHitAlpha)
		{
			continue;
		}

		EarliestHitAlpha = HitAlpha;
		EarliestBarnacle = Barnacle;
	}

	if (!EarliestBarnacle)
	{
		return false;
	}

	// 경계에 정확히 붙이지 않고 조금 앞에서 세운다.
	const double StrokeLength = FMath::Sqrt(StrokeLengthSquared);
	const double MarginAlpha = ScrapeObstacleContactMarginCm / StrokeLength;
	const double ClampedAlpha =
		FMath::Clamp(EarliestHitAlpha - MarginAlpha, 0.0, 1.0);

	OutClampedEndPositionCm = StartPositionCm + StrokeDelta * ClampedAlpha;
	OutBlockingBarnacle = EarliestBarnacle;
	return true;
}


FMD_ScrapeResult UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithToolSpec(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec)
{
	float IgnoredRemovedCleanRatioDelta = 0.0f;
	FMD_ScrapeResult Result = ApplyLocalScrapeStrokeInternal(
		Stroke,
		ToolSpec,
		Stroke.StartPositionCm,
		(Stroke.EndPositionCm - Stroke.StartPositionCm).GetSafeNormal(),
		0.0f,
		0.0f,
		1.0f,
		1.0f,
		0,
		IgnoredRemovedCleanRatioDelta);
	// 기존 Blueprint 직접 호출은 Tool의 Load 소유자를 알 수 없다. 제거 질량을
	// 버리지 않도록 예전과 같은 물리 100%로 한 번 귀속한다. 제품 긁기 경로는
	// 아래 deferred API를 사용해 Tool이 Load/물리 비율을 먼저 결정한다.
	FMD_ScrapeMassAllocationResult PhysicalOnlyAllocation;
	PhysicalOnlyAllocation.bIsValid = true;
	PhysicalOnlyAllocation.PhysicalDropRatio = 1.0f;
	CommitContaminationPhysicalDrop(
		Result,
		PhysicalOnlyAllocation,
		Stroke.EndPositionCm);
	return Result;
}


FMD_ScrapeResult
UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithDeferredCompletion(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec)
{
	float IgnoredRemovedCleanRatioDelta = 0.0f;
	return ApplyLocalScrapeStrokeInternal(
		Stroke,
		ToolSpec,
		Stroke.StartPositionCm,
		(Stroke.EndPositionCm - Stroke.StartPositionCm).GetSafeNormal(),
		0.0f,
		0.0f,
		ToolSpec.BladeDepthCm,
		ToolSpec.BladeDepthCm,
		0,
		IgnoredRemovedCleanRatioDelta,
		true);
}


void UMD_ScrapeSurfaceComponent::FinalizeLocalContaminationPath()
{
	if (!bContaminationEnabled
		|| !HasAuthority()
		|| !bContaminationSurfaceInitialized)
	{
		return;
	}

	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (bDeferredContaminationCompletionTransition
		&& bIsFullyRemoved
		&& !bContaminationCompletionBroadcast)
	{
		bContaminationCompletionBroadcast = true;
		OnContaminationFullyRemoved.Broadcast(this);
	}
	else if (!bIsFullyRemoved)
	{
		bContaminationCompletionBroadcast = false;
	}
	bDeferredContaminationCompletionTransition = false;
}


FMD_ScrapeResult
UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithContaminationStamp(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const float StampRatio,
	float& OutRemovedCleanRatioDelta,
	const bool bDeferCompletionBroadcast)
{
	const FVector2D StrokeDelta =
		Stroke.EndPositionCm - Stroke.StartPositionCm;
	const FVector2D StampPathAxis = StrokeDelta.IsNearlyZero()
		? FVector2D(1.0, 0.0)
		: StrokeDelta.GetSafeNormal();
	const float LegacyStampDepthCm = FMath::Max(
		ToolSpec.BladeDepthCm,
		0.001f);
	return ApplyLocalScrapeStrokeInternal(
		Stroke,
		ToolSpec,
		Stroke.StartPositionCm,
		StampPathAxis,
		StampRatio,
		1.0f,
		LegacyStampDepthCm,
		LegacyStampDepthCm,
		0,
		OutRemovedCleanRatioDelta,
		bDeferCompletionBroadcast,
		true);
}


FMD_ScrapeResult
UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithContaminationStamp(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FVector2D& StampPathStartPositionCm,
	const FVector2D& StampPathAxis,
	const float StampRatio,
	const float StampCoverage,
	const float StampDepthCm,
	const float StampSpacingCm,
	const int32 StampPatternSeed,
	float& OutRemovedCleanRatioDelta,
	const bool bDeferCompletionBroadcast)
{
	return ApplyLocalScrapeStrokeInternal(
		Stroke,
		ToolSpec,
		StampPathStartPositionCm,
		StampPathAxis,
		StampRatio,
		StampCoverage,
		StampDepthCm,
		StampSpacingCm,
		StampPatternSeed,
		OutRemovedCleanRatioDelta,
		bDeferCompletionBroadcast);
}


void UMD_ScrapeSurfaceComponent::FlushLocalContaminationPaintTail(
	const FMD_ScrapeStroke& FullPathStroke,
	const FVector2D& StampPathAxis,
	const FMD_ScrapeToolSpec& ToolSpec,
	const float StampRatio,
	const float StampCoverage,
	const float StampDepthCm,
	const float StampSpacingCm,
	const int32 StampPatternSeed)
{
	if (!bContaminationEnabled
		|| !HasAuthority()
		|| !FMath::IsFinite(StampRatio)
		|| StampRatio <= 0.0f
		|| !FMath::IsFinite(StampCoverage)
		|| StampCoverage <= 0.0f)
	{
		return;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized)
	{
		return;
	}

	TArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(FullPathStroke.SurfaceFace);
	FMD_ScrapeStroke PaintStroke;
	if (FaceCells.IsEmpty()
		|| !FMD_ScrapeEvaluator::TryBuildPendingContaminationPaintStroke(
			FullPathStroke,
			StampPathAxis,
			ToolSpec,
			ContaminationGridDimensions,
			ResolvedContaminationSurfaceSizeCm,
			StampDepthCm,
			PaintStroke))
	{
		return;
	}

	const uint32 SurfacePatternHash = HashCombineFast(
		GetTypeHash(StampPatternSeed),
		BuildStableContaminationPaintSurfaceSalt(
			this,
			FullPathStroke.SurfaceFace));
	const int32 SurfacePatternSeed = static_cast<int32>(
		SurfacePatternHash & static_cast<uint32>(MAX_int32));
	TArray<FMD_ScrapeCellChange> StampChanges;
	FMD_ScrapeEvaluator::ApplyContaminationPaintStrokeToGrid(
		PaintStroke,
		ToolSpec,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm,
		StampRatio,
		StampCoverage,
		StampDepthCm,
		StampSpacingCm,
		SurfacePatternSeed,
		FaceCells,
		StampChanges);
	if (StampChanges.IsEmpty())
	{
		return;
	}

	const int32 CellsPerFace = FaceCells.Num();
	for (FMD_ScrapeCellChange& StampChange : StampChanges)
	{
		StampChange.CellIndex = FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			FullPathStroke.SurfaceFace,
			StampChange.CellIndex,
			CellsPerFace);
	}
	RefreshContaminationCleanRatioCache(FullPathStroke.SurfaceFace);
	UpdateContaminationMask(StampChanges);
	PublishContaminationChanges(StampChanges);
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
}


float UMD_ScrapeSurfaceComponent::ApplyLocalContaminationDab(
	const FMD_ScrapeContaminationDab& Dab,
	const FMD_ScrapeToolSpec& ToolSpec,
	const float StampAmount,
	const float StampDepthCm)
{
	if (!bContaminationEnabled
		|| !HasAuthority()
		|| !Dab.Identity.IsValid()
		|| AppliedContaminationStampIdentities.Contains(Dab.Identity)
		|| !ToolSpec.IsValid()
		|| !FMath::IsFinite(StampAmount)
		|| StampAmount <= 0.0f
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}
	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	TArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Dab.SurfaceFace);
	if (FaceCells.IsEmpty())
	{
		return 0.0f;
	}
	const int32 FaceSlot = Dab.SurfaceFace == EMD_ScrapeSurfaceFace::Back
		? 1
		: 0;
	if (FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			TransferredContaminationRecoveryLots[FaceSlot]) < 0.0)
	{
		return 0.0f;
	}

	TArray<FMD_ScrapeCellChange> StampChanges;
	const float AddedAmount = FMD_ScrapeEvaluator::ApplyContaminationDabToGrid(
		Dab,
		ToolSpec,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm,
		StampAmount,
		StampDepthCm,
		FaceCells,
		StampChanges);
	if (AddedAmount > 0.0f)
	{
		// 독립 dab은 제거 provenance를 승계하지 않으므로 reward 0이다.
		const bool bRecorded =
			FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				TransferredContaminationRecoveryLots[FaceSlot],
				static_cast<double>(AddedAmount));
		ensureMsgf(
			bRecorded,
			TEXT("Contamination dab could not be recorded in recovery ledger."));
		double FinalTransferredCarrierAmount = 0.0;
		for (const FMD_ScrapeSurfaceCell& Cell : FaceCells)
		{
			FinalTransferredCarrierAmount +=
				static_cast<double>(Cell.TransferredContamination);
		}
		const bool bReconciled = bRecorded
			&& FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
				TransferredContaminationRecoveryLots[FaceSlot],
				FinalTransferredCarrierAmount);
		ensureMsgf(
			bReconciled,
			TEXT("Contamination dab recovery carrier could not be reconciled."));
	}

	// 표면 밖의 유효한 dab도 승인된 identity다. 재전송이 뒤늦게
	// 들어와 다른 표면 상태에서 적용되는 것을 막기 위해 기록한다.
	AppliedContaminationStampIdentities.Add(Dab.Identity);
	AppliedContaminationStampIdentityOrder.Add(Dab.Identity);
	while (AppliedContaminationStampIdentityOrder.Num()
		> MaximumRecentContaminationStampIdentities)
	{
		const FMD_ScrapeContaminationStampIdentity ExpiredIdentity =
			AppliedContaminationStampIdentityOrder[0];
		AppliedContaminationStampIdentityOrder.RemoveAt(
			0,
			1,
			EAllowShrinking::No);
		AppliedContaminationStampIdentities.Remove(ExpiredIdentity);
	}

	if (StampChanges.IsEmpty())
	{
		return AddedAmount;
	}

	const int32 CellsPerFace = FaceCells.Num();
	for (FMD_ScrapeCellChange& StampChange : StampChanges)
	{
		StampChange.CellIndex = FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			Dab.SurfaceFace,
			StampChange.CellIndex,
			CellsPerFace);
	}
	RefreshContaminationCleanRatioCache(Dab.SurfaceFace);
	UpdateContaminationMask(StampChanges);
	PublishContaminationChanges(StampChanges);
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
	return AddedAmount;
}


double UMD_ScrapeSurfaceComponent::GetContaminationPaintProtectionDistanceCm(
	const FMD_ScrapeToolSpec& ToolSpec,
	const float StampDepthCm) const
{
	return FMD_ScrapeEvaluator::CalculateContaminationPaintProtectionDistanceCm(
		ToolSpec,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm,
		StampDepthCm);
}


double UMD_ScrapeSurfaceComponent::GetScrapeSampleSpacingCm(
	const FMD_ScrapeToolSpec& ToolSpec) const
{
	return FMD_ScrapeEvaluator::CalculateStrokeSampleSpacingCm(
		ToolSpec,
		ScrapeSettings,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm);
}


int32 UMD_ScrapeSurfaceComponent::ResolveContaminationPaintPatternSeed(
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const int32 PatternSeed) const
{
	const uint32 SurfacePatternHash = HashCombineFast(
		GetTypeHash(PatternSeed),
		BuildStableContaminationPaintSurfaceSalt(this, SurfaceFace));
	return static_cast<int32>(
		SurfacePatternHash & static_cast<uint32>(MAX_int32));
}


FMD_ScrapeResult UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeInternal(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FVector2D& StampPathStartPositionCm,
	const FVector2D& StampPathAxis,
	const float StampRatio,
	const float StampCoverage,
	const float StampDepthCm,
	const float StampSpacingCm,
	const int32 StampPatternSeed,
	float& OutRemovedCleanRatioDelta,
	const bool bDeferCompletionBroadcast,
	const bool bUseExactRequestedAmountStamp)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeStrokeTotal);
	OutRemovedCleanRatioDelta = 0.0f;

	if (!bContaminationEnabled || !HasAuthority())
	{
		return FMD_ScrapeResult();
	}

	// Spawn 직후 호출도 지원
	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	if (!bContaminationSurfaceInitialized)
	{
		return FMD_ScrapeResult();
	}
	if (!CanAcceptRecoveryTransaction(Stroke.SurfaceFace))
	{
		FMD_ScrapeResult InvalidResult;
		InvalidResult.SurfaceFace = Stroke.SurfaceFace;
		return InvalidResult;
	}

	bool bWasFullyRemoved = false;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
		bWasFullyRemoved = IsContaminationFullyRemoved();
	}

	TArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Stroke.SurfaceFace);
	if (FaceCells.IsEmpty())
	{
		FMD_ScrapeResult InvalidResult;
		InvalidResult.SurfaceFace = Stroke.SurfaceFace;
		return InvalidResult;
	}
	const int32 FaceSlot = Stroke.SurfaceFace == EMD_ScrapeSurfaceFace::Back
		? 1
		: 0;
	// 잘못된 runtime lot은 셀을 먼저 바꾸고 뒤늦게 발견하지 않는다.
	// 각 layer 안의 여러 source는 균질하게 섞여 비례 소비된다.
	const double BaseRecoveryRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			BaseContaminationRecoveryLots[FaceSlot]);
	const double TransferredRecoveryRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			TransferredContaminationRecoveryLots[FaceSlot]);
	if (FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			BaseContaminationRecoveryLots[FaceSlot]) < 0.0
		|| FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			TransferredContaminationRecoveryLots[FaceSlot]) < 0.0
		|| BaseRecoveryRewardUnits < 0.0
		|| TransferredRecoveryRewardUnits < 0.0)
	{
		FMD_ScrapeResult InvalidResult;
		InvalidResult.SurfaceFace = Stroke.SurfaceFace;
		return InvalidResult;
	}
	// 저작/절차 생성 직후 고정한 분모만 제품 net 1% -> Load 1 환산에 쓴다.
	// 화면용 초기량 cache는 밤샘/legacy 복원 때 달라질 수 있으므로 여기서
	// 읽으면 같은 canonical face의 총 Load 가치가 Day에 따라 변한다.
	const double InitialContaminationAmount =
		CanonicalInitialContaminationCarrierAmounts[FaceSlot];
	FMD_ScrapeSettings EffectiveScrapeSettings = ScrapeSettings;
	if (BaseRecoveryRewardUnits + TransferredRecoveryRewardUnits > 0.0)
	{
		// 보상 provenance가 붙은 미세 오염은 CleanTolerance로 조용히
		// 지우지 않는다. 실제로 긁혀 owner가 Tool/물리 경로로 이동할 때까지
		// 셀과 원장을 함께 유지해 가치가 논리-clean Surface에 갇히지 않는다.
		EffectiveScrapeSettings.CleanTolerance = 0.0f;
	}

	const bool bApplyExactRequestedAmountStamp =
		bUseExactRequestedAmountStamp
		&& FMath::IsFinite(StampRatio)
		&& StampRatio > 0.0f;
	TArray<FMD_ScrapeSurfaceCell> OriginalFaceCells;
	if (bApplyExactRequestedAmountStamp)
	{
		// Evaluator의 Normalize와 제거도 transaction에 포함된다. 상한 거부 시
		// 다섯 셀 필드를 정확히 되돌릴 수 있도록 전체 원본을 한 번 보관한다.
		OriginalFaceCells.Append(FaceCells.GetData(), FaceCells.Num());
	}
	FMD_ScrapeResult Result;
	TArray<float> RequestedRemovalAmounts;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeEvaluate);
		if (bApplyExactRequestedAmountStamp)
		{
			Result =
				FMD_ScrapeEvaluator::ApplyStrokeToGridAndCaptureRequestedAmounts(
					Stroke,
					ToolSpec,
					EffectiveScrapeSettings,
					ContaminationGridDimensions,
					ResolvedContaminationSurfaceSizeCm,
					FaceCells,
					RequestedRemovalAmounts);
		}
		else
		{
			Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
				Stroke,
				ToolSpec,
				EffectiveScrapeSettings,
				ContaminationGridDimensions,
				ResolvedContaminationSurfaceSizeCm,
				FaceCells);
		}
	}

	// 잘못된 입력은 상태를 변경하지 않음
	if (!Result.bIsInputValid)
	{
		return Result;
	}
	const float GrossRemovedContaminationAmount =
		Result.RemovedContaminationAmount;
	const float GrossRemovedCleanRatioDelta = Result.CleanRatioDelta;
	TArray<FMD_ScrapeCellChange> StampChanges;
	float AddedContaminationAmount = 0.0f;
	if (bApplyExactRequestedAmountStamp
		&& !RequestedRemovalAmounts.IsEmpty())
	{
		if (!FMD_ScrapeEvaluator::TryApplyContaminationStampToGrid(
			ContaminationGridDimensions,
			RequestedRemovalAmounts,
			StampRatio,
			FaceCells,
			StampChanges,
			AddedContaminationAmount))
		{
			for (int32 CellIndex = 0; CellIndex < FaceCells.Num(); ++CellIndex)
			{
				FaceCells[CellIndex] = OriginalFaceCells[CellIndex];
			}

			FMD_ScrapeResult RejectedResult;
			RejectedResult.SurfaceFace = Stroke.SurfaceFace;
			return RejectedResult;
		}
	}
	else if (FMath::IsFinite(StampRatio)
		&& StampRatio > 0.0f
		&& FMath::IsFinite(StampCoverage)
		&& StampCoverage > 0.0f)
	{
		const uint32 SurfacePatternHash = HashCombineFast(
			GetTypeHash(StampPatternSeed),
			BuildStableContaminationPaintSurfaceSalt(
				this,
				Stroke.SurfaceFace));
		const int32 SurfacePatternSeed = static_cast<int32>(
			SurfacePatternHash & static_cast<uint32>(MAX_int32));
		FMD_ScrapeStroke PaintStroke;
		if (FMD_ScrapeEvaluator::TryBuildProtectedContaminationPaintStroke(
				Stroke,
				StampPathStartPositionCm,
				StampPathAxis,
				ToolSpec,
				ContaminationGridDimensions,
				ResolvedContaminationSurfaceSizeCm,
				StampDepthCm,
				PaintStroke))
		{
			AddedContaminationAmount =
				FMD_ScrapeEvaluator::ApplyContaminationPaintStrokeToGrid(
				PaintStroke,
				ToolSpec,
				ContaminationGridDimensions,
				ResolvedContaminationSurfaceSizeCm,
				StampRatio,
				StampCoverage,
				StampDepthCm,
				StampSpacingCm,
				SurfacePatternSeed,
				FaceCells,
				StampChanges);
		}
	}
	OutRemovedCleanRatioDelta = GrossRemovedCleanRatioDelta;

	// MASS-003: 같은 transaction의 역오염까지 끝난 뒤 표면에서 실제로
	// 사라진 양만 Load와 물리 드롭이 나눠 갖는다. 새로 더럽힌 양이 gross
	// 제거량보다 큰 경우에도 음수 산출물은 만들지 않는다.
	const float NetRemovedContaminationAmount = FMath::Max(
		0.0f,
		GrossRemovedContaminationAmount - AddedContaminationAmount);
	Result.NetRemovedContaminationAmount =
		NetRemovedContaminationAmount;
	Result.NetRemovedCleanRatioDelta =
		InitialContaminationAmount > 0.0
		? static_cast<float>(FMath::Clamp(
			static_cast<double>(NetRemovedContaminationAmount)
				/ InitialContaminationAmount,
			0.0,
			1.0))
		: 0.0f;

	// MASS-011: evaluator가 나눈 base/transferred 실제 제거량만큼 각 layer
	// 원장에서 lot을 비례 추출한다. Stage3가 같은 transaction에서 되묻힌
	// raw는 추출 lot의 일부를 transferred layer로 되돌려 새 unit을 만들지
	// 않고, 제거량보다 더 생성된 부분만 reward 0으로 기록한다.
	TArray<FMD_ScrapeRecoveryLot> WorkingBaseLots =
		BaseContaminationRecoveryLots[FaceSlot];
	TArray<FMD_ScrapeRecoveryLot> WorkingTransferredLots =
		TransferredContaminationRecoveryLots[FaceSlot];
	TArray<FMD_ScrapeRecoveryLot> RemovedBaseLots;
	TArray<FMD_ScrapeRecoveryLot> RemovedTransferredLots;
	TArray<FMD_ScrapeRecoveryLot> GrossRemovedLots;
	const bool bExtractedBaseLots = TryExtractRecoveryLots(
		WorkingBaseLots,
		static_cast<double>(Result.RemovedBaseContaminationAmount),
		RemovedBaseLots);
	const bool bExtractedTransferredLots = TryExtractRecoveryLots(
		WorkingTransferredLots,
		static_cast<double>(Result.RemovedTransferredContaminationAmount),
		RemovedTransferredLots);
	double FinalBaseCarrierAmount = 0.0;
	double FinalTransferredCarrierAmount = 0.0;
	for (const FMD_ScrapeSurfaceCell& Cell : FaceCells)
	{
		FinalBaseCarrierAmount +=
			static_cast<double>(Cell.RemainingContamination);
		FinalTransferredCarrierAmount +=
			static_cast<double>(Cell.TransferredContamination);
	}
	const double FinalTransferredCarrierBeforeStamp = FMath::Max(
		0.0,
		FinalTransferredCarrierAmount
			- static_cast<double>(AddedContaminationAmount));
	const bool bReconciledBaseLots = bExtractedBaseLots
		&& TryReconcileRecoveryLayer(
			WorkingBaseLots,
			FinalBaseCarrierAmount,
			RemovedBaseLots);
	const bool bReconciledTransferredLots = bExtractedTransferredLots
		&& TryReconcileRecoveryLayer(
			WorkingTransferredLots,
			FinalTransferredCarrierBeforeStamp,
			RemovedTransferredLots);
	const bool bBuiltGrossLots = bReconciledBaseLots
		&& bReconciledTransferredLots
		&& TryAppendRecoveryLots(GrossRemovedLots, RemovedBaseLots)
		&& TryAppendRecoveryLots(GrossRemovedLots, RemovedTransferredLots)
		&& FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			GrossRemovedLots,
			static_cast<double>(GrossRemovedContaminationAmount));
	if (!bBuiltGrossLots)
	{
		if (bApplyExactRequestedAmountStamp)
		{
			for (int32 CellIndex = 0; CellIndex < FaceCells.Num(); ++CellIndex)
			{
				FaceCells[CellIndex] = OriginalFaceCells[CellIndex];
			}
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		return RejectedResult;
	}

	const double ReturnedCarrierAmount = FMath::Min(
		static_cast<double>(AddedContaminationAmount),
		static_cast<double>(GrossRemovedContaminationAmount));
	TArray<FMD_ScrapeRecoveryLot> ReturnedLots;
	if (!TryExtractRecoveryLots(
			GrossRemovedLots,
			ReturnedCarrierAmount,
			ReturnedLots)
		|| !TryAppendRecoveryLots(WorkingTransferredLots, ReturnedLots))
	{
		if (bApplyExactRequestedAmountStamp)
		{
			for (int32 CellIndex = 0; CellIndex < FaceCells.Num(); ++CellIndex)
			{
				FaceCells[CellIndex] = OriginalFaceCells[CellIndex];
			}
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		return RejectedResult;
	}

	const double ZeroRewardAddedCarrierAmount = FMath::Max(
		0.0,
		static_cast<double>(AddedContaminationAmount)
			- ReturnedCarrierAmount);
	if ((ZeroRewardAddedCarrierAmount > 0.0
			&& !FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				WorkingTransferredLots,
				ZeroRewardAddedCarrierAmount))
		|| !FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			GrossRemovedLots,
			static_cast<double>(NetRemovedContaminationAmount))
		|| !FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			WorkingTransferredLots,
			FinalTransferredCarrierAmount))
	{
		if (bApplyExactRequestedAmountStamp)
		{
			for (int32 CellIndex = 0; CellIndex < FaceCells.Num(); ++CellIndex)
			{
				FaceCells[CellIndex] = OriginalFaceCells[CellIndex];
			}
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		return RejectedResult;
	}

	Result.NetRemovedRecoveryLots = GrossRemovedLots;
	Result.NetRemovedRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			Result.NetRemovedRecoveryLots);
	if (Result.NetRemovedRewardUnits < 0.0)
	{
		if (bApplyExactRequestedAmountStamp)
		{
			for (int32 CellIndex = 0; CellIndex < FaceCells.Num(); ++CellIndex)
			{
				FaceCells[CellIndex] = OriginalFaceCells[CellIndex];
			}
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		return RejectedResult;
	}
	BaseContaminationRecoveryLots[FaceSlot] = MoveTemp(WorkingBaseLots);
	TransferredContaminationRecoveryLots[FaceSlot] =
		MoveTemp(WorkingTransferredLots);
	const double RemainingFaceRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			BaseContaminationRecoveryLots[FaceSlot])
		+ FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			TransferredContaminationRecoveryLots[FaceSlot]);
	const float EffectiveCompletionTolerance =
		RemainingFaceRewardUnits > 0.0
			? 0.0f
			: ScrapeSettings.CleanTolerance;

	if (!StampChanges.IsEmpty())
	{
		// gross 제거량은 진단과 셀 변경 내역으로 보존한다. 실제 Load·물리
		// 산출 귀속은 위에서 계산한 net 필드만 사용한다.
		// 완료 여부와 셀 변경 목록은 제거 뒤 되묻힌 최종 상태에 맞춘다.
		Result.CleanRatioAfter =
			FMD_ScrapeEvaluator::CalculateCleanRatio(FaceCells);
		Result.CleanRatioDelta = FMath::Max(
			0.0f,
			Result.CleanRatioAfter - Result.CleanRatioBefore);
		Result.bIsSurfaceFullyClean =
			FMD_ScrapeEvaluator::IsSurfaceFullyClean(
				FaceCells,
				EffectiveCompletionTolerance);

		Result.NewlyCleanedCellCount = 0;
		for (FMD_ScrapeCellChange& CellChange : Result.CellChanges)
		{
			if (!FaceCells.IsValidIndex(CellChange.CellIndex))
			{
				continue;
			}

			CellChange.RemainingAmountAfter =
				FaceCells[CellChange.CellIndex].RemainingContamination;
			CellChange.bBecameClean = CellChange.bBecameClean
				&& CellChange.RemainingAmountAfter
					<= EffectiveCompletionTolerance
				&& FaceCells[CellChange.CellIndex].TransferredContamination
					<= EffectiveCompletionTolerance;
			if (CellChange.bBecameClean)
			{
				++Result.NewlyCleanedCellCount;
			}
		}
	}

	const int32 CellsPerFace = FaceCells.Num();
	for (FMD_ScrapeCellChange& CellChange : Result.CellChanges)
	{
		CellChange.CellIndex = FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			Stroke.SurfaceFace,
			CellChange.CellIndex,
			CellsPerFace);
	}
	for (FMD_ScrapeCellChange& CellChange : StampChanges)
	{
		CellChange.CellIndex = FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
			Stroke.SurfaceFace,
			CellChange.CellIndex,
			CellsPerFace);
	}

	TArray<FMD_ScrapeCellChange> SurfaceChanges = Result.CellChanges;
	TMap<int32, int32> SurfaceChangeIndexByCell;
	SurfaceChangeIndexByCell.Reserve(
		Result.CellChanges.Num() + StampChanges.Num());
	for (int32 ChangeIndex = 0;
		ChangeIndex < SurfaceChanges.Num();
		++ChangeIndex)
	{
		SurfaceChangeIndexByCell.Add(
			SurfaceChanges[ChangeIndex].CellIndex,
			ChangeIndex);
	}
	for (const FMD_ScrapeCellChange& StampChange : StampChanges)
	{
		if (const int32* ExistingChangeIndex =
			SurfaceChangeIndexByCell.Find(StampChange.CellIndex))
		{
			FMD_ScrapeCellChange& ExistingChange =
				SurfaceChanges[*ExistingChangeIndex];
			ExistingChange.RemainingAmountAfter =
				StampChange.RemainingAmountAfter;
			ExistingChange.bBecameClean = false;
			continue;
		}

		const int32 AddedChangeIndex = SurfaceChanges.Add(StampChange);
		SurfaceChangeIndexByCell.Add(
			StampChange.CellIndex,
			AddedChangeIndex);
	}
	Result.CellChanges = SurfaceChanges;
	Result.ModifiedCellCount = SurfaceChanges.Num();

	// 스트로크는 한 면만 건드리므로 그 면만 다시 계산한다.
	RefreshContaminationCleanRatioCache(Stroke.SurfaceFace);
	UpdateContaminationMask(SurfaceChanges);
	PublishContaminationChanges(SurfaceChanges);

	// MASS-004: 여기서는 표면 상태만 확정한다. Tool이 실제 Load 수용량을
	// 확인한 뒤 받지 못한 순제거 몫만 CommitContaminationPhysicalDrop으로
	// 한 번 전달한다.

	// 어느 한 면이 처음 완료되는 전환에서 한 번만 방송
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (bDeferCompletionBroadcast)
	{
		if (!bWasFullyRemoved
			&& bIsFullyRemoved
			&& !bContaminationCompletionBroadcast)
		{
			bDeferredContaminationCompletionTransition = true;
		}
		else if (!bIsFullyRemoved)
		{
			bDeferredContaminationCompletionTransition = false;
			bContaminationCompletionBroadcast = false;
		}
	}
	else if (!bWasFullyRemoved
		&& bIsFullyRemoved
		&& !bContaminationCompletionBroadcast)
	{
		bDeferredContaminationCompletionTransition = false;
		bContaminationCompletionBroadcast = true;
		OnContaminationFullyRemoved.Broadcast(this);
	}
	else if (!bIsFullyRemoved)
	{
		bDeferredContaminationCompletionTransition = false;
		bContaminationCompletionBroadcast = false;
	}

	return Result;
}


float UMD_ScrapeSurfaceComponent::GetContaminationCleanRatio() const
{
	if (!bContaminationEnabled)
	{
		return 1.0f;
	}

	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	const double TotalInitialContaminationAmount =
		CachedFrontInitialContaminationAmount
		+ CachedBackInitialContaminationAmount;
	const double TotalCurrentContaminationAmount =
		CachedFrontCurrentContaminationAmount
		+ CachedBackCurrentContaminationAmount;
	return FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
		TotalInitialContaminationAmount,
		TotalCurrentContaminationAmount);
}

float UMD_ScrapeSurfaceComponent::GetContaminationCleanRatio(
	const EMD_ScrapeSurfaceFace Face) const
{
	if (!bContaminationEnabled)
	{
		return 1.0f;
	}

	if (!bContaminationSurfaceInitialized)
	{
		return 0.0f;
	}

	switch (Face)
	{
	case EMD_ScrapeSurfaceFace::Front:
		return FMath::Clamp(
			CachedFrontContaminationCleanRatio,
			0.0f,
			1.0f);

	case EMD_ScrapeSurfaceFace::Back:
		return FMath::Clamp(
			CachedBackContaminationCleanRatio,
			0.0f,
			1.0f);

	default:
		return 0.0f;
	}
}

bool UMD_ScrapeSurfaceComponent::IsContaminationFullyRemoved() const
{
	if (!bContaminationEnabled)
	{
		return true;
	}

	return IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Front)
		|| IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Back);
}

bool UMD_ScrapeSurfaceComponent::IsContaminationFullyRemoved(
	const EMD_ScrapeSurfaceFace Face) const
{
	if (!bContaminationEnabled)
	{
		return true;
	}

	const TConstArrayView<FMD_ScrapeSurfaceCell> FaceCells =
		GetContaminationCellsForFace(Face);
	const int32 FaceSlot = Face == EMD_ScrapeSurfaceFace::Back ? 1 : 0;
	const double RemainingRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			BaseContaminationRecoveryLots[FaceSlot])
		+ FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			TransferredContaminationRecoveryLots[FaceSlot]);
	if (RemainingRewardUnits < 0.0)
	{
		return false;
	}
	const float EffectiveCleanTolerance = RemainingRewardUnits > 0.0
		? 0.0f
		: ScrapeSettings.CleanTolerance;
	return bContaminationSurfaceInitialized
		&& !FaceCells.IsEmpty()
		&& FMD_ScrapeEvaluator::IsSurfaceFullyClean(
			FaceCells,
			EffectiveCleanTolerance);
}

void UMD_ScrapeSurfaceComponent::RefreshContaminationCleanRatioCache()
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeCleanRatio);
	RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Front);
	RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Back);
}

void UMD_ScrapeSurfaceComponent::RefreshContaminationCleanRatioCache(
	const EMD_ScrapeSurfaceFace Face)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeCleanRatio);

	// 정의되지 않은 면을 그대로 넘기면 빈 슬라이스가 "오염 없음"으로 계산돼
	// 제거율이 1.0으로 잘못 캐시되므로 반드시 먼저 막는다.
	if (Face != EMD_ScrapeSurfaceFace::Front
		&& Face != EMD_ScrapeSurfaceFace::Back)
	{
		return;
	}

	// 제거 비율 계산은 면 전체를 훑으므로, 바뀌지 않은 면까지 다시 계산하지 않는다.
	float& CachedCleanRatio = Face == EMD_ScrapeSurfaceFace::Back
		? CachedBackContaminationCleanRatio
		: CachedFrontContaminationCleanRatio;
	double& CachedInitialContaminationAmount =
		Face == EMD_ScrapeSurfaceFace::Back
		? CachedBackInitialContaminationAmount
		: CachedFrontInitialContaminationAmount;
	double& CachedCurrentContaminationAmount =
		Face == EMD_ScrapeSurfaceFace::Back
		? CachedBackCurrentContaminationAmount
		: CachedFrontCurrentContaminationAmount;
	CachedCleanRatio = FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
		GetContaminationCellsForFace(Face),
		CachedInitialContaminationAmount,
		CachedCurrentContaminationAmount);
}
