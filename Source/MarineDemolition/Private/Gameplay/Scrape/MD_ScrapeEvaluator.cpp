// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"

#include "Containers/BitArray.h"

#include "Util/MD_LogUtility.h"

namespace
{
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

	uint32 AvalancheContaminationPaintHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352DU;
		Value ^= Value >> 15;
		Value *= 0x846CA68BU;
		Value ^= Value >> 16;
		return Value;
	}

	uint32 BuildContaminationPaintHash(
		const int32 PatternSeed,
		const int32 DabBinIndex,
		const uint32 ChannelSalt)
	{
		uint32 Value = AvalancheContaminationPaintHash(
			static_cast<uint32>(DabBinIndex) + 0x9E3779B9U);
		Value ^= AvalancheContaminationPaintHash(
			static_cast<uint32>(PatternSeed) + 0x85EBCA6BU);
		Value ^= AvalancheContaminationPaintHash(
			ChannelSalt + 0xC2B2AE35U);
		return AvalancheContaminationPaintHash(Value);
	}

	double ContaminationPaintHashToUnitDouble(const uint32 Hash)
	{
		// 상위 24비트를 사용해 플랫폼과 프레임 분할에 무관한 [0, 1) 값을 만든다.
		return static_cast<double>(Hash >> 8) / 16777216.0;
	}

	int32 BuildContaminationPaintIdentitySeed(
		const FMD_ScrapeContaminationStampIdentity& Identity,
		const int32 PatternSeed)
	{
		uint32 Hash = HashCombineFast(
			GetTypeHash(PatternSeed),
			GetTypeHash(Identity.FocusSessionGeneration));
		Hash = HashCombineFast(Hash, GetTypeHash(Identity.LmbStrokeGeneration));
		Hash = HashCombineFast(Hash, GetTypeHash(Identity.SubStrokeGeneration));
		return static_cast<int32>(
			AvalancheContaminationPaintHash(Hash) & static_cast<uint32>(MAX_int32));
	}

	double CalculateTransferredPaintPatternDensity(
		const FVector2D& BrushToCellCenterCm,
		const FVector2D& CanonicalBladeWidthAxis,
		const double BladeHalfWidthCm)
	{
		if (BladeHalfWidthCm <= UE_SMALL_NUMBER)
		{
			return 1.0;
		}

		const double NormalizedWidth = FMath::Clamp(
			FVector2D::DotProduct(
				BrushToCellCenterCm,
				CanonicalBladeWidthAxis)
				/ (BladeHalfWidthCm * 2.0)
				+ 0.5,
			0.0,
			1.0);
		const double SmoothedWidth = NormalizedWidth * NormalizedWidth
			* (3.0 - 2.0 * NormalizedWidth);

		// 참조 붓자국처럼 긴 축의 전반부는 약 50% 점묘로 유지하고,
		// 반대쪽 끝으로 갈수록 약 96%까지 조밀해진다.
		return 0.50 + 0.46 * SmoothedWidth;
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
	TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells)
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
	TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells)
{
	return ApplyStrokeToGridInternal(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		nullptr);
}

FMD_ScrapeResult
FMD_ScrapeEvaluator::ApplyStrokeToGridAndCaptureRequestedAmounts(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
	TArray<float>& OutRequestedRemovalAmounts)
{
	return ApplyStrokeToGridInternal(
		Stroke,
		ToolSpec,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		InOutSurfaceCells,
		&OutRequestedRemovalAmounts);
}

FMD_ScrapeResult FMD_ScrapeEvaluator::ApplyStrokeToGridInternal(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
	TArray<float>* const OutRequestedRemovalAmounts)
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

	// 모든 셀을 0~InitialContamination 범위로 정규화 수행
	for (FMD_ScrapeSurfaceCell& Cell : InOutSurfaceCells)
	{
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
	TBitArray<> TouchedCells(false, InOutSurfaceCells.Num());

	// 변경 내역과 "이번 스트로크로 처음 깨끗해졌는가"를 마지막에 정확히 계산하기 위해
	// 적용 전 잔여량을 별도 배열에 보관
	TArray<float> RemainingBeforeStroke;
	RemainingBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());
	TArray<float> TransferredBeforeStroke;
	TransferredBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());
	// 시각 마스크는 잔여량과 피복률을 면적으로 가중합하므로, 잔여량이 그대로여도
	// 피복률만 늘면 겉모습이 달라진다. 그런 셀도 변경 내역에 담아야 마스크가
	// 갱신된다. 피복률은 단조 증가라 1에 포화되면 더는 변경을 만들지 않는다.
	TArray<float> CoveredFractionBeforeStroke;
	CoveredFractionBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		RemainingBeforeStroke[CellIndex] = InOutSurfaceCells[CellIndex].RemainingContamination;
		TransferredBeforeStroke[CellIndex] = InOutSurfaceCells[CellIndex].TransferredContamination;
		CoveredFractionBeforeStroke[CellIndex] =
			InOutSurfaceCells[CellIndex].CoveredFraction;
	}

	bool bRemovedAnyContamination = false;

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

				Result.bHadSurfaceContact = true;
				TouchedCells[CellIndex] = true;

				FMD_ScrapeSurfaceCell& Cell = InOutSurfaceCells[CellIndex];

				// 셀 중심만 보고 딱 잘라 판정하면 격자 모양 경계가 생기므로
				// 아래 하한 계산과 같은 정규화 피복률을 시각용으로 누적한다.
				// 이미 깨끗한 셀도 갱신해야 한다. 그래야 나중에 오염이 다시
				// 쌓였을 때 "얼마나 덮였는지"가 이 스트로크까지 반영된다.
				const double NormalizedCoverageWeight =
					FullContactCoverageWeight > UE_SMALL_NUMBER
					? FMath::Min(
						1.0,
						CoverageWeight / FullContactCoverageWeight)
					: 1.0;
				Cell.CoveredFraction = FMath::Max(
					Cell.CoveredFraction,
					static_cast<float>(NormalizedCoverageWeight));

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
				if (OutRequestedRemovalAmounts
					&& OutRequestedRemovalAmounts->IsValidIndex(CellIndex))
				{
					(*OutRequestedRemovalAmounts)[CellIndex] +=
						static_cast<float>(RequestedRemovalAmount);
				}

				// 이미 깨끗한 셀에 작업량이 가해지면 모든 힘이 맨 벽면으로 전달
				if (TotalRemainingBeforeSample
					<= static_cast<double>(Settings.CleanTolerance))
				{
					Result.bTouchedBareSurface = true;
					continue;
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
					bRemovedAnyContamination = true;
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
		}
	}

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		if (TouchedCells[CellIndex])
		{
			++Result.TouchedCellCount;
		}

		const FMD_ScrapeSurfaceCell& CellAfterStroke =
			InOutSurfaceCells[CellIndex];
		const float RemovedBaseAmount = FMath::Max(
			0.0f,
			RemainingBeforeStroke[CellIndex]
				- CellAfterStroke.RemainingContamination);
		const float RemovedTransferredAmount = FMath::Max(
			0.0f,
			TransferredBeforeStroke[CellIndex]
				- CellAfterStroke.TransferredContamination);
		const float RemovedAmount =
			RemovedBaseAmount + RemovedTransferredAmount;
		const float AddedCoveredFraction = FMath::Max(
			0.0f,
			CellAfterStroke.CoveredFraction
				- CoveredFractionBeforeStroke[CellIndex]);

		// 부동소수점 연산 중 생길 수 있는 무의미한 극소 차이는 변경 내역에서 제외
		if (RemovedAmount <= UE_SMALL_NUMBER
			&& AddedCoveredFraction <= UE_SMALL_NUMBER)
		{
			continue;
		}

		FMD_ScrapeCellChange& CellChange = Result.CellChanges.AddDefaulted_GetRef();
		CellChange.CellIndex = CellIndex;
		CellChange.GridCoordinate = FIntPoint(
			CellIndex % GridDimensions.X,
			CellIndex / GridDimensions.X);
		CellChange.RemovedAmount = RemovedAmount;
		CellChange.RemainingAmountAfter =
			CellAfterStroke.RemainingContamination;
		CellChange.bBecameClean =
			(RemainingBeforeStroke[CellIndex] > Settings.CleanTolerance
				|| TransferredBeforeStroke[CellIndex] > Settings.CleanTolerance)
			&& CellAfterStroke.RemainingContamination <= Settings.CleanTolerance
			&& CellAfterStroke.TransferredContamination <= Settings.CleanTolerance;

		// Surface의 보상 provenance 원장은 base와 Stage3 transferred를
		// 서로 다른 layer로 보존한다. RPC/Blueprint에는 노출하지 않는
		// server-only 합계만 기록해 실제 제거 비율대로 lot을 옮길 수 있게 한다.
		Result.RemovedBaseContaminationAmount += RemovedBaseAmount;
		Result.RemovedTransferredContaminationAmount +=
			RemovedTransferredAmount;
		Result.RemovedContaminationAmount += RemovedAmount;

		if (CellChange.bBecameClean)
		{
			++Result.NewlyCleanedCellCount;
		}
	}

	Result.ModifiedCellCount = Result.CellChanges.Num();
	Result.CleanRatioAfter = CalculateCleanRatio(InOutSurfaceCells);
	Result.CleanRatioDelta = FMath::Max(
		0.0f,
		Result.CleanRatioAfter - Result.CleanRatioBefore);
	// 순수 제거 판정에는 아직 역오염 단계가 없으므로 gross와 net이 같다.
	// Surface가 같은 트랜잭션에서 역오염을 더하면 두 net 필드만 다시 줄인다.
	Result.NetRemovedContaminationAmount =
		Result.RemovedContaminationAmount;
	Result.NetRemovedCleanRatioDelta = Result.CleanRatioDelta;
	Result.bIsSurfaceFullyClean = IsSurfaceFullyClean(
		InOutSurfaceCells,
		Settings.CleanTolerance);

	// 접촉 종류는 실제 제거가 있었는지와 맨 벽면 접촉이 있었는지의 조합으로 결정
	// 이 분류 자체는 감점을 발생시키지 않으며 다음 계층이 피드백/규칙을 선택
	if (!Result.bHadSurfaceContact)
	{
		Result.ContactType = EMD_ScrapeContactType::None;
	}
	else if (bRemovedAnyContamination && Result.bTouchedBareSurface)
	{
		Result.ContactType = EMD_ScrapeContactType::Mixed;
	}
	else if (bRemovedAnyContamination)
	{
		Result.ContactType = EMD_ScrapeContactType::ContaminationOnly;
	}
	else
	{
		Result.ContactType = EMD_ScrapeContactType::BareSurfaceOnly;
	}

	return Result;
}

bool FMD_ScrapeEvaluator::TryApplyContaminationStampToGrid(
	const FIntPoint& GridDimensions,
	const TConstArrayView<float> RequestedRemovalAmounts,
	const float StampRatio,
	const TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
	TArray<FMD_ScrapeCellChange>& OutCellChanges,
	float& OutAddedContaminationAmount)
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
		|| !FMath::IsFinite(StampRatio)
		|| StampRatio <= 0.0f)
	{
		return false;
	}

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
			const float RequestedRemovalAmount)
		{
			return CurrentTransferred
				+ RequestedRemovalAmount * SafeStampRatio;
		};

	// DEC-020: 어느 한 셀이라도 목표 역오염을 전부 담지 못하면 전체 segment를
	// 거부한다. 이 사전 검사는 실제 셀을 하나도 바꾸지 않아 부분 적용을 막는다.
	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		const float RequestedRemovalAmount = RequestedRemovalAmounts[CellIndex];
		if (!FMath::IsFinite(RequestedRemovalAmount)
			|| RequestedRemovalAmount < 0.0f)
		{
			return false;
		}
		if (RequestedRemovalAmount <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const float TargetTransferred = CalculateTargetTransferred(
			GetSafeCurrentTransferred(
				InOutSurfaceCells[CellIndex].TransferredContamination),
			RequestedRemovalAmount);
		if (!FMath::IsFinite(TargetTransferred)
			|| TargetTransferred > 1.0f)
		{
			return false;
		}
	}

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		const float RequestedRemovalAmount = RequestedRemovalAmounts[CellIndex];
		if (RequestedRemovalAmount <= UE_SMALL_NUMBER)
		{
			continue;
		}

		FMD_ScrapeSurfaceCell& Cell = InOutSurfaceCells[CellIndex];
		const float PreviousTransferred =
			GetSafeCurrentTransferred(Cell.TransferredContamination);
		const float TargetTransferred = CalculateTargetTransferred(
			PreviousTransferred,
			RequestedRemovalAmount);
		if (TargetTransferred <= PreviousTransferred + UE_SMALL_NUMBER)
		{
			continue;
		}

		Cell.Normalize();
		Cell.TransferredContamination = TargetTransferred;
		Cell.TransferredPaintPatternDensity = 1.0f;

		FMD_ScrapeCellChange& Change = OutCellChanges.AddDefaulted_GetRef();
		Change.CellIndex = CellIndex;
		Change.GridCoordinate = FIntPoint(
			CellIndex % GridDimensions.X,
			CellIndex / GridDimensions.X);
		Change.RemovedAmount = 0.0f;
		Change.RemainingAmountAfter = Cell.RemainingContamination;
		Change.bBecameClean = false;

		OutAddedContaminationAmount +=
			TargetTransferred - PreviousTransferred;
	}

	return true;
}

float FMD_ScrapeEvaluator::ApplyContaminationDabToGrid(
	const FMD_ScrapeContaminationDab& Dab,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const float StampAmount,
	const float StampDepthCm,
	const TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
	TArray<FMD_ScrapeCellChange>& OutCellChanges)
{
	OutCellChanges.Reset();
	const int64 ExpectedCellCount =
		static_cast<int64>(GridDimensions.X)
		* static_cast<int64>(GridDimensions.Y);
	FResolvedBladeBasis BladeBasis;
	if (!Dab.Identity.IsValid()
		|| !IsFiniteVector2D(Dab.PositionCm)
		|| ResolveBladeBasis(Dab.BladeBasis, BladeBasis)
			!= EBladeBasisResolveState::Valid
		|| !ToolSpec.IsValid()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| ExpectedCellCount > MAX_int32
		|| InOutSurfaceCells.Num() != ExpectedCellCount
		|| !IsFiniteVector2D(SurfaceSizeCm)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StampAmount)
		|| StampAmount <= 0.0f
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float SafeStampAmount = FMath::Clamp(StampAmount, 0.0f, 1.0f);
	const double BladeHalfWidthCm =
		static_cast<double>(ToolSpec.BladeWidthCm) * 0.5;
	const double BladeHalfDepthCm =
		static_cast<double>(StampDepthCm) * 0.5;
	const FVector2D CellSizeCm(
		SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),
		SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));
	const FVector2D SurfaceMinimumCm = SurfaceSizeCm * -0.5;
	const double CellHalfDiagonalCm = 0.5 * FMath::Sqrt(
		FMath::Square(CellSizeCm.X) + FMath::Square(CellSizeCm.Y));

	FVector2D MaximumInfluenceExtentCm;
	if (ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle)
	{
		const double InfluenceRadiusCm = BladeHalfWidthCm + CellHalfDiagonalCm;
		MaximumInfluenceExtentCm = FVector2D(InfluenceRadiusCm, InfluenceRadiusCm);
	}
	else
	{
		MaximumInfluenceExtentCm = FVector2D(
			FMath::Abs(BladeBasis.WidthAxis.X) * BladeHalfWidthCm
				+ FMath::Abs(BladeBasis.DepthAxis.X) * BladeHalfDepthCm
				+ CellHalfDiagonalCm,
			FMath::Abs(BladeBasis.WidthAxis.Y) * BladeHalfWidthCm
				+ FMath::Abs(BladeBasis.DepthAxis.Y) * BladeHalfDepthCm
				+ CellHalfDiagonalCm);
	}

	const int32 UnclampedMinimumX = FMath::FloorToInt(
		(Dab.PositionCm.X - MaximumInfluenceExtentCm.X - SurfaceMinimumCm.X)
		/ CellSizeCm.X);
	const int32 UnclampedMaximumX = FMath::FloorToInt(
		(Dab.PositionCm.X + MaximumInfluenceExtentCm.X - SurfaceMinimumCm.X)
		/ CellSizeCm.X);
	const int32 UnclampedMinimumY = FMath::FloorToInt(
		(Dab.PositionCm.Y - MaximumInfluenceExtentCm.Y - SurfaceMinimumCm.Y)
		/ CellSizeCm.Y);
	const int32 UnclampedMaximumY = FMath::FloorToInt(
		(Dab.PositionCm.Y + MaximumInfluenceExtentCm.Y - SurfaceMinimumCm.Y)
		/ CellSizeCm.Y);
	if (UnclampedMaximumX < 0
		|| UnclampedMinimumX >= GridDimensions.X
		|| UnclampedMaximumY < 0
		|| UnclampedMinimumY >= GridDimensions.Y)
	{
		return 0.0f;
	}

	FVector2D CanonicalBladeWidthAxis = BladeBasis.WidthAxis;
	if (CanonicalBladeWidthAxis.X < -UE_SMALL_NUMBER
		|| (FMath::Abs(CanonicalBladeWidthAxis.X) <= UE_SMALL_NUMBER
			&& CanonicalBladeWidthAxis.Y < 0.0))
	{
		CanonicalBladeWidthAxis *= -1.0;
	}

	const int32 MinimumX = FMath::Clamp(
		UnclampedMinimumX, 0, GridDimensions.X - 1);
	const int32 MaximumX = FMath::Clamp(
		UnclampedMaximumX, 0, GridDimensions.X - 1);
	const int32 MinimumY = FMath::Clamp(
		UnclampedMinimumY, 0, GridDimensions.Y - 1);
	const int32 MaximumY = FMath::Clamp(
		UnclampedMaximumY, 0, GridDimensions.Y - 1);
	float AddedContaminationAmount = 0.0f;
	for (int32 CellY = MinimumY; CellY <= MaximumY; ++CellY)
	{
		for (int32 CellX = MinimumX; CellX <= MaximumX; ++CellX)
		{
			const int32 CellIndex = CellX + CellY * GridDimensions.X;
			const FVector2D CellCenterCm(
				SurfaceMinimumCm.X
					+ (static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
				SurfaceMinimumCm.Y
					+ (static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);
			const FVector2D BrushToCellCenterCm =
				CellCenterCm - Dab.PositionCm;

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
					BladeBasis.WidthAxis,
					BladeBasis.DepthAxis,
					BladeHalfWidthCm,
					BladeHalfDepthCm,
					CellHalfDiagonalCm);
			}
			if (CoverageWeight <= UE_SMALL_NUMBER)
			{
				continue;
			}

			FMD_ScrapeSurfaceCell& Cell = InOutSurfaceCells[CellIndex];
			Cell.Normalize();
			const float PreviousTransferred = Cell.TransferredContamination;
			const float PreviousPatternDensity =
				Cell.TransferredPaintPatternDensity;
			const float TargetTransferred = FMath::Clamp(
				PreviousTransferred
					+ SafeStampAmount * static_cast<float>(CoverageWeight),
				0.0f,
				1.0f);
			const float PaintPatternDensity =
				ToolSpec.BrushShape == EMD_ScrapeBrushShape::Rectangle
				? static_cast<float>(CalculateTransferredPaintPatternDensity(
					BrushToCellCenterCm,
					CanonicalBladeWidthAxis,
					BladeHalfWidthCm))
				: 1.0f;
			const float TargetPatternDensity = FMath::Max(
				PreviousPatternDensity,
				PaintPatternDensity);
			if (TargetTransferred <= PreviousTransferred + UE_SMALL_NUMBER
				&& TargetPatternDensity
					<= PreviousPatternDensity + UE_SMALL_NUMBER)
			{
				continue;
			}

			Cell.TransferredContamination = TargetTransferred;
			Cell.TransferredPaintPatternDensity = TargetPatternDensity;
			AddedContaminationAmount += TargetTransferred - PreviousTransferred;

			FMD_ScrapeCellChange& Change = OutCellChanges.AddDefaulted_GetRef();
			Change.CellIndex = CellIndex;
			Change.GridCoordinate = FIntPoint(CellX, CellY);
			Change.RemovedAmount = 0.0f;
			Change.RemainingAmountAfter = Cell.RemainingContamination;
			Change.bBecameClean = false;
		}
	}

	return AddedContaminationAmount;
}

double FMD_ScrapeEvaluator::CalculateContaminationPaintProtectionDistanceCm(
	const FMD_ScrapeToolSpec& ToolSpec,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const float StampDepthCm)
{
	if (!ToolSpec.IsValid()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| !IsFiniteVector2D(SurfaceSizeCm)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER)
	{
		return 0.0;
	}

	const FVector2D CellSizeCm(
		SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),
		SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));
	const double CellHalfDiagonalCm = 0.5 * FMath::Sqrt(
		FMath::Square(CellSizeCm.X) + FMath::Square(CellSizeCm.Y));
	const double RemovalBoundingRadiusCm = 0.5 * FMath::Sqrt(
		FMath::Square(static_cast<double>(ToolSpec.BladeWidthCm))
		+ FMath::Square(static_cast<double>(ToolSpec.BladeDepthCm)));
	return RemovalBoundingRadiusCm
		+ static_cast<double>(StampDepthCm) * 0.5
		+ CellHalfDiagonalCm * 2.0;
}

bool FMD_ScrapeEvaluator::ShouldSelectContaminationDab(
	const FMD_ScrapeContaminationStampIdentity& Identity,
	const float StampChance,
	const int32 PatternSeed)
{
	if (!Identity.IsValid()
		|| !FMath::IsFinite(StampChance)
		|| StampChance <= 0.0f)
	{
		return false;
	}
	const int32 IdentitySeed = BuildContaminationPaintIdentitySeed(
		Identity,
		PatternSeed);
	const double SelectionSample = ContaminationPaintHashToUnitDouble(
		BuildContaminationPaintHash(
			IdentitySeed,
			Identity.DistanceBin,
			0U));
	return SelectionSample
		< static_cast<double>(FMath::Clamp(StampChance, 0.0f, 1.0f));
}

double FMD_ScrapeEvaluator::CalculateContaminationDabArcLengthCm(
	const FMD_ScrapeContaminationStampIdentity& Identity,
	const float StampDepthCm,
	const float StampSpacingCm,
	const int32 PatternSeed)
{
	if (!Identity.IsValid()
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StampSpacingCm)
		|| StampSpacingCm <= UE_SMALL_NUMBER)
	{
		return -1.0;
	}

	const double SafeStampSpacingCm = static_cast<double>(StampSpacingCm);
	const int32 IdentitySeed = BuildContaminationPaintIdentitySeed(
		Identity,
		PatternSeed);
	const double JitterSample = ContaminationPaintHashToUnitDouble(
		BuildContaminationPaintHash(
			IdentitySeed,
			Identity.DistanceBin,
			1U));
	const double MaximumPositionJitterCm = FMath::Max(
		0.0,
		FMath::Min(
			SafeStampSpacingCm * 0.3,
			(SafeStampSpacingCm - static_cast<double>(StampDepthCm)) * 0.5));
	const double PositionJitterCm =
		(JitterSample * 2.0 - 1.0) * MaximumPositionJitterCm;
	return (static_cast<double>(Identity.DistanceBin) + 0.5)
		* SafeStampSpacingCm
		+ PositionJitterCm;
}

bool FMD_ScrapeEvaluator::TryBuildProtectedContaminationPaintStroke(
	const FMD_ScrapeStroke& Stroke,
	const FVector2D& PathStartPositionCm,
	const FVector2D& PathAxis,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const float StampDepthCm,
	FMD_ScrapeStroke& OutPaintStroke)
{
	OutPaintStroke = Stroke;
	const auto IsFiniteVector = [](const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	};

	if (!IsFiniteVector(Stroke.StartPositionCm)
		|| !IsFiniteVector(Stroke.EndPositionCm)
		|| !IsFiniteVector(PathStartPositionCm)
		|| !IsFiniteVector(PathAxis)
		|| !ToolSpec.IsValid()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| !IsFiniteVector(SurfaceSizeCm)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D StrokeDelta =
		Stroke.EndPositionCm - Stroke.StartPositionCm;
	FVector2D SafePathAxis = PathAxis.GetSafeNormal();
	if (SafePathAxis.IsNearlyZero())
	{
		SafePathAxis = StrokeDelta.GetSafeNormal();
	}
	if (SafePathAxis.IsNearlyZero())
	{
		return false;
	}

	// LockedStrokeAxis는 축만 나타내므로, 시작점에서 실제 진행하는 부호를 붙인다.
	if (FVector2D::DotProduct(
			Stroke.EndPositionCm - PathStartPositionCm,
			SafePathAxis) < 0.0)
	{
		SafePathAxis *= -1.0;
	}

	const double StartTravelCm = FVector2D::DotProduct(
		Stroke.StartPositionCm - PathStartPositionCm,
		SafePathAxis);
	const double EndTravelCm = FVector2D::DotProduct(
		Stroke.EndPositionCm - PathStartPositionCm,
		SafePathAxis);
	if (!FMath::IsFinite(StartTravelCm)
		|| !FMath::IsFinite(EndTravelCm)
		|| EndTravelCm <= StartTravelCm + UE_SMALL_NUMBER)
	{
		return false;
	}

	// 다음 segment가 수직 drift로 기울어도 안전하도록 회전 전체를
	// 감싸는 반대각선과 기존 2×cell half diagonal 여유를 유지한다.
	const double PaintProtectionDistanceCm =
		CalculateContaminationPaintProtectionDistanceCm(
			ToolSpec,
			GridDimensions,
			SurfaceSizeCm,
			StampDepthCm);
	const double PaintStartTravelCm = FMath::Max(
		0.0,
		StartTravelCm - PaintProtectionDistanceCm);
	const double PaintEndTravelCm =
		EndTravelCm - PaintProtectionDistanceCm;
	if (PaintEndTravelCm
		<= PaintStartTravelCm + UE_SMALL_NUMBER)
	{
		return false;
	}

	OutPaintStroke.StartPositionCm =
		PathStartPositionCm + SafePathAxis * PaintStartTravelCm;
	OutPaintStroke.EndPositionCm =
		PathStartPositionCm + SafePathAxis * PaintEndTravelCm;
	return IsFiniteVector(OutPaintStroke.StartPositionCm)
		&& IsFiniteVector(OutPaintStroke.EndPositionCm);
}

bool FMD_ScrapeEvaluator::TryBuildPendingContaminationPaintStroke(
	const FMD_ScrapeStroke& FullPathStroke,
	const FVector2D& PathAxis,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const float StampDepthCm,
	FMD_ScrapeStroke& OutPaintStroke)
{
	OutPaintStroke = FullPathStroke;
	const auto IsFiniteVector = [](const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	};
	if (!IsFiniteVector(FullPathStroke.StartPositionCm)
		|| !IsFiniteVector(FullPathStroke.EndPositionCm)
		|| !IsFiniteVector(PathAxis)
		|| !ToolSpec.IsValid()
		|| GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| !IsFiniteVector(SurfaceSizeCm)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D FullPathDelta =
		FullPathStroke.EndPositionCm - FullPathStroke.StartPositionCm;
	FVector2D SafePathAxis = PathAxis.GetSafeNormal();
	if (SafePathAxis.IsNearlyZero())
	{
		SafePathAxis = FullPathDelta.GetSafeNormal();
	}
	if (SafePathAxis.IsNearlyZero())
	{
		return false;
	}
	if (FVector2D::DotProduct(FullPathDelta, SafePathAxis) < 0.0)
	{
		SafePathAxis *= -1.0;
	}

	const double EndTravelCm = FVector2D::DotProduct(
		FullPathDelta,
		SafePathAxis);
	if (!FMath::IsFinite(EndTravelCm)
		|| EndTravelCm <= UE_SMALL_NUMBER)
	{
		return false;
	}

	FMD_ScrapeStroke ProjectedFullPathStroke = FullPathStroke;
	ProjectedFullPathStroke.EndPositionCm =
		FullPathStroke.StartPositionCm + SafePathAxis * EndTravelCm;
	FMD_ScrapeStroke ProtectedPaintStroke;
	if (TryBuildProtectedContaminationPaintStroke(
			ProjectedFullPathStroke,
			FullPathStroke.StartPositionCm,
			SafePathAxis,
			ToolSpec,
			GridDimensions,
			SurfaceSizeCm,
			StampDepthCm,
			ProtectedPaintStroke))
	{
		OutPaintStroke.StartPositionCm =
			ProtectedPaintStroke.EndPositionCm;
	}
	else
	{
		OutPaintStroke.StartPositionCm =
			FullPathStroke.StartPositionCm;
	}
	OutPaintStroke.EndPositionCm =
		ProjectedFullPathStroke.EndPositionCm;
	return FVector2D::Distance(
			OutPaintStroke.StartPositionCm,
			OutPaintStroke.EndPositionCm) > UE_SMALL_NUMBER;
}

float FMD_ScrapeEvaluator::ApplyContaminationPaintStrokeToGrid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const float StampAmount,
	const float StampChance,
	const float StampDepthCm,
	const float StampSpacingCm,
	const int32 PatternSeed,
	const TArrayView<FMD_ScrapeSurfaceCell> InOutSurfaceCells,
	TArray<FMD_ScrapeCellChange>& OutCellChanges)
{
	OutCellChanges.Reset();

	const int64 ExpectedCellCount =
		static_cast<int64>(GridDimensions.X)
		* static_cast<int64>(GridDimensions.Y);
	const FVector2D StrokeDelta =
		Stroke.EndPositionCm - Stroke.StartPositionCm;
	const double StrokeDistanceCm = StrokeDelta.Size();
	if (GridDimensions.X <= 0
		|| GridDimensions.Y <= 0
		|| ExpectedCellCount > MAX_int32
		|| InOutSurfaceCells.Num() != ExpectedCellCount
		|| !FMath::IsFinite(SurfaceSizeCm.X)
		|| !FMath::IsFinite(SurfaceSizeCm.Y)
		|| SurfaceSizeCm.X <= UE_SMALL_NUMBER
		|| SurfaceSizeCm.Y <= UE_SMALL_NUMBER
		|| !ToolSpec.IsValid()
		|| !FMath::IsFinite(StampAmount)
		|| StampAmount <= 0.0f
		|| !FMath::IsFinite(StampChance)
		|| StampChance <= 0.0f
		|| !FMath::IsFinite(StampDepthCm)
		|| StampDepthCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StampSpacingCm)
		|| StampSpacingCm <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(StrokeDistanceCm)
		|| StrokeDistanceCm <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float SafeStampAmount = FMath::Clamp(StampAmount, 0.0f, 1.0f);
	const float SafeStampChance = FMath::Clamp(StampChance, 0.0f, 1.0f);
	const double SafeStampSpacingCm = static_cast<double>(StampSpacingCm);
	FResolvedStrokeBladeBasis StrokeBladeBasis;
	if (!TryResolveStrokeBladeBasis(
			Stroke,
			StrokeDelta / StrokeDistanceCm,
			StrokeBladeBasis))
	{
		return 0.0f;
	}
	const FResolvedBladeBasis MidpointBladeBasis = InterpolateBladeBasis(
		StrokeBladeBasis,
		0.5);
	const FVector2D BladeDepthAxis = MidpointBladeBasis.DepthAxis;
	const FVector2D BladeWidthAxis = MidpointBladeBasis.WidthAxis;
	// TASK 5 separates signed travel from the 180-degree-equivalent blade basis.
	// This straight-path compatibility API still bins along the actual stroke;
	// the explicit basis controls only the shape and orientation of each dab.
	const FVector2D TravelAxis = StrokeDelta / StrokeDistanceCm;
	const FVector2D TravelWidthAxis(-TravelAxis.Y, TravelAxis.X);
	FVector2D CanonicalBladeWidthAxis = BladeWidthAxis;
	if (CanonicalBladeWidthAxis.X < -UE_SMALL_NUMBER
		|| (FMath::Abs(CanonicalBladeWidthAxis.X) <= UE_SMALL_NUMBER
			&& CanonicalBladeWidthAxis.Y < 0.0))
	{
		CanonicalBladeWidthAxis *= -1.0;
	}
	const double BladeHalfWidthCm =
		static_cast<double>(ToolSpec.BladeWidthCm) * 0.5;
	const double BladeHalfDepthCm =
		static_cast<double>(StampDepthCm) * 0.5;
	const FVector2D CellSizeCm(
		SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),
		SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));
	const FVector2D SurfaceMinimumCm = SurfaceSizeCm * -0.5;
	const double CellHalfDiagonalCm = 0.5 * FMath::Sqrt(
		FMath::Square(CellSizeCm.X) + FMath::Square(CellSizeCm.Y));

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

	TArray<float> TransferredBeforePaint;
	TransferredBeforePaint.SetNumUninitialized(InOutSurfaceCells.Num());
	TArray<float> PatternDensityBeforePaint;
	PatternDensityBeforePaint.SetNumUninitialized(InOutSurfaceCells.Num());
	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		FMD_ScrapeSurfaceCell& Cell = InOutSurfaceCells[CellIndex];
		Cell.Normalize();
		TransferredBeforePaint[CellIndex] = Cell.TransferredContamination;
		PatternDensityBeforePaint[CellIndex] =
			Cell.TransferredPaintPatternDensity;
	}

	const double StartCoordinate = FVector2D::DotProduct(
		Stroke.StartPositionCm,
		TravelAxis);
	const double EndCoordinate = FVector2D::DotProduct(
		Stroke.EndPositionCm,
		TravelAxis);
	const double WidthCoordinate = FVector2D::DotProduct(
		Stroke.StartPositionCm,
		TravelWidthAxis);
	const int32 FirstCandidateBin =
		FMath::FloorToInt(StartCoordinate / SafeStampSpacingCm) - 1;
	const int32 LastCandidateBin =
		FMath::FloorToInt(EndCoordinate / SafeStampSpacingCm) + 1;
	float AddedContaminationAmount = 0.0f;

	for (int32 DabBinIndex = FirstCandidateBin;
		DabBinIndex <= LastCandidateBin;
		++DabBinIndex)
	{
		const double SelectionSample = ContaminationPaintHashToUnitDouble(
			BuildContaminationPaintHash(
				PatternSeed,
				DabBinIndex,
				0U));
		const bool bShouldPaintDab =
			SelectionSample < static_cast<double>(SafeStampChance);
		const double JitterSample = ContaminationPaintHashToUnitDouble(
			BuildContaminationPaintHash(
				PatternSeed,
				DabBinIndex,
				1U));
		const double MaximumPositionJitterCm = FMath::Max(
			0.0,
			FMath::Min(
				SafeStampSpacingCm * 0.3,
				(SafeStampSpacingCm - BladeHalfDepthCm * 2.0) * 0.5));
		const double PositionJitterCm =
			(JitterSample * 2.0 - 1.0) * MaximumPositionJitterCm;
		const double DabCoordinate =
			(static_cast<double>(DabBinIndex) + 0.5)
				* SafeStampSpacingCm
			+ PositionJitterCm;

		// 시작점은 제외하고 끝점은 포함해 연속 프레임 경계에서 같은 도장을 두 번 찍지 않는다.
		if (!bShouldPaintDab
			|| DabCoordinate <= StartCoordinate + UE_KINDA_SMALL_NUMBER
			|| DabCoordinate > EndCoordinate + UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D DabPositionCm =
			TravelAxis * DabCoordinate
			+ TravelWidthAxis * WidthCoordinate;
		const int32 UnclampedMinimumX = FMath::FloorToInt(
			(DabPositionCm.X - MaximumInfluenceExtentCm.X - SurfaceMinimumCm.X)
			/ CellSizeCm.X);
		const int32 UnclampedMaximumX = FMath::FloorToInt(
			(DabPositionCm.X + MaximumInfluenceExtentCm.X - SurfaceMinimumCm.X)
			/ CellSizeCm.X);
		const int32 UnclampedMinimumY = FMath::FloorToInt(
			(DabPositionCm.Y - MaximumInfluenceExtentCm.Y - SurfaceMinimumCm.Y)
			/ CellSizeCm.Y);
		const int32 UnclampedMaximumY = FMath::FloorToInt(
			(DabPositionCm.Y + MaximumInfluenceExtentCm.Y - SurfaceMinimumCm.Y)
			/ CellSizeCm.Y);
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

		for (int32 CellY = MinimumY; CellY <= MaximumY; ++CellY)
		{
			for (int32 CellX = MinimumX; CellX <= MaximumX; ++CellX)
			{
				const int32 CellIndex = CellX + CellY * GridDimensions.X;
				const FVector2D CellCenterCm(
					SurfaceMinimumCm.X
						+ (static_cast<double>(CellX) + 0.5) * CellSizeCm.X,
					SurfaceMinimumCm.Y
						+ (static_cast<double>(CellY) + 0.5) * CellSizeCm.Y);
				const FVector2D BrushToCellCenterCm =
					CellCenterCm - DabPositionCm;

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
				if (CoverageWeight <= UE_SMALL_NUMBER)
				{
					continue;
				}

				FMD_ScrapeSurfaceCell& Cell = InOutSurfaceCells[CellIndex];
				const float PreviousTransferred =
					Cell.TransferredContamination;
				const float TargetTransferred = FMath::Clamp(
					PreviousTransferred
						+ SafeStampAmount * static_cast<float>(CoverageWeight),
					0.0f,
					1.0f);
				const float PaintPatternDensity =
					ToolSpec.BrushShape == EMD_ScrapeBrushShape::Rectangle
					? static_cast<float>(CalculateTransferredPaintPatternDensity(
						BrushToCellCenterCm,
						CanonicalBladeWidthAxis,
						BladeHalfWidthCm))
					: 1.0f;
				const float TargetPatternDensity = FMath::Max(
					Cell.TransferredPaintPatternDensity,
					PaintPatternDensity);
				if (TargetTransferred <= PreviousTransferred + UE_SMALL_NUMBER
					&& TargetPatternDensity
						<= Cell.TransferredPaintPatternDensity + UE_SMALL_NUMBER)
				{
					continue;
				}

				Cell.TransferredContamination = TargetTransferred;
				Cell.TransferredPaintPatternDensity = TargetPatternDensity;
				AddedContaminationAmount +=
					TargetTransferred - PreviousTransferred;
			}
		}
	}

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		const FMD_ScrapeSurfaceCell& Cell = InOutSurfaceCells[CellIndex];
		if (FMath::IsNearlyEqual(
				Cell.TransferredContamination,
				TransferredBeforePaint[CellIndex],
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				Cell.TransferredPaintPatternDensity,
				PatternDensityBeforePaint[CellIndex],
				UE_SMALL_NUMBER))
		{
			continue;
		}

		FMD_ScrapeCellChange& Change = OutCellChanges.AddDefaulted_GetRef();
		Change.CellIndex = CellIndex;
		Change.GridCoordinate = FIntPoint(
			CellIndex % GridDimensions.X,
			CellIndex / GridDimensions.X);
		Change.RemovedAmount = 0.0f;
		Change.RemainingAmountAfter = Cell.RemainingContamination;
		Change.bBecameClean = false;
	}

	return AddedContaminationAmount;
}

bool FMD_ScrapeEvaluator::IsEvaluationInputValid(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeSettings& Settings,
	const FIntPoint& GridDimensions,
	const FVector2D& SurfaceSizeCm,
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells)
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
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells)
{
	double InitialContaminationAmount = 0.0;
	double CurrentContaminationAmount = 0.0;
	return CalculateCurrentCleanRatio(
		SurfaceCells,
		InitialContaminationAmount,
		CurrentContaminationAmount);
}

float FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
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
