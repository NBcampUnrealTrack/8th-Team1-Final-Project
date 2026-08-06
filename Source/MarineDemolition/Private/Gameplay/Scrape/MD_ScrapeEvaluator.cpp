// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"

#include "Containers/BitArray.h"

#include "Util/MD_LogUtility.h"

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
	FMD_ScrapeResult Result;
	Result.SurfaceFace = Stroke.SurfaceFace;

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
	}

	Result.CleanRatioBefore = CalculateCleanRatio(InOutSurfaceCells);
	Result.CleanRatioAfter = Result.CleanRatioBefore;
	Result.bIsSurfaceFullyClean = IsSurfaceFullyClean(InOutSurfaceCells, Settings.CleanTolerance);

	const FVector2D StrokeDelta = Stroke.EndPositionCm - Stroke.StartPositionCm;
	const double StrokeDistanceCm = StrokeDelta.Size();

	// 이동 거리가 너무 짧으면 버튼을 누른 채 정지해 있는 입력이나 미세한 손 떨림으로 가정
	// 정상적인 플레이에서도 매 프레임 발생하는 상황이므로 로그를 남기지 않음
	if (StrokeDistanceCm <= static_cast<double>(UE_SMALL_NUMBER)
		|| StrokeDistanceCm < static_cast<double>(Settings.MinimumTravelDistanceCm))
	{
		return Result;
	}

	Result.bWasStrokeLongEnough = true;

	const FVector2D CellSizeCm(SurfaceSizeCm.X / static_cast<double>(GridDimensions.X),SurfaceSizeCm.Y / static_cast<double>(GridDimensions.Y));

	// 표면의 로컬 원점인 중앙을 맞추기 위해, 전체 크기의 절반이 음수로 이동한 위치가 좌하단 좌표
	const FVector2D SurfaceMinimumCm = SurfaceSizeCm * -0.5;

	const double SmallestCellSideCm = FMath::Min(CellSizeCm.X, CellSizeCm.Y);
	const double RatioBasedSpacingCm = SmallestCellSideCm * static_cast<double>(Settings.SampleSpacingRatioOfSmallestCell);
	const double BrushTravelSizeCm =
		ToolSpec.BrushShape == EMD_ScrapeBrushShape::Circle
		? static_cast<double>(ToolSpec.BladeWidthCm)
		: static_cast<double>(ToolSpec.BladeDepthCm);
	const double BrushBasedSpacingCm = BrushTravelSizeCm * 0.5;
	const double SampleSpacingCm = FMath::Min3(
		RatioBasedSpacingCm,
		static_cast<double>(Settings.MaximumSampleSpacingCm),
		BrushBasedSpacingCm);

	// 이동 거리를 샘플 최대 간격으로 나눈 뒤 올림하면 실제 간격은 항상 설정값 이하로 유지
	// 얇은 사각 헤라도 이동 방향으로 빈틈이 생기지 않도록 브러시 길이의 절반을 추가 상한으로 사용
	const int32 SampleCount = FMath::Max(1,FMath::CeilToInt(StrokeDistanceCm / SampleSpacingCm));
	const double TravelDistancePerSampleCm = StrokeDistanceCm / static_cast<double>(SampleCount);

	const double CellHalfDiagonalCm = 0.5 * FMath::Sqrt(FMath::Square(CellSizeCm.X) + FMath::Square(CellSizeCm.Y));
	const FVector2D BladeDepthAxis = StrokeDelta / StrokeDistanceCm;
	const FVector2D BladeWidthAxis(
		-BladeDepthAxis.Y,
		BladeDepthAxis.X);
	const double BladeHalfWidthCm =
		static_cast<double>(ToolSpec.BladeWidthCm) * 0.5;
	const double BladeHalfDepthCm =
		static_cast<double>(ToolSpec.BladeDepthCm) * 0.5;

	// 후보 셀 검색 범위를 브러시 형태의 표면 X/Y AABB로 계산
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

	// 동일 셀이 여러 샘플과 겹쳐도 TouchedCellCount는 한 번만 증가해야 하므로 비트 배열을 사용
	TBitArray<> TouchedCells(false, InOutSurfaceCells.Num());

	// 변경 내역과 "이번 스트로크로 처음 깨끗해졌는가"를 마지막에 정확히 계산하기 위해
	// 적용 전 잔여량을 별도 배열에 보관
	TArray<float> RemainingBeforeStroke;
	RemainingBeforeStroke.SetNumUninitialized(InOutSurfaceCells.Num());

	for (int32 CellIndex = 0; CellIndex < InOutSurfaceCells.Num(); ++CellIndex)
	{
		RemainingBeforeStroke[CellIndex] = InOutSurfaceCells[CellIndex].RemainingContamination;
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
				const double RemainingBeforeSample =
					static_cast<double>(Cell.RemainingContamination);

				// 제거량은 Tick 횟수나 DeltaTime이 아니라 실제 이동 거리 기준
				// 따라서 30fps와 120fps에서 같은 거리로 움직인 헤라는 거의 같은 결과
				const double RequestedRemovalAmount =
					static_cast<double>(ToolSpec.RemovalPerTravelCentimeter)
					* TravelDistancePerSampleCm
					* CoverageWeight;

				// 이미 깨끗한 셀에 작업량이 가해지면 모든 힘이 맨 벽면으로 전달
				if (RemainingBeforeSample
					<= static_cast<double>(Settings.CleanTolerance))
				{
					Result.bTouchedBareSurface = true;
					continue;
				}

				const double ActualRemovedAmount = FMath::Min(
					RemainingBeforeSample,
					RequestedRemovalAmount);

				Cell.RemainingContamination = static_cast<float>(
					FMath::Max(0.0, RemainingBeforeSample - ActualRemovedAmount));

				// 허용 오차보다 작은 찌꺼기는 0으로 스냅해 완료 판정과 시각 마스크를 일치
				if (Cell.RemainingContamination <= Settings.CleanTolerance)
				{
					Cell.RemainingContamination = 0.0f;
				}

				if (ActualRemovedAmount > UE_SMALL_NUMBER)
				{
					bRemovedAnyContamination = true;
				}

				// 요청 작업량이 실제 남은 양보다 컸다면 오염막을 다 벗긴 뒤에도 힘이 남은 상태
				// 그 여분의 작업량은 맨 벽면을 긁은 것으로 분류해 위험한 피드백의 근거로 기록
				if (RequestedRemovalAmount
					> RemainingBeforeSample
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

		const float RemovedAmount =
			RemainingBeforeStroke[CellIndex]
			- InOutSurfaceCells[CellIndex].RemainingContamination;

		// 부동소수점 연산 중 생길 수 있는 무의미한 극소 차이는 변경 내역에서 제외
		if (RemovedAmount <= UE_SMALL_NUMBER)
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
			InOutSurfaceCells[CellIndex].RemainingContamination;
		CellChange.bBecameClean =
			RemainingBeforeStroke[CellIndex] > Settings.CleanTolerance
			&& InOutSurfaceCells[CellIndex].RemainingContamination
				<= Settings.CleanTolerance;

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

bool FMD_ScrapeEvaluator::IsSurfaceFullyClean(
	const TConstArrayView<FMD_ScrapeSurfaceCell> SurfaceCells,
	const float CleanTolerance)
{
	const float SafeTolerance = FMath::IsFinite(CleanTolerance)
		? FMath::Max(0.0f, CleanTolerance)
		: 0.0f;

	for (const FMD_ScrapeSurfaceCell& Cell : SurfaceCells)
	{
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
