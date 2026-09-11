// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "CompGeom/ConvexHull2.h"
#include "HAL/IConsoleManager.h"

#include "Containers/BitArray.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"

#include "Util/MD_LogUtility.h"

namespace
{
    TAutoConsoleVariable<int32> CVarCaptureBoundary(TEXT("md.Scrape.BoundaryCapture"),1,
        TEXT("Diagnostic baseline: 0 disables new footprint capture; restart the test world for paired comparisons."));
	// 부분 접촉으로 남기는 잔여 오염이 기준량의 이 비율보다 작으면
	// 화면에서 사실상 보이지 않아 플레이어가 남은 자리를 찾을 수 없다.
	// 그런 잔여는 아예 남기지 않아 완료 판정이 막히지 않게 한다.
	constexpr double MinimumVisibleContaminationFraction = 0.1;

	/**
	 * 얇은 막 정리를 적용할 최소 피복률.
	 *
	 * 이 값보다 덜 덮인 셀은 자국 경계를 이루는 부분 피복 셀이다. 그쪽까지
	 * 정리하면 경계 램프에 구멍이 생겨 테두리가 셀 단위 계단으로 돌아간다.
	 */
	constexpr float FullCoverageFilmCleanupFraction = 0.98f;

	enum class EBladeBasisResolveState : uint8
	{
		Unset,
		Valid,
		Invalid
	};

	struct FResolvedBladeBasis
	{
		FVector2D DepthAxis = FVector2D::ZeroVector;
		FVector2D WidthAxis = FVector2D::ZeroVector;
	};

	struct FResolvedStrokeBladeBasis
	{
		FResolvedBladeBasis Start;
		FResolvedBladeBasis End;
		double SignedRotationRadians = 0.0;
		bool bUsesExplicitBasis = false;
	};

	bool IsFiniteVector2D(const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	}

	EBladeBasisResolveState ResolveBladeBasis(
		const FMD_ScrapeBladeBasis& Basis,
		FResolvedBladeBasis& OutBasis)
	{
		OutBasis = FResolvedBladeBasis();
		if (!IsFiniteVector2D(Basis.DepthAxis)
			|| !IsFiniteVector2D(Basis.WidthAxis))
		{
			return EBladeBasisResolveState::Invalid;
		}

		const bool bHasDepthAxis = !Basis.DepthAxis.IsNearlyZero();
		const bool bHasWidthAxis = !Basis.WidthAxis.IsNearlyZero();
		if (!bHasDepthAxis && !bHasWidthAxis)
		{
			return EBladeBasisResolveState::Unset;
		}
		if (!bHasDepthAxis || !bHasWidthAxis)
		{
			return EBladeBasisResolveState::Invalid;
		}

		const FVector2D SafeDepthAxis = Basis.DepthAxis.GetSafeNormal();
		const FVector2D OrthogonalWidthAxis = Basis.WidthAxis
			- SafeDepthAxis * FVector2D::DotProduct(
				SafeDepthAxis,
				Basis.WidthAxis);
		const FVector2D SafeWidthAxis = OrthogonalWidthAxis.GetSafeNormal();
		if (!IsFiniteVector2D(SafeDepthAxis)
			|| !IsFiniteVector2D(SafeWidthAxis)
			|| SafeDepthAxis.IsNearlyZero()
			|| SafeWidthAxis.IsNearlyZero())
		{
			return EBladeBasisResolveState::Invalid;
		}

		OutBasis.DepthAxis = SafeDepthAxis;
		OutBasis.WidthAxis = SafeWidthAxis;
		return EBladeBasisResolveState::Valid;
	}

	bool HaveMatchingBladeBasisHandedness(
		const FResolvedBladeBasis& StartBasis,
		const FResolvedBladeBasis& EndBasis)
	{
		const double StartHandedness =
			StartBasis.DepthAxis.X * StartBasis.WidthAxis.Y
			- StartBasis.DepthAxis.Y * StartBasis.WidthAxis.X;
		const double EndHandedness =
			EndBasis.DepthAxis.X * EndBasis.WidthAxis.Y
			- EndBasis.DepthAxis.Y * EndBasis.WidthAxis.X;
		return StartHandedness * EndHandedness > 0.0;
	}

	FVector2D RotateBladeAxis(
		const FVector2D& Axis,
		const double AngleRadians)
	{
		const double CosAngle = FMath::Cos(AngleRadians);
		const double SinAngle = FMath::Sin(AngleRadians);
		return FVector2D(
			Axis.X * CosAngle - Axis.Y * SinAngle,
			Axis.X * SinAngle + Axis.Y * CosAngle);
	}

	bool TryResolveStrokeBladeBasis(
		const FMD_ScrapeStroke& Stroke,
		const FVector2D& LegacyDepthAxis,
		FResolvedStrokeBladeBasis& OutBasis)
	{
		OutBasis = FResolvedStrokeBladeBasis();
		FResolvedBladeBasis StartBasis;
		FResolvedBladeBasis EndBasis;
		const EBladeBasisResolveState StartState = ResolveBladeBasis(
			Stroke.StartBladeBasis,
			StartBasis);
		const EBladeBasisResolveState EndState = ResolveBladeBasis(
			Stroke.EndBladeBasis,
			EndBasis);
		if (StartState == EBladeBasisResolveState::Invalid
			|| EndState == EBladeBasisResolveState::Invalid)
		{
			return false;
		}

		if (StartState == EBladeBasisResolveState::Unset
			&& EndState == EBladeBasisResolveState::Unset)
		{
			const FVector2D SafeLegacyDepthAxis = LegacyDepthAxis.GetSafeNormal();
			if (!IsFiniteVector2D(SafeLegacyDepthAxis)
				|| SafeLegacyDepthAxis.IsNearlyZero())
			{
				return false;
			}

			OutBasis.Start.DepthAxis = SafeLegacyDepthAxis;
			OutBasis.Start.WidthAxis = FVector2D(
				-SafeLegacyDepthAxis.Y,
				SafeLegacyDepthAxis.X);
			OutBasis.End = OutBasis.Start;
			return true;
		}

		OutBasis.bUsesExplicitBasis = true;
		if (StartState == EBladeBasisResolveState::Unset)
		{
			StartBasis = EndBasis;
		}
		if (EndState == EBladeBasisResolveState::Unset)
		{
			EndBasis = StartBasis;
		}
		if (!HaveMatchingBladeBasisHandedness(StartBasis, EndBasis))
		{
			return false;
		}

		// 날 방향은 180도 동치다. 끝 basis 전체를 뒤집어 같은 반구에 둔다.
		if (FVector2D::DotProduct(StartBasis.DepthAxis, EndBasis.DepthAxis) < 0.0)
		{
			EndBasis.DepthAxis *= -1.0;
			EndBasis.WidthAxis *= -1.0;
		}

		const double Dot = FMath::Clamp(
			FVector2D::DotProduct(StartBasis.DepthAxis, EndBasis.DepthAxis),
			-1.0,
			1.0);
		const double Cross = StartBasis.DepthAxis.X * EndBasis.DepthAxis.Y
			- StartBasis.DepthAxis.Y * EndBasis.DepthAxis.X;

		OutBasis.Start = StartBasis;
		OutBasis.End = EndBasis;
		OutBasis.SignedRotationRadians = FMath::Atan2(Cross, Dot);
		return FMath::IsFinite(OutBasis.SignedRotationRadians);
	}

	FResolvedBladeBasis InterpolateBladeBasis(
		const FResolvedStrokeBladeBasis& Basis,
		const double Alpha)
	{
		const double AngleRadians = Basis.SignedRotationRadians
			* FMath::Clamp(Alpha, 0.0, 1.0);
		FResolvedBladeBasis Result;
		Result.DepthAxis = RotateBladeAxis(Basis.Start.DepthAxis, AngleRadians);
		Result.WidthAxis = RotateBladeAxis(Basis.Start.WidthAxis, AngleRadians);
		return Result;
	}

	void ApplyScrapeCoverageSample(
		FMD_ScrapeSurfaceCell& Cell, const double CoverageWeight,
		const double FullContactCoverageWeight, const double TravelDistancePerSampleCm,
		const FMD_ScrapeToolSpec& ToolSpec, const FMD_ScrapeSettings& Settings,
		FMD_ScrapeResult& Result, bool& bHasContaminationContact,
		float* OutRequestedRemovalAmount, const bool bApplyRemoval)
	{
		const double BaseRemainingBeforeSample =
			static_cast<double>(Cell.RemainingContamination);
		const double PaintRemainingBeforeSample =
			static_cast<double>(Cell.TransferredContamination);
		const double TotalRemainingBeforeSample =
			BaseRemainingBeforeSample + PaintRemainingBeforeSample;

		// 제거량은 Tick 횟수나 DeltaTime이 아니라 실제 이동 거리 기준
		// 따라서 30fps와 120fps에서 같은 거리로 움직인 헤라는 거의 같은 결과
		const double RequestedRemovalAmount =
			static_cast<double>(ToolSpec.RemovalPerTravelCentimeter)
			* TravelDistancePerSampleCm
			* CoverageWeight;
		if (OutRequestedRemovalAmount)
		{
			*OutRequestedRemovalAmount +=
				static_cast<float>(RequestedRemovalAmount);
		}

		// A saturated blade only measures its footprint. Preserve both dirt layers and coverage.
		if (!bApplyRemoval)
		{
			if (TotalRemainingBeforeSample <= static_cast<double>(Settings.CleanTolerance))
			{
				Result.bTouchedBareSurface = true;
			}
			else
			{
				bHasContaminationContact = true;
			}
			return;
		}

		const double NormalizedCoverageWeight =
			FullContactCoverageWeight > UE_SMALL_NUMBER
			? FMath::Min(
				1.0,
				CoverageWeight / FullContactCoverageWeight)
			: 1.0;
		Cell.CoveredFraction = FMath::Max(
			Cell.CoveredFraction,
			static_cast<float>(NormalizedCoverageWeight));

		// 이미 깨끗한 셀에 작업량이 가해지면 모든 힘이 맨 벽면으로 전달
		if (TotalRemainingBeforeSample
			<= static_cast<double>(Settings.CleanTolerance))
		{
			Result.bTouchedBareSurface = true;
			return;
		}

		// 셀을 일부만 덮은 헤라는 덮은 면적만큼만 벗겨낼 수 있다
		// 이 하한이 없으면 살짝 걸친 셀도 반복 접촉으로 결국 0까지 깎이고,
		// 마스크가 셀 단위 0/1로만 남아 자국 경계가 격자 계단 모양이 된다
		const double ReferenceContamination =
			static_cast<double>(Cell.InitialContamination);
		const double UncoveredContamination = ReferenceContamination
			* (1.0 - NormalizedCoverageWeight);
		const double CoverageLimitedRemainingFloor =
			UncoveredContamination
				>= ReferenceContamination
					* MinimumVisibleContaminationFraction
			? UncoveredContamination
			: 0.0;
		const double RemovableTotalAmount = FMath::Max(
			0.0,
			TotalRemainingBeforeSample
				- CoverageLimitedRemainingFloor);
		const double AllowedRemovalAmount = FMath::Min(
			RequestedRemovalAmount,
			RemovableTotalAmount);

		const double RemovedTransferredAmount = FMath::Min(
			PaintRemainingBeforeSample,
			AllowedRemovalAmount);
		const double RemainingRemovalBudget = FMath::Max(
			0.0,
			AllowedRemovalAmount - RemovedTransferredAmount);
		const double RemovedBaseAmount = FMath::Min(
			BaseRemainingBeforeSample,
			RemainingRemovalBudget);
		const double ActualRemovedAmount =
			RemovedTransferredAmount + RemovedBaseAmount;

		Cell.RemainingContamination = static_cast<float>(
			FMath::Max(0.0, BaseRemainingBeforeSample - RemovedBaseAmount));
		Cell.TransferredContamination = static_cast<float>(
			FMath::Max(
				0.0,
				PaintRemainingBeforeSample - RemovedTransferredAmount));

		// 허용 오차보다 작은 찌꺼기는 0으로 스냅해 완료 판정과 시각 마스크를 일치
		if (Cell.RemainingContamination <= Settings.CleanTolerance)
		{
			Cell.RemainingContamination = 0.0f;
		}
		// 날에 완전히 덮인 셀에 마스크가 표현할 수 없을 만큼 얇은 막만
		// 남았다면 끝낸 것으로 본다. 안 그러면 깨끗해 보이는데 완료만
		// 막히는 셀이 남는다. 부분 피복 셀은 제외해야 자국 경계의 피복
		// 램프에 구멍이 생기지 않는다.
		if (Cell.CoveredFraction >= FullCoverageFilmCleanupFraction
			&& Cell.RemainingContamination
				<= Cell.InitialContamination
					* FMath::Max(0.0f, Settings.NegligibleFilmFraction))
		{
			Cell.RemainingContamination = 0.0f;
		}
		if (Cell.TransferredContamination <= Settings.CleanTolerance)
		{
			Cell.TransferredContamination = 0.0f;
			Cell.TransferredPaintPatternDensity = 0.0f;
		}

		if (ActualRemovedAmount > UE_SMALL_NUMBER)
		{
			bHasContaminationContact = true;
		}

		// 요청 작업량이 덮은 면적에서 벗겨낼 수 있는 양보다 컸다면
		// 그 자리의 오염막을 다 벗긴 뒤에도 힘이 남은 상태
		// 그 여분의 작업량은 맨 벽면을 긁은 것으로 분류해 위험한 피드백의 근거로 기록
		if (RequestedRemovalAmount
			> RemovableTotalAmount
				+ static_cast<double>(Settings.CleanTolerance))
		{
			Result.bTouchedBareSurface = true;
		}
	}

	bool IsCellUnclean(const FMD_ScrapeSurfaceCell& Cell, const float Tolerance)
	{
		const float Transferred = FMath::IsFinite(Cell.TransferredContamination)
			? FMath::Clamp(Cell.TransferredContamination, 0.0f, 1.0f) : 0.0f;
		const float Initial = FMath::IsFinite(Cell.InitialContamination)
			? FMath::Clamp(Cell.InitialContamination, 0.0f, 1.0f) : 0.0f;
		const float Remaining = FMath::IsFinite(Cell.RemainingContamination)
			? FMath::Clamp(Cell.RemainingContamination, 0.0f, Initial) : 0.0f;
		return Transferred > Tolerance || (Initial > UE_SMALL_NUMBER && Remaining > Tolerance);
	}

	bool HasDetailedMeshUVProgress(const FMD_ScrapeMeshUVProgress& Progress, const int32 CellCount)
	{
		return Progress.bIsValid
			&& FMath::IsFinite(Progress.InitialCarrierAmount) && Progress.InitialCarrierAmount >= 0.0
			&& FMath::IsFinite(Progress.CurrentCarrierAmount) && Progress.CurrentCarrierAmount >= 0.0
			&& FMath::IsFinite(Progress.BaseCarrierAmount) && Progress.BaseCarrierAmount >= 0.0
			&& FMath::IsFinite(Progress.TransferredCarrierAmount) && Progress.TransferredCarrierAmount >= 0.0
			&& FMath::IsFinite(Progress.CleanTolerance) && Progress.CleanTolerance >= 0.0f
			&& Progress.UncleanCellCount >= 0 && Progress.UncleanCellCount <= CellCount
			&& Progress.UncleanCellCountAtZero >= Progress.UncleanCellCount
			&& Progress.UncleanCellCountAtZero <= CellCount;
	}

	void FinalizeScrapeEvaluation(
		const FIntPoint& GridDimensions, const FMD_ScrapeSettings& Settings,
		const FMD_ScrapeCellView InOutSurfaceCells,
		const FMD_ScrapeEvaluationScratch& Scratch,
		const bool bHasContaminationContact, FMD_ScrapeResult& Result,
		const FMD_ScrapeCarrierWeightView CellCarrierWeights = {},
		FMD_ScrapeMeshUVProgress* OutProgressAfter = nullptr,
		const FMD_ScrapeMeshUVProgress* ProgressBefore = nullptr)
	{
		const auto FinalizeCell = [&](const int32 CellIndex, const FMD_ScrapeSurfaceCell& Before)
		{
			++Result.TouchedCellCount;

			const FMD_ScrapeSurfaceCell& CellAfterStroke =
				InOutSurfaceCells.Read(CellIndex);
			const float RemovedBaseAmount = FMath::Max(
				0.0f,
				Before.RemainingContamination
					- CellAfterStroke.RemainingContamination);
			const float RemovedTransferredAmount = FMath::Max(
				0.0f,
				Before.TransferredContamination
					- CellAfterStroke.TransferredContamination);
			const float RemovedAmount =
				RemovedBaseAmount + RemovedTransferredAmount;
			const float AddedCoveredFraction = FMath::Max(
				0.0f,
				CellAfterStroke.CoveredFraction
					- Before.CoveredFraction);

			// 부동소수점 연산 중 생길 수 있는 무의미한 극소 차이는 변경 내역에서 제외
			if (RemovedAmount <= UE_SMALL_NUMBER
				&& AddedCoveredFraction <= UE_SMALL_NUMBER)
			{
				return;
			}

			FMD_ScrapeCellChange& CellChange = Result.CellChanges.AddDefaulted_GetRef();
			CellChange.CellIndex = CellIndex;
			CellChange.GridCoordinate = FIntPoint(
				CellIndex % GridDimensions.X,
				CellIndex / GridDimensions.X);
			const double CarrierWeight = CellCarrierWeights.IsEmpty() ? 1.0 : CellCarrierWeights[CellIndex];
			CellChange.RemovedAmount = static_cast<float>(RemovedAmount * CarrierWeight);
			CellChange.RemainingAmountAfter =
				CellAfterStroke.RemainingContamination;
			CellChange.bBecameClean =
				(Before.RemainingContamination > Settings.CleanTolerance
					|| Before.TransferredContamination > Settings.CleanTolerance)
				&& CellAfterStroke.RemainingContamination <= Settings.CleanTolerance
				&& CellAfterStroke.TransferredContamination <= Settings.CleanTolerance;

			// Surface의 보상 provenance 원장은 base와 Stage3 transferred를
			// 서로 다른 layer로 보존한다. RPC/Blueprint에는 노출하지 않는
			// server-only 합계만 기록해 실제 제거 비율대로 lot을 옮길 수 있게 한다.
			Result.RemovedBaseContaminationAmount += static_cast<float>(RemovedBaseAmount * CarrierWeight);
			Result.RemovedTransferredContaminationAmount +=
				static_cast<float>(RemovedTransferredAmount * CarrierWeight);
			Result.RemovedContaminationAmount += CellChange.RemovedAmount;

			if (CellChange.bBecameClean)
			{
				++Result.NewlyCleanedCellCount;
			}
		};
		if (CellCarrierWeights.IsEmpty())
		{
			for (TConstSetBitIterator<> It(Scratch.TouchedCells); It; ++It)
			{
				const int32 Index = It.GetIndex();
				FMD_ScrapeSurfaceCell Before;
				Before.RemainingContamination = Scratch.RemainingBeforeStroke[Index];
				Before.TransferredContamination = Scratch.TransferredBeforeStroke[Index];
				Before.CoveredFraction = Scratch.CoveredFractionBeforeStroke[Index];
				FinalizeCell(Index, Before);
			}
		}
		else
		{
			for (const FMD_ScrapeTouchedCell& Record : Scratch.MeshUVTouchedCells)
			{
				FinalizeCell(Record.CellIndex, Record.Before);
			}
		}

		Result.ModifiedCellCount = Result.CellChanges.Num();
		if (CellCarrierWeights.IsEmpty())
		{
			Result.CleanRatioAfter = FMD_ScrapeEvaluator::CalculateCleanRatio(InOutSurfaceCells);
		}
		else
		{
			FMD_ScrapeMeshUVProgress ProgressAfter;
			const bool bUpdated = ProgressBefore
				&& FMD_ScrapeEvaluator::UpdateMeshUVProgressFromTouchedCells(*ProgressBefore,
					Scratch.MeshUVTouchedCells, InOutSurfaceCells, CellCarrierWeights, ProgressAfter);
			if (!bUpdated)
			{
				FMD_ScrapeEvaluator::CalculateMeshUVProgress(InOutSurfaceCells, CellCarrierWeights,
					Settings.CleanTolerance, ProgressAfter);
			}
			Result.CleanRatioAfter = FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
				ProgressAfter.InitialCarrierAmount, ProgressAfter.CurrentCarrierAmount);
			Result.bIsSurfaceFullyClean = ProgressAfter.bIsValid
				&& (Settings.CleanTolerance == 0.0f
					? ProgressAfter.UncleanCellCountAtZero : ProgressAfter.UncleanCellCount) == 0;
			if (OutProgressAfter) { *OutProgressAfter = ProgressAfter; }
		}

		Result.CleanRatioDelta = FMath::Max(
			0.0f,
			Result.CleanRatioAfter - Result.CleanRatioBefore);
		// 순수 제거 판정에는 아직 역오염 단계가 없으므로 gross와 net이 같다.
		// Surface가 같은 트랜잭션에서 역오염을 더하면 두 net 필드만 다시 줄인다.
		Result.NetRemovedContaminationAmount =
			Result.RemovedContaminationAmount;
		Result.NetRemovedCleanRatioDelta = Result.CleanRatioDelta;
		if (CellCarrierWeights.IsEmpty())
		{
			Result.bIsSurfaceFullyClean = FMD_ScrapeEvaluator::IsSurfaceFullyClean(
				InOutSurfaceCells, Settings.CleanTolerance);
		}

		// 제거 모드는 실제 제거, 접촉 전용 모드는 기존 오염 접촉과 맨 벽면 접촉으로 분류
		// 이 분류 자체는 감점을 발생시키지 않으며 다음 계층이 피드백/규칙을 선택
		if (!Result.bHadSurfaceContact)
		{
			Result.ContactType = EMD_ScrapeContactType::None;
		}
		else if (bHasContaminationContact && Result.bTouchedBareSurface)
		{
			Result.ContactType = EMD_ScrapeContactType::Mixed;
		}
		else if (bHasContaminationContact)
		{
			Result.ContactType = EMD_ScrapeContactType::ContaminationOnly;
		}
		else
		{
			Result.ContactType = EMD_ScrapeContactType::BareSurfaceOnly;
		}
	}

	FVector MeshUVWorldNormal(const FTransform& Transform, const FVector& LocalNormal)
	{
		return Transform.TransformVectorNoScale(LocalNormal / Transform.GetScale3D()).GetSafeNormal();
	}

	bool IsMeshUVContactValid(const UMD_ScrapeMeshUVData& Data,
		const FTransform& Transform, const FMD_ScrapeMeshUVStroke& Stroke)
	{
		const FVector Scale = Transform.GetScale3D();
		return FMath::IsFinite(Data.TargetCellSizeCm) && Data.TargetCellSizeCm > 0.0
			&& !Transform.ContainsNaN() && FMath::Abs(Scale.X) > UE_SMALL_NUMBER
			&& FMath::Abs(Scale.Y) > UE_SMALL_NUMBER && FMath::Abs(Scale.Z) > UE_SMALL_NUMBER
			&& !Stroke.StartWorldPosition.ContainsNaN() && !Stroke.EndWorldPosition.ContainsNaN()
			&& !Stroke.StartWorldNormal.ContainsNaN() && !Stroke.EndWorldNormal.ContainsNaN()
			&& !Stroke.StartWorldDepthAxis.ContainsNaN() && !Stroke.EndWorldDepthAxis.ContainsNaN()
			&& !Stroke.StartWorldNormal.IsNearlyZero() && !Stroke.EndWorldNormal.IsNearlyZero()
			&& Data.Triangles.IsValidIndex(Stroke.StartTriangleIndex)
			&& Data.Triangles.IsValidIndex(Stroke.EndTriangleIndex)
			&& Data.Triangles[Stroke.StartTriangleIndex].bCleanable
			&& Data.Triangles[Stroke.EndTriangleIndex].bCleanable;
	}

	bool FindDirectMeshUVSeamPath(const UMD_ScrapeMeshUVData& Data, const FTransform& Transform,
		const FMD_ScrapeMeshUVStroke& Stroke, FVector& OutCrossing,
		double& OutStartDistance, double& OutEndDistance)
	{
		if (Stroke.StartTriangleIndex == Stroke.EndTriangleIndex
			|| !Data.Triangles.IsValidIndex(Stroke.StartTriangleIndex))
		{
			return false;
		}
		const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[Stroke.StartTriangleIndex];
        bool bFound = false;
        double BestLength = TNumericLimits<double>::Max();
        const auto ConsiderPortal = [&](const FVector& LocalA, const FVector& LocalB)
        {
            const FVector A = Transform.TransformPosition(LocalA);
            const FVector B = Transform.TransformPosition(LocalB);
			const auto LengthThrough = [&](const double T)
			{
				const FVector Point = FMath::Lerp(A, B, T);
				return FVector::Distance(Stroke.StartWorldPosition, Point) + FVector::Distance(Point, Stroke.EndWorldPosition);
			};
			double Low = 0.0, High = 1.0;
			// The sum of distances along one edge is convex, including an endpoint minimum.
			for (int32 Iteration = 0; Iteration < 48; ++Iteration)
			{
				const double First = (Low * 2.0 + High) / 3.0;
				const double Second = (Low + High * 2.0) / 3.0;
				if (LengthThrough(First) < LengthThrough(Second)) { High = Second; }
				else { Low = First; }
			}
            const FVector Crossing = FMath::Lerp(A, B, (Low + High) * 0.5);
            const double StartDistance = FVector::Distance(Stroke.StartWorldPosition, Crossing);
            const double EndDistance = FVector::Distance(Crossing, Stroke.EndWorldPosition);
            if (StartDistance + EndDistance < BestLength)
            {
                BestLength = StartDistance + EndDistance; bFound = true;
                OutCrossing = Crossing; OutStartDistance = StartDistance; OutEndDistance = EndDistance;
            }
        };
        const TConstArrayView<FMD_ScrapeMeshUVNeighbor> Neighbors = Data.GetTriangleNeighbors(Stroke.StartTriangleIndex);
        if (!Neighbors.IsEmpty())
        {
            for (const FMD_ScrapeMeshUVNeighbor& Neighbor : Neighbors)
            {
                if (Neighbor.TriangleIndex == Stroke.EndTriangleIndex)
                { ConsiderPortal(Neighbor.SharedEdgeA, Neighbor.SharedEdgeB); }
            }
        }
        else
        {
            const FVector Vertices[] = {Triangle.A, Triangle.B, Triangle.C};
            for (int32 Edge = 0; Edge < 3; ++Edge)
            {
                if (Triangle.AdjacentTriangles[Edge] == Stroke.EndTriangleIndex)
                { ConsiderPortal(Vertices[Edge], Vertices[(Edge + 1) % 3]); }
            }
        }
        return bFound;
	}

	void GatherConnectedMeshUVTriangles(const UMD_ScrapeMeshUVData& Data,
		const FTransform& Transform, const int32 Seed, const FVector& Center,
		const FVector& ContactNormal, const double Radius, TArray<int32>& OutTriangles)
	{
		OutTriangles.Reset();
		TBitArray<> Visited(false, Data.Triangles.Num());
		TArray<int32, TInlineAllocator<64>> Pending;
		Pending.Add(Seed);
		for (int32 QueueIndex = 0; QueueIndex < Pending.Num(); ++QueueIndex)
		{
			const int32 Index = Pending[QueueIndex];
			if (!Data.Triangles.IsValidIndex(Index) || Visited[Index])
			{
				continue;
			}
			Visited[Index] = true;
			const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[Index];
			// Do not wrap around a thin wall or cross an excluded cap to reach another face.
			if (!Triangle.bCleanable
				|| FVector::DotProduct(MeshUVWorldNormal(Transform, Triangle.NormalLocal), ContactNormal) < 0.1)
			{
				continue;
			}
            FVector Closest;
            if (!FMD_ScrapeMeshUVGeometry::ClosestPointOnSourceTriangle(Triangle, Transform, Center, Closest)) { continue; }
			if (FVector::DistSquared(Closest, Center) > FMath::Square(Radius))
			{
				continue;
			}
			OutTriangles.Add(Index);
            const TConstArrayView<FMD_ScrapeMeshUVNeighbor> Neighbors = Data.GetTriangleNeighbors(Index);
            if (!Neighbors.IsEmpty())
            {
                for (const FMD_ScrapeMeshUVNeighbor& Neighbor : Neighbors)
                {
                    if (!Data.Triangles.IsValidIndex(Neighbor.TriangleIndex) || Visited[Neighbor.TriangleIndex]) { continue; }
                    const FVector A = Transform.TransformPosition(Neighbor.SharedEdgeA);
                    const FVector B = Transform.TransformPosition(Neighbor.SharedEdgeB);
                    const FVector Edge = B - A;
                    const double T = Edge.SizeSquared() > 0.0
                        ? FMath::Clamp(FVector::DotProduct(Center - A, Edge) / Edge.SizeSquared(), 0.0, 1.0) : 0.0;
                    // A nearby triangle is reachable only through a shared portion inside the brush neighborhood.
                    if (FVector::DistSquared(Center, A + Edge * T) <= FMath::Square(Radius))
                    { Pending.Add(Neighbor.TriangleIndex); }
                }
            }
            else
            {
                for (int32 Edge = 0; Edge < 3; ++Edge)
                {
                    const int32 Neighbor = Triangle.AdjacentTriangles[Edge];
                    if (Data.Triangles.IsValidIndex(Neighbor) && !Visited[Neighbor]) { Pending.Add(Neighbor); }
                }
            }
		}
	}

	double MeshUVPolygonArea(const TArray<FVector2D>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 I = 1; I + 1 < Polygon.Num(); ++I)
		{
			TwiceArea += FVector2D::CrossProduct(Polygon[I] - Polygon[0], Polygon[I + 1] - Polygon[0]);
		}
		return FMath::Abs(TwiceArea) * 0.5;
	}

	void ClipMeshUVBarycentricPolygon(TArray<FVector2D>& Polygon,
		const FVector2D& BarycentricCenter, const FVector2D& CellCenterOnBrush,
		const FVector2D& ProjectedEdgeB, const FVector2D& ProjectedEdgeC,
		const FVector2D& A, const FVector2D& B)
	{
		if (Polygon.IsEmpty())
		{
			return;
		}
		TArray<FVector2D> Clipped;
		Clipped.Reserve(Polygon.Num() + 1);
		const FVector2D Edge = B - A;
		const double CenterDistance = FVector2D::CrossProduct(Edge, CellCenterOnBrush - A);
		const FVector2D Coefficients(FVector2D::CrossProduct(Edge, ProjectedEdgeB),
			FVector2D::CrossProduct(Edge, ProjectedEdgeC));
		const auto SignedDistance = [&](const FVector2D& Point)
		{
			return CenterDistance + FVector2D::DotProduct(Coefficients, Point - BarycentricCenter);
		};
		FVector2D Previous = Polygon.Last();
		double PreviousDistance = SignedDistance(Previous);
		for (const FVector2D& Current : Polygon)
		{
			const double CurrentDistance = SignedDistance(Current);
			if ((CurrentDistance >= 0.0) != (PreviousDistance >= 0.0))
			{
				Clipped.Add(FMath::Lerp(Previous, Current, PreviousDistance / (PreviousDistance - CurrentDistance)));
			}
			if (CurrentDistance >= 0.0)
			{
				Clipped.Add(Current);
			}
			Previous = Current;
			PreviousDistance = CurrentDistance;
		}
		Polygon = MoveTemp(Clipped);
	}

	struct FMeshUVCellCoverage
	{
		int32 CellIndex = INDEX_NONE;
		double Fraction = 0.0;
	};

	bool GetMeshUVBrushCellBounds(const UMD_ScrapeMeshUVData& Data,
		const FTransform& Transform, const FMD_ScrapeMeshUVTriangle& Triangle,
		const FVector& WorldCenter, const double Radius, FIntPoint& OutMinimum, FIntPoint& OutMaximum)
	{
		const FVector2D UV0 = Triangle.UVB - Triangle.UVA, UV1 = Triangle.UVC - Triangle.UVA;
		const double Determinant = FVector2D::CrossProduct(UV0, UV1);
		if (!FMath::IsFinite(Determinant) || Determinant == 0.0)
		{
			return false;
		}
		const FVector E0 = Triangle.B - Triangle.A, E1 = Triangle.C - Triangle.A;
		const FVector CellU = (E0 * UV1.Y - E1 * UV0.Y) / (Determinant * Data.AtlasDimensions.X);
		const FVector CellV = (E1 * UV0.X - E0 * UV1.X) / (Determinant * Data.AtlasDimensions.Y);
		// Any clipped cell's centroid-to-corner support fits inside its full atlas-cell diagonal.
		// This bound includes every cell the existing center/support sphere test could retain.
		const double MaximumSupport = FMath::Max((CellU + CellV).Size(), (CellU - CellV).Size())
			* Transform.GetMaximumAxisScale();
		const double SearchRadius = Radius + MaximumSupport;
		const FVector LocalCenter = Transform.InverseTransformPosition(WorldCenter);
		const auto UVAt = [&](const FVector& Position, FVector2D& OutUV)
		{
			FVector Barycentric;
			if (!FMD_ScrapeMeshUVGeometry::TryCalculateBarycentric(Triangle, Position, Barycentric)) { return false; }
			OutUV = Triangle.UVA * Barycentric.X + Triangle.UVB * Barycentric.Y + Triangle.UVC * Barycentric.Z;
			return !OutUV.ContainsNaN();
		};
		FVector2D CenterUV, DX, DY, DZ;
		if (!UVAt(LocalCenter, CenterUV)
			|| !UVAt(LocalCenter + Transform.InverseTransformVector(FVector::XAxisVector), DX)
			|| !UVAt(LocalCenter + Transform.InverseTransformVector(FVector::YAxisVector), DY)
			|| !UVAt(LocalCenter + Transform.InverseTransformVector(FVector::ZAxisVector), DZ))
		{
			return false;
		}
		DX -= CenterUV; DY -= CenterUV; DZ -= CenterUV;
		const FVector2D Extent(FVector(DX.X, DY.X, DZ.X).Size() * SearchRadius,
			FVector(DX.Y, DY.Y, DZ.Y).Size() * SearchRadius);
		const FVector2D Minimum = CenterUV - Extent, Maximum = CenterUV + Extent;
		if (Minimum.ContainsNaN() || Maximum.ContainsNaN()
			|| Maximum.X < 0.0 || Maximum.Y < 0.0 || Minimum.X > 1.0 || Minimum.Y > 1.0)
		{
			return false;
		}
		OutMinimum = FIntPoint(
			FMath::Max(0, FMath::FloorToInt32(FMath::Clamp(Minimum.X, 0.0, 1.0) * Data.AtlasDimensions.X) - 1),
			FMath::Max(0, FMath::FloorToInt32(FMath::Clamp(Minimum.Y, 0.0, 1.0) * Data.AtlasDimensions.Y) - 1));
		OutMaximum = FIntPoint(
			FMath::Min(Data.AtlasDimensions.X - 1, FMath::FloorToInt32(FMath::Clamp(Maximum.X, 0.0, 1.0) * Data.AtlasDimensions.X) + 1),
			FMath::Min(Data.AtlasDimensions.Y - 1, FMath::FloorToInt32(FMath::Clamp(Maximum.Y, 0.0, 1.0) * Data.AtlasDimensions.Y) + 1));
		// A thin triangle can have a very large inverse-UV search radius. Only its
		// own chart can own candidate cells; retain the same one-pixel boundary margin.
		const FVector2D TriangleMinimum(FMath::Min3(Triangle.UVA.X, Triangle.UVB.X, Triangle.UVC.X),
			FMath::Min3(Triangle.UVA.Y, Triangle.UVB.Y, Triangle.UVC.Y));
		const FVector2D TriangleMaximum(FMath::Max3(Triangle.UVA.X, Triangle.UVB.X, Triangle.UVC.X),
			FMath::Max3(Triangle.UVA.Y, Triangle.UVB.Y, Triangle.UVC.Y));
		OutMinimum.X = FMath::Max(OutMinimum.X, FMath::FloorToInt32(TriangleMinimum.X * Data.AtlasDimensions.X) - 1);
		OutMinimum.Y = FMath::Max(OutMinimum.Y, FMath::FloorToInt32(TriangleMinimum.Y * Data.AtlasDimensions.Y) - 1);
		OutMaximum.X = FMath::Min(OutMaximum.X, FMath::FloorToInt32(TriangleMaximum.X * Data.AtlasDimensions.X) + 1);
		OutMaximum.Y = FMath::Min(OutMaximum.Y, FMath::FloorToInt32(TriangleMaximum.Y * Data.AtlasDimensions.Y) + 1);
		return OutMinimum.X <= OutMaximum.X && OutMinimum.Y <= OutMaximum.Y;
	}

	void CollectMeshUVFootprint(const UMD_ScrapeMeshUVData& Data,
		const FTransform& Transform, const FMD_ScrapeMeshUVSample& Sample,
		const FVector& WorldDepthAxis, const FMD_ScrapeToolSpec& Tool,
		TArray<FMeshUVCellCoverage>& OutCoverage, int64* OutVisitedCellCount = nullptr,
        FMD_ScrapeEvaluationScratch* BoundaryScratch = nullptr,
        const FMD_ScrapeMeshUVSample* SweepStart = nullptr, const FVector* SweepStartAxis = nullptr,
        const FMD_ScrapeMeshUVSample* SweepEnd = nullptr, const FVector* SweepEndAxis = nullptr)
	{
		OutCoverage.Reset();
		const FVector Center = Transform.TransformPosition(Sample.PositionLocal);
		const FVector Normal = MeshUVWorldNormal(Transform, Sample.NormalLocal);
		const FVector Depth = FVector::VectorPlaneProject(WorldDepthAxis, Normal).GetSafeNormal();
		const FVector Width = FVector::CrossProduct(Normal, Depth).GetSafeNormal();
		if (Depth.IsNearlyZero() || Width.IsNearlyZero())
		{
			return;
		}
		const double HalfWidth = Tool.BladeWidthCm * 0.5;
		const double HalfDepth = Tool.BladeDepthCm * 0.5;
		const double Radius = Tool.BrushShape == EMD_ScrapeBrushShape::Circle
			? HalfWidth : FMath::Sqrt(HalfWidth * HalfWidth + HalfDepth * HalfDepth);
		TArray<int32> Triangles;
		GatherConnectedMeshUVTriangles(Data, Transform, Sample.TriangleIndex, Center, Normal, Radius, Triangles);
		TArray<FVector2D> Blade;
		if (Tool.BrushShape == EMD_ScrapeBrushShape::Circle)
		{
			// An inscribed 64-gon keeps the error below 0.13% of the circle radius.
			for (int32 I = 0; I < 64; ++I)
			{
				const double Angle = 2.0 * UE_PI * I / 64.0;
				Blade.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * HalfWidth);
			}
		}
		else
		{
			Blade = { FVector2D(-HalfWidth, -HalfDepth), FVector2D(HalfWidth, -HalfDepth),
				FVector2D(HalfWidth, HalfDepth), FVector2D(-HalfWidth, HalfDepth) };
		}
		for (const int32 TriangleIndex : Triangles)
		{
			FIntPoint Minimum, Maximum;
			if (!GetMeshUVBrushCellBounds(Data, Transform, Data.Triangles[TriangleIndex], Center, Radius, Minimum, Maximum))
			{
				continue;
			}
			const int32 ContactsBefore = OutCoverage.Num();
			for (int32 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
			{
				for (int32 X = Minimum.X; X <= Maximum.X; ++X)
				{
					const int32 CellIndex = X + Y * Data.AtlasDimensions.X;
					if (OutVisitedCellCount) { ++*OutVisitedCellCount; }
					if (!Data.IsActiveCell(CellIndex) || Data.Cells[CellIndex].TriangleIndex != TriangleIndex)
					{
						continue;
					}
					const FMD_ScrapeMeshUVCell& Cell = Data.Cells[CellIndex];
					const FVector Offset = Transform.TransformVector(Cell.PositionLocal - Sample.PositionLocal);
					if (Offset.Size() > Radius + Cell.SupportRadiusCm * Transform.GetMaximumAxisScale())
					{
						continue;
					}
					TArray<FVector> BarycentricPolygon;
					if (!FMD_ScrapeMeshUVGeometry::GetCellPolygonBarycentric(Data, CellIndex, BarycentricPolygon))
					{
						continue;
					}
					TArray<FVector2D> Polygon;
					for (const FVector& Point : BarycentricPolygon) Polygon.Add(FVector2D(Point.Y, Point.Z));
					const FMD_ScrapeMeshUVTriangle& SourceTriangle = Data.Triangles[TriangleIndex];
					const FVector EdgeB = Transform.TransformVector(SourceTriangle.B - SourceTriangle.A);
					const FVector EdgeC = Transform.TransformVector(SourceTriangle.C - SourceTriangle.A);
					const FVector2D ProjectedEdgeB(FVector::DotProduct(EdgeB, Width), FVector::DotProduct(EdgeB, Depth));
					const FVector2D ProjectedEdgeC(FVector::DotProduct(EdgeC, Width), FVector::DotProduct(EdgeC, Depth));
					const FVector2D BarycentricCenter(Cell.Barycentric.Y, Cell.Barycentric.Z);
					const FVector2D CellCenterOnBrush(FVector::DotProduct(Offset, Width), FVector::DotProduct(Offset, Depth));
					// Keep the polygon in source simplex space. The cm-space blade boundary
					// remains an affine inequality here, so its area ratio is unchanged while
					// tiny positive cells cannot collapse through world-coordinate subtraction.
					const double FullArea = MeshUVPolygonArea(Polygon);
					// Every positive baked cell must remain cleanable, including float-UV boundary slivers.
					if (!FMath::IsFinite(FullArea) || FullArea <= 0.0)
					{
						continue;
					}
					for (int32 I = 0; I < Blade.Num() && !Polygon.IsEmpty(); ++I)
					{
						ClipMeshUVBarycentricPolygon(Polygon, BarycentricCenter, CellCenterOnBrush,
							ProjectedEdgeB, ProjectedEdgeC, Blade[I], Blade[(I + 1) % Blade.Num()]);
					}
					const double Fraction = FMath::Clamp(MeshUVPolygonArea(Polygon) / FullArea, 0.0, 1.0);
					if (Fraction > UE_SMALL_NUMBER)
					{
						OutCoverage.Add({ CellIndex, Fraction });
					}
				}
			}

            if (BoundaryScratch && BoundaryScratch->bCaptureBoundary && CVarCaptureBoundary.GetValueOnGameThread()!=0 && OutCoverage.Num()>ContactsBefore)
            {
                // The exact affine blade half-planes used above, inverted into the
                // owning private chart. Continuous polygons, never cell thresholds.
                if (BoundaryScratch->BoundaryFootprints.Num()>=512)
                { BoundaryScratch->bBoundaryOverflow=true; continue; }
                const auto& T=Data.Triangles[TriangleIndex];
                auto& Footprint=BoundaryScratch->BoundaryFootprints.AddDefaulted_GetRef();
                Footprint.TriangleIndex=TriangleIndex;
                const FVector B=Transform.TransformVector(T.B-T.A);
                const FVector C=Transform.TransformVector(T.C-T.A);
                const auto AddBlade=[&](const FMD_ScrapeMeshUVSample& At,const FVector& Axis)
                {
                    const FVector N=MeshUVWorldNormal(Transform,At.NormalLocal);
                    const FVector D=FVector::VectorPlaneProject(Axis,N).GetSafeNormal();
                    const FVector W=FVector::CrossProduct(N,D).GetSafeNormal();
                    const FVector A=Transform.TransformVector(T.A-At.PositionLocal);
                    const FVector2D PA(FVector::DotProduct(A,W),FVector::DotProduct(A,D));
                    const FVector2D PB(FVector::DotProduct(B,W),FVector::DotProduct(B,D));
                    const FVector2D PC(FVector::DotProduct(C,W),FVector::DotProduct(C,D));
                    const double Det=PB.X*PC.Y-PB.Y*PC.X;
                    if (FMath::Abs(Det)<=UE_SMALL_NUMBER) return;
                    for (const FVector2D& Point : Blade)
                    {
                        const FVector2D Q=Point-PA;
                        const double Beta=(Q.X*PC.Y-Q.Y*PC.X)/Det;
                        const double Gamma=(PB.X*Q.Y-PB.Y*Q.X)/Det;
                        const FVector2D UV=T.UVA+Beta*(T.UVB-T.UVA)+Gamma*(T.UVC-T.UVA);
                        Footprint.Points.Add(UV*FVector2D(Data.AtlasDimensions));
                    }
                };
                AddBlade(Sample,WorldDepthAxis);
                // Connect the validated endpoints on a common physical plane.
                // Translation is exact; bounded angular subdivision limits the
                // convex sweep's rotation error to bladeRadius*(1-cos(theta/2)).
                // At a fold keep the physical midpoint footprint of each face.
                if (SweepStart && SweepEnd && SweepStartAxis && SweepEndAxis
                    && FVector::DotProduct(MeshUVWorldNormal(Transform,SweepStart->NormalLocal),Normal)>0.999999
                    && FVector::DotProduct(MeshUVWorldNormal(Transform,SweepEnd->NormalLocal),Normal)>0.999999)
                { AddBlade(*SweepStart,*SweepStartAxis); AddBlade(*SweepEnd,*SweepEndAxis); }
                UE::Geometry::FConvexHull2d Hull;
                if (Hull.Solve(MakeArrayView(Footprint.Points)))
                {
                    TArray<FVector2D> Points;
                    for (int32 Index : Hull.GetPolygonIndices()) Points.Add(Footprint.Points[Index]);
                    Footprint.Points=MoveTemp(Points);
                }
                const int32 Columns=FMath::DivideAndRoundUp(Data.AtlasDimensions.X,8);
                const int32 Rows=FMath::DivideAndRoundUp(Data.AtlasDimensions.Y,8);
                TSet<int32> Tiles;
                for (int32 Contact=ContactsBefore;Contact<OutCoverage.Num();++Contact)
                {
                    const int32 Address=OutCoverage[Contact].CellIndex;
                    const int32 X=Address%Data.AtlasDimensions.X,Y=Address/Data.AtlasDimensions.X;
                    for (int32 TY=FMath::Max(0,(Y-1)/8);TY<=FMath::Min(Rows-1,(Y+1)/8);++TY)
                        for (int32 TX=FMath::Max(0,(X-1)/8);TX<=FMath::Min(Columns-1,(X+1)/8);++TX)
                            Tiles.Add(TX+TY*Columns);
                }
                Footprint.Tiles=Tiles.Array(); Footprint.Tiles.Sort();
            }
		}
	}

}

double FMD_ScrapeEvaluator::CalculateStrokeSampleSpacingCm(
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm)
{
	if (!ToolSpec.IsValid()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Settings.SampleSpacingRatioOfSmallestCell)
		|| Settings.SampleSpacingRatioOfSmallestCell <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Settings.MaximumSampleSpacingCm)
		|| Settings.MaximumSampleSpacingCm <= UE_SMALL_NUMBER)
	{
		return 0.0;
	}

	const FVector2D CellSizeCm(
		SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),
		SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));
	const double RatioBasedSpacingCm = FMath::Min(CellSizeCm.X, CellSizeCm.Y)
		* static_cast<double>(Settings.SampleSpacingRatioOfSmallestCell);
	const double BrushTravelSizeCm =
		ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle
		? static_cast<double>(ToolSpec.BladeWidthCm)
		: static_cast<double>(FMath::Min(
			ToolSpec.BladeWidthCm,
			ToolSpec.BladeDepthCm));
	const double BrushBasedSpacingCm = BrushTravelSizeCm * 0.5;
	return FMath::Min3(
		RatioBasedSpacingCm,
		static_cast<double>(Settings.MaximumSampleSpacingCm),
		BrushBasedSpacingCm);
}

FMD_ScrapeResult FMD_ScrapeEvaluator::ApplyStrokeToGrid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	FMD_ScrapeCellView InOutSurfaceCells)
{
	return ApplyStrokeToGrid(
		Stroke,
		Settings.GetToolSpec(),
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells);
}

FMD_ScrapeResult FMD_ScrapeEvaluator::ApplyStrokeToGrid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	FMD_ScrapeCellView InOutSurfaceCells)
{
	return ApplyStrokeToGridInternal(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		nullptr,
		nullptr);
}

FMD_ScrapeResult FMD_ScrapeEvaluator::ApplyStrokeToGrid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	FMD_ScrapeCellView InOutSurfaceCells,
	FMD_ScrapeEvaluationScratch& Scratch)
{
	return ApplyStrokeToGridInternal(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		nullptr,
		&Scratch);
}

FMD_ScrapeResult
FMD_ScrapeEvaluator::ApplyStrokeToGridAndCaptureRequestedAmounts(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	FMD_ScrapeCellView InOutSurfaceCells,
	TArray<float>& OutRequestedRemovalAmounts,
	const bool bApplyRemoval)
{
	return ApplyStrokeToGridInternal(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		&OutRequestedRemovalAmounts,
		nullptr,
		bApplyRemoval);
}

FMD_ScrapeResult
FMD_ScrapeEvaluator::ApplyStrokeToGridAndCaptureRequestedAmounts(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	FMD_ScrapeCellView InOutSurfaceCells,
	TArray<float>& OutRequestedRemovalAmounts,
	FMD_ScrapeEvaluationScratch& Scratch,
	const bool bApplyRemoval)
{
	return ApplyStrokeToGridInternal(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		&OutRequestedRemovalAmounts,
		&Scratch,
		bApplyRemoval);
}

FMD_ScrapeResult FMD_ScrapeEvaluator::ApplyStrokeToGridInternal(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	FMD_ScrapeCellView InOutSurfaceCells,
	TArray<float>* const OutRequestedRemovalAmounts,
	FMD_ScrapeEvaluationScratch* const Scratch,
	const bool bApplyRemoval)
{
	FMD_ScrapeResult Result;
	Result.SurfaceFace = Stroke.SurfaceFace;
	if (OutRequestedRemovalAmounts)
	{
		OutRequestedRemovalAmounts->Reset();
	}

	// 실제 배열을 수정하기 전에 모든 입력을 먼저 검사
	// 실패 이유는 결과 구조체에 담지 않고 로그로만 남김
	// 게임 규칙 분기는 bIsInputValid로 판단하며, 문자열은 개발 중 원인 추적에만 사용
	FString ValidationErrorMessage;
	if (!IsEvaluationInputValid(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		&ValidationErrorMessage))
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 긁기 판정 입력이 올바르지 않음: %s"), *UE_CALL_LOCATIONINFO, *ValidationErrorMessage);

		return Result;
	}

	Result.bIsInputValid = true;

	if (bApplyRemoval)
	{
		// 모든 셀을 0~InitialContamination 범위로 정규화 수행
		for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
		{
			FMD_ScrapeSurfaceCell Cell = InOutSurfaceCells.Read(CellIndex);
			Cell.Normalize();

			// 완료 허용 오차 안에 들어온 극소 잔여량은 판정 시작 시점에도 0으로 수정
			if (Cell.RemainingContamination <= Settings.CleanTolerance)
			{
				Cell.RemainingContamination = 0.0f;
			}
			if (Cell.TransferredContamination <= Settings.CleanTolerance)
			{
				Cell.TransferredContamination = 0.0f;
				Cell.TransferredPaintPatternDensity = 0.0f;
			}
			InOutSurfaceCells.Set(CellIndex, Cell);
		}
	}

	Result.CleanRatioBefore = CalculateCleanRatio(InOutSurfaceCells);
	Result.CleanRatioAfter = Result.CleanRatioBefore;
	Result.bIsSurfaceFullyClean = IsSurfaceFullyClean(InOutSurfaceCells, Settings.CleanTolerance);

	const FVector2D StrokeDelta = Stroke.EndPositionCm - Stroke.StartPositionCm;
	const double StrokeChordDistanceCm = StrokeDelta.Size();
	FResolvedStrokeBladeBasis StrokeBladeBasis;
	const FVector2D LegacyDepthAxis = StrokeChordDistanceCm > UE_SMALL_NUMBER
		? StrokeDelta / StrokeChordDistanceCm
		: FVector2D::ZeroVector;
	if (!TryResolveStrokeBladeBasis(
			Stroke,
			LegacyDepthAxis,
			StrokeBladeBasis))
	{
		return Result;
	}
	// TravelDistanceCm is a replay-only value. Legacy callers that do not provide
	// an explicit blade basis keep the original chord-distance contract.
	const bool bHasExplicitTravelDistance =
		StrokeBladeBasis.bUsesExplicitBasis && Stroke.TravelDistanceCm > 0.0f;
	const double ResolvedTravelDistanceCm = bHasExplicitTravelDistance
		? static_cast<double>(Stroke.TravelDistanceCm)
		: StrokeChordDistanceCm;

	// 이동 거리가 너무 짧으면 버튼을 누른 채 정지해 있는 입력이나 미세한 손 떨림으로 가정
	// 정상적인 플레이에서도 매 프레임 발생하는 상황이므로 로그를 남기지 않음
	const bool bCanReplayShortExplicitSegment = bHasExplicitTravelDistance;
	if (ResolvedTravelDistanceCm <= static_cast<double>(UE_SMALL_NUMBER)
		|| (!bCanReplayShortExplicitSegment
			&& ResolvedTravelDistanceCm
				< static_cast<double>(Settings.MinimumTravelDistanceCm)))
	{
		return Result;
	}

	Result.bWasStrokeLongEnough = true;
	Result.EvaluatedTravelDistanceCm = ResolvedTravelDistanceCm;
	if (OutRequestedRemovalAmounts)
	{
		OutRequestedRemovalAmounts->SetNumZeroed(InOutSurfaceCells.Num());
	}

	const FVector2D CellSizeCm(SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));

	// 표면의 로컬 원점인 중앙을 맞추기 위해, 전체 크기의 절반이 음수로 이동한 위치가 좌하단 좌표
	const FVector2D SurfaceMinimumCm = SurfaceSizeCm * -0.5;

	const double SampleSpacingCm = CalculateStrokeSampleSpacingCm(
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm);
	// 이동 거리를 샘플 최대 간격으로 나눈 뒤 올림하면 실제 간격은 항상 설정값 이하로 유지
	// 얇은 사각 헤라도 이동 방향으로 빈틈이 생기지 않도록 브러시 길이의 절반을 추가 상한으로 사용
	const int32 TravelSampleCount = FMath::Max(
		1,
		FMath::CeilToInt(ResolvedTravelDistanceCm / SampleSpacingCm));
	const double BladeRotationDegrees = FMath::Abs(
		FMath::RadiansToDegrees(StrokeBladeBasis.SignedRotationRadians));
	const int32 RotationSampleCount = FMath::Max(
		1,
		FMath::CeilToInt(
			BladeRotationDegrees
			/ static_cast<double>(
				Settings.MaximumBladeRotationDegreesPerSample)));
	const int32 SampleCount = FMath::Max(
		TravelSampleCount,
		RotationSampleCount);
	const double TravelDistancePerSampleCm =
		ResolvedTravelDistanceCm / static_cast<double>(SampleCount);

	const double CellHalfDiagonalCm = 0.5 * FMath::Sqrt(FMath::Square(CellSizeCm.X) + FMath::Square(CellSizeCm.Y));
	const double BladeHalfWidthCm =
		static_cast<double>(ToolSpec.BladeWidthCm) * 0.5;
	const double BladeHalfDepthCm =
		static_cast<double>(ToolSpec.BladeDepthCm) * 0.5;

	// 셀 중심이 브러시 중심과 정확히 겹쳤을 때의 피복 가중치
	// 부분 접촉 하한을 이 값으로 정규화하므로, 셀보다 얇은 헤라도
	// 셀 정중앙을 지나가면 그 셀을 끝까지 벗길 수 있다
	const double FullContactCoverageWeight =
		ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle
		? CalculateCircleCellCoverageWeight(
			0.0,
			BladeHalfWidthCm,
			CellHalfDiagonalCm)
		: CalculateRectangleCellCoverageWeight(
			FVector2D::ZeroVector,
			StrokeBladeBasis.Start.WidthAxis,
			StrokeBladeBasis.Start.DepthAxis,
			BladeHalfWidthCm,
			BladeHalfDepthCm,
			CellHalfDiagonalCm);

	// 후보 셀 검색 범위를 브러시 형태의 표면 X/Y AABB로 계산

	// 동일 셀이 여러 샘플과 겹쳐도 TouchedCellCount는 한 번만 증가해야 하므로 비트 배열을 사용
	FMD_ScrapeEvaluationScratch LocalScratch;
	FMD_ScrapeEvaluationScratch& EvaluationScratch = Scratch
		? *Scratch
		: LocalScratch;
	EvaluationScratch.MeshUVTouchedCells.Empty();
	EvaluationScratch.MeshUVTouchedCellSlots.Empty();
	TBitArray<>& TouchedCells = EvaluationScratch.TouchedCells;
	TouchedCells.Init(false, InOutSurfaceCells.Num());

	// 변경 내역과 "이번 스트로크로 처음 깨끗해졌는가"를 마지막에 정확히 계산하기 위해
	// 적용 전 잔여량을 별도 배열에 보관
	TArray<float>& RemainingBeforeStroke =
		EvaluationScratch.RemainingBeforeStroke;
	RemainingBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());
	TArray<float>& TransferredBeforeStroke =
		EvaluationScratch.TransferredBeforeStroke;
	TransferredBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());
	// 시각 마스크는 잔여량과 피복률을 면적으로 가중합하므로, 잔여량이 그대로여도
	// 피복률만 늘면 겉모습이 달라진다. 그런 셀도 변경 내역에 담아야 마스크가
	// 갱신된다. 피복률은 단조 증가라 1에 포화되면 더는 변경을 만들지 않는다.
	TArray<float>& CoveredFractionBeforeStroke =
		EvaluationScratch.CoveredFractionBeforeStroke;
	CoveredFractionBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		RemainingBeforeStroke[CellIndex] = InOutSurfaceCells.Read(CellIndex).RemainingContamination;
		TransferredBeforeStroke[CellIndex] = InOutSurfaceCells.Read(CellIndex).TransferredContamination;
		CoveredFractionBeforeStroke[CellIndex] =
			InOutSurfaceCells.Read(CellIndex).CoveredFraction;
	}

	bool bHasContaminationContact = false;

	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		// 각 구간의 시작점이 아니라 중앙을 샘플링
		// 중앙점에 해당 구간 이동량을 배정하면 프레임이 달라도 전체 제거 작업량이 일정
		const double Alpha =
			(static_cast<double>(SampleIndex) + 0.5)
			/ static_cast<double>(SampleCount);
		const FVector2D SamplePositionCm =
			FMath::Lerp(Stroke.StartPositionCm, Stroke.EndPositionCm, Alpha);
		const FResolvedBladeBasis SampleBladeBasis = InterpolateBladeBasis(
			StrokeBladeBasis,
			Alpha);
		const FVector2D& BladeDepthAxis = SampleBladeBasis.DepthAxis;
		const FVector2D& BladeWidthAxis = SampleBladeBasis.WidthAxis;

		FVector2D MaximumInfluenceExtentCm;
		if (ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle)
		{
			const double InfluenceRadiusCm =
				BladeHalfWidthCm + CellHalfDiagonalCm;
			MaximumInfluenceExtentCm = FVector2D(
				InfluenceRadiusCm,
				InfluenceRadiusCm);
		}
		else
		{
			MaximumInfluenceExtentCm = FVector2D(
				FMath::Abs(BladeWidthAxis.X) * BladeHalfWidthCm
					+ FMath::Abs(BladeDepthAxis.X) * BladeHalfDepthCm
					+ CellHalfDiagonalCm,
				FMath::Abs(BladeWidthAxis.Y) * BladeHalfWidthCm
					+ FMath::Abs(BladeDepthAxis.Y) * BladeHalfDepthCm
					+ CellHalfDiagonalCm);
		}

		// 전체 격자를 매 샘플마다 훑지 않고, 헤라 영향 범위 근처의 셀 인덱스만 계산
		// FloorToInt를 사용하므로 표면 최소 좌표가 정확히 0번 셀의 시작점에 대응
		const int32 UnclampedMinimumX = FMath::FloorToInt(
			(SamplePositionCm.X - MaximumInfluenceExtentCm.X - SurfaceMinimumCm.X)
			/ CellSizeCm.X);
		const int32 UnclampedMaximumX = FMath::FloorToInt(
			(SamplePositionCm.X + MaximumInfluenceExtentCm.X - SurfaceMinimumCm.X)
			/ CellSizeCm.X);
		const int32 UnclampedMinimumY = FMath::FloorToInt(
			(SamplePositionCm.Y - MaximumInfluenceExtentCm.Y - SurfaceMinimumCm.Y)
			/ CellSizeCm.Y);
		const int32 UnclampedMaximumY = FMath::FloorToInt(
			(SamplePositionCm.Y + MaximumInfluenceExtentCm.Y - SurfaceMinimumCm.Y)
			/ CellSizeCm.Y);

		// 후보 범위가 표면과 전혀 겹치지 않으면 Clamp로 가장자리 셀을 억지로 검사하지 않고 건너뜀
		if (UnclampedMaximumX < 0
			|| UnclampedMinimumX >= GridDimensions.X
			|| UnclampedMaximumY < 0
			|| UnclampedMinimumY >= GridDimensions.Y)
		{
			continue;
		}

		const int32 MinimumX = FMath::Clamp(
			UnclampedMinimumX,
			0,
			GridDimensions.X - 1);
		const int32 MaximumX = FMath::Clamp(
			UnclampedMaximumX,
			0,
			GridDimensions.X - 1);
		const int32 MinimumY = FMath::Clamp(
			UnclampedMinimumY,
			0,
			GridDimensions.Y - 1);
		const int32 MaximumY = FMath::Clamp(
			UnclampedMaximumY,
			0,
			GridDimensions.Y - 1);

		const auto VisitFootprint = [&](auto&& Visit)
		{
			for (int32 CellY = MinimumY; CellY <= MaximumY; ++CellY)
			{
				for (int32 CellX = MinimumX; CellX <= MaximumX; ++CellX)
				{
					const int32 CellIndex = CellX + (CellY * GridDimensions.X);

					// 셀 중심은 셀 시작점에서 가로·세로 크기의 절반만큼 이동한 위치
					const FVector2D CellCenterCm(
						SurfaceMinimumCm.X
							+ (static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
						SurfaceMinimumCm.Y
							+ (static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);

					const FVector2D BrushToCellCenterCm =
						CellCenterCm - SamplePositionCm;
					double CoverageWeight = 0.0;
					if (ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle)
					{
						CoverageWeight = CalculateCircleCellCoverageWeight(
							BrushToCellCenterCm.Size(),
							BladeHalfWidthCm,
							CellHalfDiagonalCm);
					}
					else
					{
						CoverageWeight = CalculateRectangleCellCoverageWeight(
							BrushToCellCenterCm,
							BladeWidthAxis,
							BladeDepthAxis,
							BladeHalfWidthCm,
							BladeHalfDepthCm,
							CellHalfDiagonalCm);
					}

					// 피복 가중치가 사실상 0인 후보는 실제 접촉으로 미집계
					if (CoverageWeight <= UE_SMALL_NUMBER)
					{
						continue;
					}

					Visit(CellIndex, BrushToCellCenterCm, CoverageWeight);
				}
			}
		};
		const auto ApplyCoverage = [&](const int32 CellIndex, const double CoverageWeight)
		{
			Result.bHadSurfaceContact = true;
			TouchedCells[CellIndex] = true;

			FMD_ScrapeSurfaceCell Cell = InOutSurfaceCells.Read(CellIndex);

			// 셀 중심만 보고 딱 잘라 판정하면 격자 모양 경계가 생기므로
			// 아래 하한 계산과 같은 정규화 피복률을 시각용으로 누적한다.
			// 이미 깨끗한 셀도 갱신해야 한다. 그래야 나중에 오염이 다시
			// 쌓였을 때 "얼마나 덮였는지"가 이 스트로크까지 반영된다.
			ApplyScrapeCoverageSample(Cell, CoverageWeight, FullContactCoverageWeight,
				TravelDistancePerSampleCm, ToolSpec, Settings, Result,
				bHasContaminationContact, OutRequestedRemovalAmounts
					? &(*OutRequestedRemovalAmounts)[CellIndex] : nullptr, bApplyRemoval);
            if (bApplyRemoval) { InOutSurfaceCells.Set(CellIndex, Cell); }
		};
		VisitFootprint([&](const int32 CellIndex, const FVector2D&, const double CoverageWeight)
		{
			ApplyCoverage(CellIndex, CoverageWeight);
		});
	}

	FinalizeScrapeEvaluation(GridDimensions, Settings, InOutSurfaceCells,
		EvaluationScratch, bHasContaminationContact, Result);

	return Result;
}


bool FMD_ScrapeEvaluator::TryInterpolateMeshUVStroke(
	const UMD_ScrapeMeshUVData& Data, const FTransform& MeshToWorld,
	const FMD_ScrapeMeshUVStroke& Stroke, const double Alpha,
	FMD_ScrapeMeshUVSample& OutSample, FVector& OutWorldDepthAxis)
{
	OutSample = FMD_ScrapeMeshUVSample();
	OutWorldDepthAxis = FVector::ZeroVector;
	if (!IsMeshUVContactValid(Data, MeshToWorld, Stroke) || !FMath::IsFinite(Alpha)
		|| Alpha < 0.0 || Alpha > 1.0)
	{
		return false;
	}
	// A common endpoint hemisphere lets a contact move around a corner without admitting a reverse face.
	if (FVector::DotProduct(Stroke.StartWorldNormal.GetSafeNormal(), Stroke.EndWorldNormal.GetSafeNormal()) < -0.5)
	{
		return false;
	}
	FVector Desired = FMath::Lerp(Stroke.StartWorldPosition, Stroke.EndWorldPosition, Alpha);
	FVector Crossing;
	double StartDistance = 0.0, EndDistance = 0.0;
	if (FindDirectMeshUVSeamPath(Data, MeshToWorld, Stroke, Crossing, StartDistance, EndDistance))
	{
		const double Arc = Alpha * (StartDistance + EndDistance);
		Desired = Arc <= StartDistance && StartDistance > UE_SMALL_NUMBER
			? FMath::Lerp(Stroke.StartWorldPosition, Crossing, Arc / StartDistance)
			: FMath::Lerp(Crossing, Stroke.EndWorldPosition,
				EndDistance > UE_SMALL_NUMBER ? FMath::Clamp((Arc - StartDistance) / EndDistance, 0.0, 1.0) : 1.0);
	}
	const FVector Normal = (Stroke.StartWorldNormal.GetSafeNormal() + Stroke.EndWorldNormal.GetSafeNormal()).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return false;
	}
	const double Chord = FVector::Distance(Stroke.StartWorldPosition, Stroke.EndWorldPosition);
	const double CellSize = Data.TargetCellSizeCm * MeshToWorld.GetMaximumAxisScale();
	TArray<int32> Candidates;
	GatherConnectedMeshUVTriangles(Data, MeshToWorld, Stroke.StartTriangleIndex,
		Desired, Normal, Chord + CellSize * 2.0, Candidates);
	if (Alpha >= 1.0 && !Candidates.Contains(Stroke.EndTriangleIndex))
	{
		return false;
	}
	double BestDistanceSq = TNumericLimits<double>::Max();
	FVector BestWorldPoint = FVector::ZeroVector;
	for (const int32 Index : Candidates)
	{
		const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[Index];
        FVector Point;
        if (!FMD_ScrapeMeshUVGeometry::ClosestPointOnSourceTriangle(Triangle, MeshToWorld, Desired, Point)) { continue; }
		const double DistanceSq = FVector::DistSquared(Point, Desired);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestWorldPoint = Point;
			OutSample.TriangleIndex = Index;
		}
	}
	if (OutSample.TriangleIndex == INDEX_NONE
		|| BestDistanceSq > FMath::Square(FMath::Max(CellSize * 2.0, Chord * 0.1)))
	{
		return false;
	}
	const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[OutSample.TriangleIndex];
	OutSample.PositionLocal = MeshToWorld.InverseTransformPosition(BestWorldPoint);
	OutSample.NormalLocal = Triangle.NormalLocal;
	OutSample.DistanceCm = FMath::Sqrt(BestDistanceSq);
	if (!FMD_ScrapeMeshUVGeometry::TryCalculateBarycentric(Triangle,
		OutSample.PositionLocal, OutSample.Barycentric))
	{
		return false;
	}
	OutSample.CanonicalUV = Triangle.UVA * OutSample.Barycentric.X
		+ Triangle.UVB * OutSample.Barycentric.Y + Triangle.UVC * OutSample.Barycentric.Z;
	FVector StartAxis = Stroke.StartWorldDepthAxis;
	FVector EndAxis = Stroke.EndWorldDepthAxis;
	if (StartAxis.IsNearlyZero())
	{
		StartAxis = EndAxis.IsNearlyZero() ? Stroke.EndWorldPosition - Stroke.StartWorldPosition : EndAxis;
	}
	if (EndAxis.IsNearlyZero())
	{
		EndAxis = StartAxis;
	}
	if (FVector::DotProduct(StartAxis, EndAxis) < 0.0)
	{
		EndAxis *= -1.0;
	}
	const FVector WorldNormal = MeshUVWorldNormal(MeshToWorld, Triangle.NormalLocal);
	OutWorldDepthAxis = FVector::VectorPlaneProject(FMath::Lerp(StartAxis, EndAxis, Alpha), WorldNormal).GetSafeNormal();
	if (OutWorldDepthAxis.IsNearlyZero())
	{
		OutWorldDepthAxis = FVector::VectorPlaneProject(
			MeshToWorld.TransformVector(Triangle.B - Triangle.A), WorldNormal).GetSafeNormal();
	}
	return !OutWorldDepthAxis.IsNearlyZero();
}

FMD_ScrapeResult FMD_ScrapeEvaluator::ApplyStrokeToMeshUVGrid(
	const UMD_ScrapeMeshUVData& Data, const FTransform& MeshToWorld,
	const FMD_ScrapeMeshUVStroke& Stroke, const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings, const FMD_ScrapeCellView InOutSurfaceCells,
	TArray<float>* OutRequestedRemovalAmounts, FMD_ScrapeEvaluationScratch* Scratch,
	const FMD_ScrapeCarrierWeightView CachedCellCarrierWeights,
	const FMD_ScrapeMeshUVProgress* CachedProgressBefore,
	FMD_ScrapeMeshUVProgress* OutProgressAfter,
	const bool bCaptureTouchedRequestedAmounts,
	const bool bApplyRemoval)
{
	FMD_ScrapeMeshUVProgress ProgressBefore = CachedProgressBefore
		? *CachedProgressBefore : FMD_ScrapeMeshUVProgress();
	if (OutProgressAfter) { *OutProgressAfter = FMD_ScrapeMeshUVProgress(); }
	FMD_ScrapeEvaluationScratch LocalScratch;
	FMD_ScrapeEvaluationScratch& State = Scratch ? *Scratch : LocalScratch;
	// The lookup grows with this evaluation's contacts, never the whole atlas.
	State.MeshUVTouchedCellSlots.Empty();
	TMap<int32, int32> TouchedSlots;
	State.MeshUVTouchedCells.Reset();
    State.BoundaryFootprints.Reset(); State.bBoundaryOverflow=false;
	State.MeshUVVisitedCellCount = 0;
	State.TouchedCells.Empty();
	State.RemainingBeforeStroke.Empty();
	State.TransferredBeforeStroke.Empty();
	State.CoveredFractionBeforeStroke.Empty();
	FMD_ScrapeResult Result;
	Result.SurfaceFace = Stroke.SurfaceFace;
	if (OutRequestedRemovalAmounts)
	{
		OutRequestedRemovalAmounts->Reset();
	}
	FMD_ScrapeStroke ValidationStroke;
	ValidationStroke.SurfaceFace = Stroke.SurfaceFace;
	ValidationStroke.TravelDistanceCm = Stroke.TravelDistanceCm;
	if (!IsMeshUVContactValid(Data, MeshToWorld, Stroke)
		|| InOutSurfaceCells.Num() != Data.Cells.Num()
		|| !IsEvaluationInputValid(ValidationStroke, ToolSpec, Settings, Data.AtlasDimensions,
			FVector2D(1.0, 1.0), InOutSurfaceCells))
	{
		return Result;
	}
	TSharedPtr<const FMD_ScrapeCarrierWeights> BuiltCellCarrierWeights;
	FMD_ScrapeCarrierWeightView CellCarrierWeights = CachedCellCarrierWeights;
	if (CellCarrierWeights.IsEmpty())
	{
		BuiltCellCarrierWeights = Data.GetSharedCellCarrierWeights(MeshToWorld);
		if (!BuiltCellCarrierWeights)
		{
			return Result;
		}
		CellCarrierWeights = BuiltCellCarrierWeights->GetView();
	}
	if (CellCarrierWeights.Num() != InOutSurfaceCells.Num())
	{
		return Result;
	}
	const double Chord = FVector::Distance(Stroke.StartWorldPosition, Stroke.EndWorldPosition);
	FVector Crossing;
	double StartDistance = 0.0, EndDistance = 0.0;
	const bool bHasDirectSeam = FindDirectMeshUVSeamPath(Data, MeshToWorld, Stroke, Crossing, StartDistance, EndDistance);
	const double PathLength = bHasDirectSeam ? StartDistance + EndDistance : Chord;
	const double MinWorldScale = MeshToWorld.GetScale3D().GetAbsMin();
	const double SampleSpacing = FMath::Min3(Data.TargetCellSizeCm * MinWorldScale
		* Settings.SampleSpacingRatioOfSmallestCell, static_cast<double>(Settings.MaximumSampleSpacingCm),
		0.5 * FMath::Min(ToolSpec.BladeWidthCm, ToolSpec.BladeDepthCm));
	if (!FMath::IsFinite(SampleSpacing) || SampleSpacing <= UE_SMALL_NUMBER
		|| PathLength / SampleSpacing > 4096.0)
	{
		return Result;
	}
	const bool bCanIncrementProgress = HasDetailedMeshUVProgress(ProgressBefore, InOutSurfaceCells.Num())
		&& (Settings.CleanTolerance == 0.0f || ProgressBefore.CleanTolerance == Settings.CleanTolerance);
	if (!bCanIncrementProgress)
	{
		if (!CalculateMeshUVProgress(InOutSurfaceCells, CellCarrierWeights, Settings.CleanTolerance, ProgressBefore))
		{
			return Result;
		}
	}
	Result.bIsInputValid = true;
	Result.CleanRatioBefore = CalculateCurrentCleanRatio(
		ProgressBefore.InitialCarrierAmount, ProgressBefore.CurrentCarrierAmount);
	Result.CleanRatioAfter = Result.CleanRatioBefore;
	Result.bIsSurfaceFullyClean = (Settings.CleanTolerance == 0.0f
		? ProgressBefore.UncleanCellCountAtZero : ProgressBefore.UncleanCellCount) == 0;
	if (Chord <= UE_SMALL_NUMBER || (Stroke.TravelDistanceCm <= 0.0f && Chord < Settings.MinimumTravelDistanceCm))
	{
		return Result;
	}
	const FVector StartAxis = Stroke.StartWorldDepthAxis.GetSafeNormal();
	const FVector EndAxis = Stroke.EndWorldDepthAxis.GetSafeNormal();
	const double RotationDegrees = !StartAxis.IsNearlyZero() && !EndAxis.IsNearlyZero()
		? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(FVector::DotProduct(StartAxis, EndAxis)), 0.0, 1.0))) : 0.0;
	const int32 SampleCount = FMath::Max3(1, FMath::CeilToInt32(PathLength / SampleSpacing),
		FMath::CeilToInt32(RotationDegrees / Settings.MaximumBladeRotationDegreesPerSample));
	if (SampleCount > 4096)
	{
		Result.bIsInputValid = false;
		return Result;
	}
	TArray<FMD_ScrapeMeshUVSample> Samples;
	TArray<FVector> DepthAxes;
	Samples.SetNum(SampleCount * 2 + 1);
	DepthAxes.SetNum(SampleCount * 2 + 1);
	// Validate the whole connected path before mutating any contamination state.
	for (int32 I = 0; I <= SampleCount * 2; ++I)
	{
		if (!TryInterpolateMeshUVStroke(Data, MeshToWorld, Stroke,
			static_cast<double>(I) / (SampleCount * 2), Samples[I], DepthAxes[I]))
		{
			Result.bIsInputValid = false;
			return Result;
		}
	}
	if (OutRequestedRemovalAmounts)
	{
		OutRequestedRemovalAmounts->SetNumZeroed(InOutSurfaceCells.Num());
	}

	Result.bWasStrokeLongEnough = true;
	bool bHasContaminationContact = false;
	const bool bCaptureRequests = bCaptureTouchedRequestedAmounts || OutRequestedRemovalAmounts;
	TArray<FMeshUVCellCoverage> Coverage;
	for (int32 I = 0; I < SampleCount; ++I)
	{
		const double Travel = bHasDirectSeam ? PathLength / SampleCount
			: FVector::Distance(MeshToWorld.TransformPosition(Samples[I * 2].PositionLocal),
			MeshToWorld.TransformPosition(Samples[I * 2 + 1].PositionLocal))
			+ FVector::Distance(MeshToWorld.TransformPosition(Samples[I * 2 + 1].PositionLocal),
				MeshToWorld.TransformPosition(Samples[I * 2 + 2].PositionLocal));
		CollectMeshUVFootprint(Data, MeshToWorld, Samples[I * 2 + 1], DepthAxes[I * 2 + 1], ToolSpec, Coverage, &State.MeshUVVisitedCellCount, &State,
            &Samples[I*2], &DepthAxes[I*2], &Samples[I*2+2], &DepthAxes[I*2+2]);
		Result.EvaluatedTravelDistanceCm += Travel;
		for (const FMeshUVCellCoverage& Hit : Coverage)
		{
			FMD_ScrapeSurfaceCell Cell = InOutSurfaceCells.Read(Hit.CellIndex);
			const int32* ExistingSlot = TouchedSlots.Find(Hit.CellIndex);
			const int32 Slot = ExistingSlot ? *ExistingSlot : State.MeshUVTouchedCells.Num();
			if (!ExistingSlot)
			{
				TouchedSlots.Add(Hit.CellIndex, Slot);
				FMD_ScrapeTouchedCell& Record = State.MeshUVTouchedCells.AddDefaulted_GetRef();
				Record.CellIndex = Hit.CellIndex;
				Record.Before = Cell;
			}
			if (bApplyRemoval) { Cell.Normalize(); }
			Result.bHadSurfaceContact = true;
			ApplyScrapeCoverageSample(Cell, Hit.Fraction, 1.0, Travel, ToolSpec, Settings,
				Result, bHasContaminationContact, bCaptureRequests
					? &State.MeshUVTouchedCells[Slot].RequestedRemovalAmount : nullptr, bApplyRemoval);
            if (bApplyRemoval) { InOutSurfaceCells.Set(Hit.CellIndex, Cell); }
		}
	}
	State.MeshUVTouchedCells.Sort([](const FMD_ScrapeTouchedCell& A, const FMD_ScrapeTouchedCell& B)
	{
		return A.CellIndex < B.CellIndex;
	});
	for (int32 Slot = 0; Slot < State.MeshUVTouchedCells.Num(); ++Slot)
	{
		const FMD_ScrapeTouchedCell& Record = State.MeshUVTouchedCells[Slot];
		if (OutRequestedRemovalAmounts)
		{
			(*OutRequestedRemovalAmounts)[Record.CellIndex] = static_cast<float>(
				Record.RequestedRemovalAmount * CellCarrierWeights[Record.CellIndex]);
		}
	}
	FinalizeScrapeEvaluation(Data.AtlasDimensions, Settings, InOutSurfaceCells, State, bHasContaminationContact,
		Result, CellCarrierWeights, OutProgressAfter, bCanIncrementProgress ? &ProgressBefore : nullptr);
	return Result;
}

bool FMD_ScrapeEvaluator::CalculateMeshUVProgress(
	const FMD_ScrapeConstCellView Cells,
	const FMD_ScrapeCarrierWeightView CellCarrierWeights, const float CleanTolerance,
	FMD_ScrapeMeshUVProgress& OutProgress)
{
	OutProgress = FMD_ScrapeMeshUVProgress();
	if (Cells.Num() != CellCarrierWeights.Num() || !FMath::IsFinite(CleanTolerance) || CleanTolerance < 0.0f)
	{
		return false;
	}
	FMD_ScrapeMeshUVProgress Progress;
	Progress.CleanTolerance = CleanTolerance;
	Progress.UncleanCellCount = Progress.UncleanCellCountAtZero = 0;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const double Weight = CellCarrierWeights[Index];
		if (!FMath::IsFinite(Weight) || Weight < 0.0) { return false; }
        FMD_ScrapeSurfaceCell Cell = Cells[Index];
        Progress.UncleanCellCount += IsCellUnclean(Cell, CleanTolerance) ? 1 : 0;
        Progress.UncleanCellCountAtZero += IsCellUnclean(Cell, 0.0f) ? 1 : 0;
        if (Weight == 0.0) { continue; }
		Cell.Normalize();
		Progress.InitialCarrierAmount += Weight * static_cast<double>(Cell.InitialContamination);
		Progress.BaseCarrierAmount += Weight * static_cast<double>(Cell.RemainingContamination);
		Progress.TransferredCarrierAmount += Weight * static_cast<double>(Cell.TransferredContamination);
	}
	Progress.CurrentCarrierAmount = Progress.BaseCarrierAmount + Progress.TransferredCarrierAmount;
	Progress.bIsValid = true;
	if (!HasDetailedMeshUVProgress(Progress, Cells.Num())) { return false; }
	OutProgress = Progress;
	return true;
}

bool FMD_ScrapeEvaluator::UpdateMeshUVProgressFromTouchedCells(
	const FMD_ScrapeMeshUVProgress& ProgressBefore,
	const TConstArrayView<FMD_ScrapeTouchedCell> TouchedCells,
	const FMD_ScrapeConstCellView Cells,
	const FMD_ScrapeCarrierWeightView CellCarrierWeights,
	FMD_ScrapeMeshUVProgress& OutProgress)
{
	if (Cells.Num() != CellCarrierWeights.Num() || !HasDetailedMeshUVProgress(ProgressBefore, Cells.Num()))
	{
		return false;
	}
	FMD_ScrapeMeshUVProgress Progress = ProgressBefore;
	double InitialDelta = 0.0, BaseDelta = 0.0, TransferredDelta = 0.0;
	int32 PreviousIndex = INDEX_NONE;
	for (const FMD_ScrapeTouchedCell& Record : TouchedCells)
	{
		const int32 Index = Record.CellIndex;
		if (!Cells.IsValidIndex(Index) || Index <= PreviousIndex) { return false; }
		PreviousIndex = Index;
		const double Weight = CellCarrierWeights[Index];
		if (!FMath::IsFinite(Weight) || Weight < 0.0) { return false; }
		const FMD_ScrapeSurfaceCell& AfterRaw = Cells[Index];
		Progress.UncleanCellCount += static_cast<int32>(IsCellUnclean(AfterRaw, Progress.CleanTolerance))
			- static_cast<int32>(IsCellUnclean(Record.Before, Progress.CleanTolerance));
		Progress.UncleanCellCountAtZero += static_cast<int32>(IsCellUnclean(AfterRaw, 0.0f))
			- static_cast<int32>(IsCellUnclean(Record.Before, 0.0f));
		FMD_ScrapeSurfaceCell Before = Record.Before;
		FMD_ScrapeSurfaceCell After = AfterRaw;
		Before.Normalize();
		After.Normalize();
		InitialDelta += Weight * (static_cast<double>(After.InitialContamination) - Before.InitialContamination);
		BaseDelta += Weight * (static_cast<double>(After.RemainingContamination) - Before.RemainingContamination);
		TransferredDelta += Weight * (static_cast<double>(After.TransferredContamination) - Before.TransferredContamination);
	}
	Progress.InitialCarrierAmount += InitialDelta;
	Progress.BaseCarrierAmount += BaseDelta;
	Progress.TransferredCarrierAmount += TransferredDelta;
	// Incremental double sums can differ from an ascending full scan only by rounding.
	// Keep the bound explicit; larger negative totals indicate a stale/mismatched cache.
	const double RoundingTolerance = 1.0e-9 * FMath::Max3(1.0,
		ProgressBefore.InitialCarrierAmount, ProgressBefore.CurrentCarrierAmount);
	const auto ClampRounding = [RoundingTolerance](double& Amount)
	{
		if (!FMath::IsFinite(Amount) || Amount < -RoundingTolerance) { return false; }
		Amount = FMath::Max(0.0, Amount);
		return true;
	};
	if (!ClampRounding(Progress.InitialCarrierAmount) || !ClampRounding(Progress.BaseCarrierAmount)
		|| !ClampRounding(Progress.TransferredCarrierAmount)) { return false; }
	Progress.CurrentCarrierAmount = Progress.BaseCarrierAmount + Progress.TransferredCarrierAmount;
	if (!HasDetailedMeshUVProgress(Progress, Cells.Num())) { return false; }
	OutProgress = Progress;
	return true;
}

bool FMD_ScrapeEvaluator::TryApplyContaminationStampToTouchedCells(
	const FIntPoint& GridDimensions,
	const TConstArrayView<FMD_ScrapeTouchedCell> TouchedCells,
	const float StampRatio,
	const FMD_ScrapeCellView InOutSurfaceCells,
	TArray<FMD_ScrapeCellChange>& OutCellChanges,
	float& OutAddedContaminationAmount,
	const FMD_ScrapeCarrierWeightView CellCarrierWeights)
{
	OutCellChanges.Reset();
	OutAddedContaminationAmount = 0.0f;
	const int64 ExpectedCellCount = static_cast<int64>(GridDimensions.X) * GridDimensions.Y;
	if (GridDimensions.X <= 0 || GridDimensions.Y <= 0 || ExpectedCellCount > MAX_int32
		|| InOutSurfaceCells.Num() != ExpectedCellCount
		|| (!CellCarrierWeights.IsEmpty() && CellCarrierWeights.Num() != ExpectedCellCount)
		|| !FMath::IsFinite(StampRatio) || StampRatio <= 0.0f)
	{
		return false;
	}
	const float SafeRatio = FMath::Clamp(StampRatio, 0.0f, 1.0f);
	const auto SafeTransferred = [](const FMD_ScrapeSurfaceCell& Cell)
	{
		return FMath::IsFinite(Cell.TransferredContamination)
			? FMath::Clamp(Cell.TransferredContamination, 0.0f, 1.0f) : 0.0f;
	};
	const float RequestThreshold = CellCarrierWeights.IsEmpty() ? UE_SMALL_NUMBER : 0.0f;
	const auto TargetTransferred = [SafeRatio](const float Previous, const float RequestedCarrier, const double Weight)
	{
		// Reproduce the existing dense capture's float carrier round trip on K records.
		return Previous + static_cast<float>(RequestedCarrier * static_cast<double>(SafeRatio) / Weight);
	};
	int32 PreviousIndex = INDEX_NONE;
	float CheckedAddedAmount = 0.0f;
	for (const FMD_ScrapeTouchedCell& Record : TouchedCells)
	{
		const int32 Index = Record.CellIndex;
		if (!InOutSurfaceCells.IsValidIndex(Index) || Index <= PreviousIndex) { return false; }
		PreviousIndex = Index;
		const double Weight = CellCarrierWeights.IsEmpty() ? 1.0 : CellCarrierWeights[Index];
		const float Requested = Record.RequestedRemovalAmount;
		if (!FMath::IsFinite(Weight) || Weight < 0.0 || !FMath::IsFinite(Requested) || Requested < 0.0f
			|| (Requested > 0.0f && Weight <= 0.0)) { return false; }
		const float RequestedCarrier = static_cast<float>(Requested * Weight);
		if (!FMath::IsFinite(RequestedCarrier)) { return false; }
		if (RequestedCarrier <= RequestThreshold) { continue; }
		const float Previous = SafeTransferred(InOutSurfaceCells.Read(Index));
		const float Target = TargetTransferred(Previous, RequestedCarrier, Weight);
		if (!FMath::IsFinite(Target) || Target > 1.0f) { return false; }
		if (Target <= Previous + UE_SMALL_NUMBER) { continue; }
		CheckedAddedAmount += static_cast<float>((Target - Previous) * Weight);
		if (!FMath::IsFinite(CheckedAddedAmount)) { return false; }
	}
	// No state changes occur until every touched request and the output sum are valid.
	for (const FMD_ScrapeTouchedCell& Record : TouchedCells)
	{
		const double Weight = CellCarrierWeights.IsEmpty() ? 1.0 : CellCarrierWeights[Record.CellIndex];
		const float RequestedCarrier = static_cast<float>(Record.RequestedRemovalAmount * Weight);
		if (RequestedCarrier <= RequestThreshold) { continue; }
		FMD_ScrapeSurfaceCell Cell = InOutSurfaceCells.Read(Record.CellIndex);
		const float Previous = SafeTransferred(Cell);
		const float Target = TargetTransferred(Previous, RequestedCarrier, Weight);
		if (Target <= Previous + UE_SMALL_NUMBER) { continue; }
		Cell.Normalize();
		Cell.TransferredContamination = Target;
		Cell.TransferredPaintPatternDensity = 1.0f;
        InOutSurfaceCells.Set(Record.CellIndex, Cell);
		FMD_ScrapeCellChange& Change = OutCellChanges.AddDefaulted_GetRef();
		Change.CellIndex = Record.CellIndex;
		Change.GridCoordinate = FIntPoint(Record.CellIndex % GridDimensions.X, Record.CellIndex / GridDimensions.X);
		Change.RemovedAmount = 0.0f;
		Change.RemainingAmountAfter = Cell.RemainingContamination;
		Change.bBecameClean = false;
	}
	OutAddedContaminationAmount = CheckedAddedAmount;
	return true;
}

bool FMD_ScrapeEvaluator::TryApplyContaminationStampToGrid(
	const FIntPoint& GridDimensions,
	const TConstArrayView<float> RequestedRemovalAmounts,
	const float StampRatio,
	const FMD_ScrapeCellView InOutSurfaceCells,
	TArray<FMD_ScrapeCellChange>& OutCellChanges,
	float& OutAddedContaminationAmount,
	const FMD_ScrapeCarrierWeightView CellCarrierWeights)
{
	OutCellChanges.Reset();
	OutAddedContaminationAmount = 0.0f;

	const int64 ExpectedCellCount =
		static_cast<int64>(GridDimensions.X)
		* static_cast<int64>(GridDimensions.Y);
	if (GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| ExpectedCellCount > MAX_int32
		|| RequestedRemovalAmounts.Num() != ExpectedCellCount
		|| InOutSurfaceCells.Num() != ExpectedCellCount
		|| (!CellCarrierWeights.IsEmpty() && CellCarrierWeights.Num() != ExpectedCellCount)
		|| !FMath::IsFinite(StampRatio)
		|| StampRatio <= 0.0f)
	{
		return false;
	}

    // Weighted requests are carrier units: even a tiny positive carrier can represent a full-density cell.
    const float RequestThreshold = CellCarrierWeights.IsEmpty() ? UE_SMALL_NUMBER : 0.0f;
	const float SafeStampRatio = FMath::Clamp(StampRatio, 0.0f, 1.0f);
	const auto GetSafeCurrentTransferred =
		[](const float CurrentTransferred)
		{
			return FMath::IsFinite(CurrentTransferred)
				? FMath::Clamp(CurrentTransferred, 0.0f, 1.0f)
				: 0.0f;
		};
	const auto CalculateTargetTransferred =
		[SafeStampRatio](
			const float CurrentTransferred,
			const float RequestedRemovalAmount, const double CarrierWeight)
		{
			return CurrentTransferred
				+ static_cast<float>(RequestedRemovalAmount * static_cast<double>(SafeStampRatio) / CarrierWeight);
		};

	// DEC-020: 어느 한 셀이라도 목표 역오염을 전부 담지 못하면 전체 segment를
	// 거부한다. 이 사전 검사는 실제 셀을 하나도 바꾸지 않아 부분 적용을 막는다.
	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		const float RequestedRemovalAmount = RequestedRemovalAmounts[CellIndex];
		const double CarrierWeight = CellCarrierWeights.IsEmpty() ? 1.0 : CellCarrierWeights[CellIndex];
		if (!FMath::IsFinite(CarrierWeight) || CarrierWeight < 0.0
			|| (RequestedRemovalAmount > RequestThreshold && CarrierWeight <= 0.0)
			|| !FMath::IsFinite(RequestedRemovalAmount)
			|| RequestedRemovalAmount < 0.0f)
		{
			return false;
		}
		if (RequestedRemovalAmount <= RequestThreshold)
		{
			continue;
		}

		const float TargetTransferred = CalculateTargetTransferred(
			GetSafeCurrentTransferred(
				InOutSurfaceCells.Read(CellIndex).TransferredContamination),
			RequestedRemovalAmount, CarrierWeight);
		if (!FMath::IsFinite(TargetTransferred)
			|| TargetTransferred > 1.0f)
		{
			return false;
		}
	}

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		const float RequestedRemovalAmount = RequestedRemovalAmounts[CellIndex];
		const double CarrierWeight = CellCarrierWeights.IsEmpty() ? 1.0 : CellCarrierWeights[CellIndex];
		if (RequestedRemovalAmount <= RequestThreshold)
		{
			continue;
		}

		FMD_ScrapeSurfaceCell Cell = InOutSurfaceCells.Read(CellIndex);
		const float PreviousTransferred =
			GetSafeCurrentTransferred(Cell.TransferredContamination);
		const float TargetTransferred = CalculateTargetTransferred(
			PreviousTransferred,
			RequestedRemovalAmount, CarrierWeight);
		if (TargetTransferred <= PreviousTransferred + UE_SMALL_NUMBER)
		{
			continue;
		}

		Cell.Normalize();
		Cell.TransferredContamination = TargetTransferred;
		Cell.TransferredPaintPatternDensity = 1.0f;
        InOutSurfaceCells.Set(CellIndex, Cell);

		FMD_ScrapeCellChange& Change = OutCellChanges.AddDefaulted_GetRef();
		Change.CellIndex = CellIndex;
		Change.GridCoordinate = FIntPoint(
			CellIndex % GridDimensions.X,
			CellIndex / GridDimensions.X);
		Change.RemovedAmount = 0.0f;
		Change.RemainingAmountAfter = Cell.RemainingContamination;
		Change.bBecameClean = false;

		OutAddedContaminationAmount +=
			static_cast<float>((TargetTransferred - PreviousTransferred) * CarrierWeight);
	}

	return true;
}

bool FMD_ScrapeEvaluator::IsEvaluationInputValid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const FMD_ScrapeConstCellView SurfaceCells,
	FString* OutErrorMessage)
{
	return IsEvaluationInputValid(
		Stroke,
		Settings.GetToolSpec(),
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		SurfaceCells,
		OutErrorMessage);
}

bool FMD_ScrapeEvaluator::IsEvaluationInputValid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const FMD_ScrapeConstCellView SurfaceCells,
	FString* OutErrorMessage)
{
	// 성공할 경우 이전 호출에서 남은 오류 문자열이 사용되지 않도록 먼저 초기화
	if (OutErrorMessage != nullptr)
	{
		OutErrorMessage->Reset();
	}

	const auto FailValidation =
		[OutErrorMessage](const TCHAR* ErrorMessage) -> bool
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = ErrorMessage;
			}

			return false;
		};

	if (Stroke.SurfaceFace != EMD_ScrapeSurfaceFace::Front
		&& Stroke.SurfaceFace != EMD_ScrapeSurfaceFace::Back)
	{
		return FailValidation(
			TEXT("Stroke.SurfaceFace must be Front or Back."));
	}

	if (GridDimensions.X <= 0 || GridDimensions.Y <= 0)
	{
		return FailValidation(
			TEXT("GridDimensions must contain positive X and Y cell counts."));
	}

	// int32 곱셈 전에 int64로 승격하여 아주 큰 잘못된 입력에서 발생하는 오버플로를 차단
	const int64 ExpectedCellCount =
		static_cast<int64>(GridDimensions.X)
		* static_cast<int64>(GridDimensions.Y);

	if (ExpectedCellCount > MAX_int32
		|| SurfaceCells.Num() != static_cast<int32>(ExpectedCellCount))
	{
		return FailValidation(
			TEXT("SurfaceCells.Num() must equal GridDimensions.X * GridDimensions.Y."));
	}

	if (!FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER)
	{
		return FailValidation(
			TEXT("SurfaceSizeCm must contain finite, positive X and Y sizes."));
	}

	if (!FMath::IsFinite(Stroke.StartPositionCm.X)
		|| !FMath::IsFinite(Stroke.StartPositionCm.Y)
		|| !FMath::IsFinite(Stroke.EndPositionCm.X)
		|| !FMath::IsFinite(Stroke.EndPositionCm.Y))
	{
		return FailValidation(
			TEXT("Scrape stroke positions must contain finite values."));
	}

	if (!FMath::IsFinite(Stroke.TravelDistanceCm)
		|| Stroke.TravelDistanceCm < 0.0f)
	{
		return FailValidation(
			TEXT("Stroke.TravelDistanceCm must be finite and zero or greater."));
	}

	FResolvedBladeBasis ResolvedStartBasis;
	FResolvedBladeBasis ResolvedEndBasis;
	const EBladeBasisResolveState StartBasisState = ResolveBladeBasis(
		Stroke.StartBladeBasis,
		ResolvedStartBasis);
	const EBladeBasisResolveState EndBasisState = ResolveBladeBasis(
		Stroke.EndBladeBasis,
		ResolvedEndBasis);
	if (StartBasisState == EBladeBasisResolveState::Invalid
		|| EndBasisState == EBladeBasisResolveState::Invalid)
	{
		return FailValidation(
			TEXT("Each explicit scrape blade basis must contain two finite, non-collinear axes."));
	}
	if (StartBasisState == EBladeBasisResolveState::Valid
		&& EndBasisState == EBladeBasisResolveState::Valid
		&& !HaveMatchingBladeBasisHandedness(
			ResolvedStartBasis,
			ResolvedEndBasis))
	{
		return FailValidation(
			TEXT("Explicit scrape blade basis endpoints must use matching handedness."));
	}

	if (ToolSpec.BrushShape != EMD_ScrapeBrushShape::Rectangle
		&& ToolSpec.BrushShape != EMD_ScrapeBrushShape::Circle)
	{
		return FailValidation(
			TEXT("ToolSpec.BrushShape must be Rectangle or Circle."));
	}

	if (!FMath::IsFinite(ToolSpec.BladeWidthCm)
		|| ToolSpec.BladeWidthCm <= UE_SMALL_NUMBER)
	{
		return FailValidation(
			TEXT("ToolSpec.BladeWidthCm must be finite and greater than zero."));
	}

	if (!FMath::IsFinite(ToolSpec.BladeDepthCm)
		|| ToolSpec.BladeDepthCm <= UE_SMALL_NUMBER)
	{
		return FailValidation(
			TEXT("ToolSpec.BladeDepthCm must be finite and greater than zero."));
	}

	if (!FMath::IsFinite(ToolSpec.RemovalPerTravelCentimeter)
		|| ToolSpec.RemovalPerTravelCentimeter <= UE_SMALL_NUMBER)
	{
		return FailValidation(
			TEXT("ToolSpec.RemovalPerTravelCentimeter must be finite and greater than zero."));
	}

	if (!FMath::IsFinite(Settings.SampleSpacingRatioOfSmallestCell)
		|| Settings.SampleSpacingRatioOfSmallestCell <= UE_SMALL_NUMBER
		|| Settings.SampleSpacingRatioOfSmallestCell > 1.0f)
	{
		return FailValidation(
			TEXT("SampleSpacingRatioOfSmallestCell must be in the (0, 1] range."));
	}

	if (!FMath::IsFinite(Settings.MaximumSampleSpacingCm)
		|| Settings.MaximumSampleSpacingCm <= UE_SMALL_NUMBER)
	{
		return FailValidation(
			TEXT("MaximumSampleSpacingCm must be finite and greater than zero."));
	}

	if (!FMath::IsFinite(Settings.MaximumBladeRotationDegreesPerSample)
		|| Settings.MaximumBladeRotationDegreesPerSample <= UE_SMALL_NUMBER
		|| Settings.MaximumBladeRotationDegreesPerSample > 90.0f)
	{
		return FailValidation(
			TEXT("MaximumBladeRotationDegreesPerSample must be in the (0, 90] range."));
	}

	if (!FMath::IsFinite(Settings.MinimumTravelDistanceCm)
		|| Settings.MinimumTravelDistanceCm < 0.0f)
	{
		return FailValidation(
			TEXT("MinimumTravelDistanceCm must be finite and zero or greater."));
	}

	if (!FMath::IsFinite(Settings.CleanTolerance)
		|| Settings.CleanTolerance < 0.0f
		|| Settings.CleanTolerance > 0.1f)
	{
		return FailValidation(
			TEXT("CleanTolerance must be in the [0, 0.1] range."));
	}

	return true;
}

float FMD_ScrapeEvaluator::CalculateCleanRatio(
	const FMD_ScrapeConstCellView SurfaceCells)
{
	double TotalInitialContamination = 0.0;
	double TotalRemainingContamination = 0.0;

	for (const FMD_ScrapeSurfaceCell& Cell : SurfaceCells)
	{
		// 이 함수는 const 입력을 받으므로 원본을 Normalize하지 않고 안전한 지역 값으로 보정
		const double SafeInitialContamination =
			FMath::IsFinite(Cell.InitialContamination)
			? FMath::Clamp(
				static_cast<double>(Cell.InitialContamination),
				0.0,
				1.0)
			: 0.0;

		const double SafeRemainingContamination =
			FMath::IsFinite(Cell.RemainingContamination)
			? FMath::Clamp(
				static_cast<double>(Cell.RemainingContamination),
				0.0,
				SafeInitialContamination)
			: 0.0;

		TotalInitialContamination += SafeInitialContamination;
		TotalRemainingContamination += SafeRemainingContamination;
	}

	// 애초에 오염이 하나도 없는 표면은 추가 작업 없이 완료 상태로 간주
	if (TotalInitialContamination <= UE_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return static_cast<float>(FMath::Clamp(
		1.0 - (TotalRemainingContamination / TotalInitialContamination),
		0.0,
		1.0));
}

float FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
	const FMD_ScrapeConstCellView SurfaceCells)
{
	double InitialContaminationAmount = 0.0;
	double CurrentContaminationAmount = 0.0;
	return CalculateCurrentCleanRatio(
		SurfaceCells,
		InitialContaminationAmount,
		CurrentContaminationAmount);
}

float FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
	const FMD_ScrapeConstCellView SurfaceCells,
	double& OutInitialContaminationAmount,
	double& OutCurrentContaminationAmount)
{
	OutInitialContaminationAmount = 0.0;
	OutCurrentContaminationAmount = 0.0;

	for (const FMD_ScrapeSurfaceCell& Cell : SurfaceCells)
	{
		const double SafeInitialContamination =
			FMath::IsFinite(Cell.InitialContamination)
			? FMath::Clamp(
				static_cast<double>(Cell.InitialContamination),
				0.0,
				1.0)
			: 0.0;
		const double SafeRemainingContamination =
			FMath::IsFinite(Cell.RemainingContamination)
			? FMath::Clamp(
				static_cast<double>(Cell.RemainingContamination),
				0.0,
				SafeInitialContamination)
			: 0.0;
		const double SafeTransferredContamination =
			FMath::IsFinite(Cell.TransferredContamination)
			? FMath::Clamp(
				static_cast<double>(Cell.TransferredContamination),
				0.0,
				1.0)
			: 0.0;

		OutInitialContaminationAmount += SafeInitialContamination;
		OutCurrentContaminationAmount +=
			SafeRemainingContamination + SafeTransferredContamination;
	}

	return CalculateCurrentCleanRatio(
		OutInitialContaminationAmount,
		OutCurrentContaminationAmount);
}

float FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
	const double InitialContaminationAmount,
	const double CurrentContaminationAmount)
{
	const double SafeInitialContaminationAmount =
		FMath::IsFinite(InitialContaminationAmount)
		? FMath::Max(0.0, InitialContaminationAmount)
		: 0.0;
	const double SafeCurrentContaminationAmount =
		FMath::IsFinite(CurrentContaminationAmount)
		? FMath::Max(0.0, CurrentContaminationAmount)
		: 0.0;

	// 처음부터 깨끗한 표면도 새로 묻은 오염이 있으면 조각 품질 제거율은 0%다.
	if (SafeInitialContaminationAmount <= UE_SMALL_NUMBER)
	{
		return SafeCurrentContaminationAmount <= UE_SMALL_NUMBER ? 1.0f : 0.0f;
	}

	return static_cast<float>(FMath::Clamp(
		1.0 - (SafeCurrentContaminationAmount
			/ SafeInitialContaminationAmount),
		0.0,
		1.0));
}

bool FMD_ScrapeEvaluator::IsSurfaceFullyClean(
	const FMD_ScrapeConstCellView SurfaceCells,
	const float CleanTolerance)
{
	const float SafeTolerance = FMath::IsFinite(CleanTolerance)
		? FMath::Max(0.0f, CleanTolerance)
		: 0.0f;

	for (const FMD_ScrapeSurfaceCell& Cell : SurfaceCells)
	{
		const float SafeTransferredContamination =
			FMath::IsFinite(Cell.TransferredContamination)
			? FMath::Clamp(Cell.TransferredContamination, 0.0f, 1.0f)
			: 0.0f;
		if (SafeTransferredContamination > SafeTolerance)
		{
			return false;
		}

		const float SafeInitialContamination =
			FMath::IsFinite(Cell.InitialContamination)
			? FMath::Clamp(Cell.InitialContamination, 0.0f, 1.0f)
			: 0.0f;

		// 처음부터 오염이 없던 셀은 완료 판정 대상에서 제외
		if (SafeInitialContamination <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const float SafeRemainingContamination =
			FMath::IsFinite(Cell.RemainingContamination)
			? FMath::Clamp(
				Cell.RemainingContamination,
				0.0f,
				SafeInitialContamination)
			: 0.0f;

		if (SafeRemainingContamination > SafeTolerance)
		{
			return false;
		}
	}

	return true;
}

double FMD_ScrapeEvaluator::CalculateCircleCellCoverageWeight(
	const double DistanceFromBrushCenterCm,
	const double BladeRadiusCm,
	const double CellHalfDiagonalCm)
{
	// 기존 원형 판정의 피복 특성을 유지해 원형 업그레이드 전환 시 수치가 달라지지 않게 한다.
	const double FullCoverageRadiusCm = FMath::Max(
		0.0,
		BladeRadiusCm - CellHalfDiagonalCm);
	const double ZeroCoverageRadiusCm =
		BladeRadiusCm + CellHalfDiagonalCm;

	if (DistanceFromBrushCenterCm <= FullCoverageRadiusCm)
	{
		return 1.0;
	}

	if (DistanceFromBrushCenterCm >= ZeroCoverageRadiusCm)
	{
		return 0.0;
	}

	const double FadeRangeCm =
		ZeroCoverageRadiusCm - FullCoverageRadiusCm;
	const double FadeAlpha =
		(DistanceFromBrushCenterCm - FullCoverageRadiusCm)
		/ FadeRangeCm;
	return FMath::Clamp(1.0 - FadeAlpha, 0.0, 1.0);
}

double FMD_ScrapeEvaluator::CalculateRectangleCellCoverageWeight(
	const FVector2D& BrushToCellCenterCm,
	const FVector2D& BladeWidthAxis,
	const FVector2D& BladeDepthAxis,
	const double BladeHalfWidthCm,
	const double BladeHalfDepthCm,
	const double CellHalfDiagonalCm)
{
	// 셀 중심을 스트로크 방향 기준의 사각 헤라 로컬 좌표로 변환
	const double LocalWidthDistanceCm = FMath::Abs(
		FVector2D::DotProduct(BrushToCellCenterCm, BladeWidthAxis));
	const double LocalDepthDistanceCm = FMath::Abs(
		FVector2D::DotProduct(BrushToCellCenterCm, BladeDepthAxis));

	// 표준 사각형 signed distance: 음수는 내부, 0은 경계, 양수는 외부
	const double WidthOutsideCm =
		LocalWidthDistanceCm - BladeHalfWidthCm;
	const double DepthOutsideCm =
		LocalDepthDistanceCm - BladeHalfDepthCm;
	const double OutsideDistanceCm = FMath::Sqrt(
		FMath::Square(FMath::Max(WidthOutsideCm, 0.0))
		+ FMath::Square(FMath::Max(DepthOutsideCm, 0.0)));
	const double InsideDistanceCm = FMath::Min(
		FMath::Max(WidthOutsideCm, DepthOutsideCm),
		0.0);
	const double SignedDistanceToRectangleCm =
		OutsideDistanceCm + InsideDistanceCm;

	return CalculateCoverageWeightFromSignedDistance(
		SignedDistanceToRectangleCm,
		CellHalfDiagonalCm);
}

double FMD_ScrapeEvaluator::CalculateCoverageWeightFromSignedDistance(
	const double SignedDistanceToBrushCm,
	const double CellHalfDiagonalCm)
{
	const double SafeCellHalfDiagonalCm = FMath::Max(
		CellHalfDiagonalCm,
		static_cast<double>(UE_SMALL_NUMBER));

	// 셀을 중심과 반대각선으로 근사해 브러시 안쪽은 1, 바깥쪽은 0으로 부드럽게 전환
	return FMath::Clamp(
		(SafeCellHalfDiagonalCm - SignedDistanceToBrushCm)
			/ (2.0 * SafeCellHalfDiagonalCm),
		0.0,
		1.0);
}
