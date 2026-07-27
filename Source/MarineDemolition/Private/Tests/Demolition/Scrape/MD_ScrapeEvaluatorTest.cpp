// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Demolition/Scrape/MD_ScrapeEvaluator.h"
#include "Misc/AutomationTest.h"

namespace MD_ScrapeEvaluatorTest
{
	constexpr int32 GridWidth = 10;
	constexpr int32 GridHeight = 10;

	TArray<FMD_ScrapeSurfaceCell> MakeFullyContaminatedGrid()
	{
		TArray<FMD_ScrapeSurfaceCell> Cells;
		Cells.SetNum(GridWidth * GridHeight);

		for (FMD_ScrapeSurfaceCell& Cell : Cells)
		{
			Cell.InitialContamination = 1.0f;
			Cell.RemainingContamination = 1.0f;
		}

		return Cells;
	}

	FMD_ScrapeSettings MakeSettings()
	{
		FMD_ScrapeSettings Settings;
		Settings.BladeWidthCm = 10.0f;
		Settings.RemovalPerTravelCentimeter = 0.02f;
		Settings.SampleSpacingRatioOfSmallestCell = 0.5f;
		Settings.MaximumSampleSpacingCm = 2.5f;
		Settings.MinimumTravelDistanceCm = 0.1f;
		Settings.CleanTolerance = 0.001f;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_ScrapeRemovesOnlyTouchedCellsTest,
	"MarineDemolition.Scrape.Evaluator.RemovesOnlyTouchedCells",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FMD_ScrapeRemovesOnlyTouchedCellsTest::RunTest(
	const FString& Parameters)
{
	using namespace MD_ScrapeEvaluatorTest;

	TArray<FMD_ScrapeSurfaceCell> Cells =
		MakeFullyContaminatedGrid();

	FMD_ScrapeStroke Stroke;
	Stroke.StartPositionCm = FVector2D(-20.0, 0.0);
	Stroke.EndPositionCm = FVector2D(20.0, 0.0);

	const FMD_ScrapeResult Result =
		FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			Stroke,
			MakeSettings(),
			FIntPoint(GridWidth, GridHeight),
			FVector2D(100.0, 100.0),
			Cells);

	TestTrue(TEXT("입력은 유효해야 한다."), Result.bIsInputValid);
	TestTrue(TEXT("이동한 스트로크로 판정해야 한다."), Result.bWasStrokeLongEnough);
	TestTrue(TEXT("표면 셀과 접촉해야 한다."), Result.bHadSurfaceContact);
	TestTrue(TEXT("일부 셀의 오염량이 감소해야 한다."), Result.ModifiedCellCount > 0);
	TestTrue(
		TEXT("긁지 않은 셀은 그대로 남아야 한다."),
		Result.ModifiedCellCount < Cells.Num());
	TestTrue(
		TEXT("전체 제거율은 증가해야 한다."),
		Result.CleanRatioAfter > Result.CleanRatioBefore);
	TestEqual(
		TEXT("변경 셀 수와 변경 목록 수가 일치해야 한다."),
		Result.ModifiedCellCount,
		Result.CellChanges.Num());

	int32 UnchangedCellCount = 0;
	for (const FMD_ScrapeSurfaceCell& Cell : Cells)
	{
		if (FMath::IsNearlyEqual(Cell.RemainingContamination, 1.0f))
		{
			++UnchangedCellCount;
		}
	}

	TestTrue(
		TEXT("경로 밖에는 완전히 오염된 셀이 남아야 한다."),
		UnchangedCellCount > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_ScrapeStationaryInputTest,
	"MarineDemolition.Scrape.Evaluator.StationaryInputDoesNotRemove",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FMD_ScrapeStationaryInputTest::RunTest(const FString& Parameters)
{
	using namespace MD_ScrapeEvaluatorTest;

	TArray<FMD_ScrapeSurfaceCell> Cells =
		MakeFullyContaminatedGrid();

	FMD_ScrapeStroke Stroke;
	Stroke.StartPositionCm = FVector2D(0.0, 0.0);
	Stroke.EndPositionCm = Stroke.StartPositionCm;

	const FMD_ScrapeResult Result =
		FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			Stroke,
			MakeSettings(),
			FIntPoint(GridWidth, GridHeight),
			FVector2D(100.0, 100.0),
			Cells);

	TestTrue(TEXT("정지 입력도 설정 자체는 유효해야 한다."), Result.bIsInputValid);
	TestFalse(
		TEXT("최소 이동 거리 미만은 스트로크로 처리하지 않아야 한다."),
		Result.bWasStrokeLongEnough);
	TestEqual(
		TEXT("정지 상태에서 변경된 셀이 없어야 한다."),
		Result.ModifiedCellCount,
		0);
	TestTrue(
		TEXT("정지 상태에서 제거율이 바뀌지 않아야 한다."),
		FMath::IsNearlyZero(Result.CleanRatioAfter));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_ScrapeDistanceDeterminismTest,
	"MarineDemolition.Scrape.Evaluator.DistanceBasedDeterminism",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FMD_ScrapeDistanceDeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace MD_ScrapeEvaluatorTest;

	TArray<FMD_ScrapeSurfaceCell> SingleStrokeCells =
		MakeFullyContaminatedGrid();
	TArray<FMD_ScrapeSurfaceCell> SplitStrokeCells =
		MakeFullyContaminatedGrid();
	const FMD_ScrapeSettings Settings = MakeSettings();
	const FIntPoint GridDimensions(GridWidth, GridHeight);
	const FVector2D SurfaceSizeCm(100.0, 100.0);

	FMD_ScrapeStroke SingleStroke;
	SingleStroke.StartPositionCm = FVector2D(-20.0, 0.0);
	SingleStroke.EndPositionCm = FVector2D(20.0, 0.0);
	const FMD_ScrapeResult SingleResult =
		FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			SingleStroke,
			Settings,
			GridDimensions,
			SurfaceSizeCm,
			SingleStrokeCells);

	FMD_ScrapeStroke FirstHalf;
	FirstHalf.StartPositionCm = FVector2D(-20.0, 0.0);
	FirstHalf.EndPositionCm = FVector2D(0.0, 0.0);
	FMD_ScrapeEvaluator::ApplyStrokeToGrid(
		FirstHalf,
		Settings,
		GridDimensions,
		SurfaceSizeCm,
		SplitStrokeCells);

	FMD_ScrapeStroke SecondHalf;
	SecondHalf.StartPositionCm = FVector2D(0.0, 0.0);
	SecondHalf.EndPositionCm = FVector2D(20.0, 0.0);
	const FMD_ScrapeResult SplitResult =
		FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			SecondHalf,
			Settings,
			GridDimensions,
			SurfaceSizeCm,
			SplitStrokeCells);

	TestTrue(
		TEXT("같은 총 이동 거리는 프레임 분할과 무관하게 같은 제거율이어야 한다."),
		FMath::IsNearlyEqual(
			SingleResult.CleanRatioAfter,
			SplitResult.CleanRatioAfter,
			UE_KINDA_SMALL_NUMBER));

	for (int32 CellIndex = 0;
		CellIndex < SingleStrokeCells.Num();
		++CellIndex)
	{
		if (!FMath::IsNearlyEqual(
				SingleStrokeCells[CellIndex].RemainingContamination,
				SplitStrokeCells[CellIndex].RemainingContamination,
				UE_KINDA_SMALL_NUMBER))
		{
			AddError(
				FString::Printf(
					TEXT("분할 여부에 따라 셀 %d의 잔여 오염량이 달라졌습니다."),
					CellIndex));
			break;
		}
	}

	return true;
}

#endif
