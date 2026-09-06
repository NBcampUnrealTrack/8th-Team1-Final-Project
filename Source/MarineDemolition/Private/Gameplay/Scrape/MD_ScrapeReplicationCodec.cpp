#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"

#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

bool FMD_ScrapeReplicationCodec::TryEncodeCellIndex(
	const int32 CellIndex,
	uint32& OutEncodedCellIndex)
{
	OutEncodedCellIndex = InvalidCellIndex;
	if (CellIndex < 0
		|| CellIndex >= FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount)
	{
		return false;
	}

	OutEncodedCellIndex = static_cast<uint32>(CellIndex);
	return true;
}

int32 FMD_ScrapeReplicationCodec::DecodeCellIndex(
	const uint32 EncodedCellIndex)
{
	return EncodedCellIndex >= static_cast<uint32>(
		FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount)
		? INDEX_NONE
		: static_cast<int32>(EncodedCellIndex);
}

uint8 FMD_ScrapeReplicationCodec::CalculateMinimumContaminatedStep(
	const float CleanTolerance)
{
	const float SafeTolerance = FMath::IsFinite(CleanTolerance)
		? FMath::Clamp(CleanTolerance, 0.0f, 1.0f)
		: 0.0f;

	// Step / 255 > SafeTolerance 를 만족하는 가장 작은 Step.
	const int32 MinimumStep = FMath::FloorToInt32(
		SafeTolerance * static_cast<float>(QuantizationSteps)) + 1;
	return static_cast<uint8>(FMath::Clamp(
		MinimumStep,
		1,
		QuantizationSteps));
}

uint8 FMD_ScrapeReplicationCodec::QuantizeRemaining(
	const float RemainingContamination,
	const float CleanTolerance)
{
	if (!FMath::IsFinite(RemainingContamination))
	{
		return 0;
	}

	const float SafeRemaining = FMath::Clamp(
		RemainingContamination,
		0.0f,
		1.0f);
	const float SafeTolerance = FMath::IsFinite(CleanTolerance)
		? FMath::Clamp(CleanTolerance, 0.0f, 1.0f)
		: 0.0f;

	// 0은 깨끗함 전용이다. 깨끗한 값만 0이 될 수 있다.
	if (SafeRemaining <= SafeTolerance)
	{
		return 0;
	}

	const int32 RoundedStep = FMath::RoundToInt32(
		SafeRemaining * static_cast<float>(QuantizationSteps));
	// 반올림이 깨끗함 구간으로 내려가지 않게 최소 단계를 함께 적용한다.
	return static_cast<uint8>(FMath::Clamp(
		FMath::Max(
			RoundedStep,
			static_cast<int32>(
				CalculateMinimumContaminatedStep(SafeTolerance))),
		1,
		QuantizationSteps));
}

float FMD_ScrapeReplicationCodec::DequantizeRemaining(
	const uint8 QuantizedRemaining)
{
	return static_cast<float>(QuantizedRemaining)
		/ static_cast<float>(QuantizationSteps);
}
