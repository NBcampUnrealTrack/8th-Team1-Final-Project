#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 긁기 스트로크 적용 · 제거율 조회

using namespace MD_ScrapeSurfaceComponentInternal;

namespace
{
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

}

FMD_ScrapeResult UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithToolSpec(
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
		1.0f,
		1.0f,
		0,
		IgnoredRemovedCleanRatioDelta);
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
	if (bIsFullyRemoved && !bContaminationCompletionBroadcast)
	{
		bContaminationCompletionBroadcast = true;
		OnContaminationFullyRemoved.Broadcast(this);
	}
	else if (!bIsFullyRemoved)
	{
		bContaminationCompletionBroadcast = false;
	}
}


FMD_ScrapeResult
UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithContaminationStamp(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const float StampRatio,
	float& OutRemovedCleanRatioDelta)
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
		OutRemovedCleanRatioDelta);
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
	const bool bDeferCompletionBroadcast)
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

	FMD_ScrapeResult Result;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeEvaluate);
		Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
			Stroke,
			ToolSpec,
			ScrapeSettings,
			ContaminationGridDimensions,
			ResolvedContaminationSurfaceSizeCm,
			FaceCells);
	}

	// 잘못된 입력은 상태를 변경하지 않음
	if (!Result.bIsInputValid)
	{
		return Result;
	}
	OutRemovedCleanRatioDelta = Result.CleanRatioDelta;

	TArray<FMD_ScrapeCellChange> StampChanges;
	if (FMath::IsFinite(StampRatio)
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

		if (!StampChanges.IsEmpty())
		{
			// 제거량은 헤라 적재 계산용으로 보존하고, 진행도만 최종 도포 상태로 맞춘다.
			Result.CleanRatioAfter =
				FMD_ScrapeEvaluator::CalculateCleanRatio(FaceCells);
			Result.CleanRatioDelta = FMath::Max(
				0.0f,
				Result.CleanRatioAfter - Result.CleanRatioBefore);
			Result.bIsSurfaceFullyClean =
				FMD_ScrapeEvaluator::IsSurfaceFullyClean(
					FaceCells,
					ScrapeSettings.CleanTolerance);

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
						<= ScrapeSettings.CleanTolerance
					&& FaceCells[CellChange.CellIndex].TransferredContamination
						<= ScrapeSettings.CleanTolerance;
				if (CellChange.bBecameClean)
				{
					++Result.NewlyCleanedCellCount;
				}
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

	// 기획 02 §1 규칙 3: 긁어낸 오염물은 월드 물리 오브젝트로 떨어진다.
	// 긁기가 끝난 지점에 떨군다. 스트로크는 짧아서 시작점과 큰 차이가 없다.
	DropContaminationItems(
		Stroke.SurfaceFace,
		Result.RemovedContaminationAmount,
		Stroke.EndPositionCm);

	// 어느 한 면이 처음 완료되는 전환에서 한 번만 방송
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (!bDeferCompletionBroadcast
		&& !bWasFullyRemoved
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
	return bContaminationSurfaceInitialized
		&& !FaceCells.IsEmpty()
		&& FMD_ScrapeEvaluator::IsSurfaceFullyClean(
			FaceCells,
			ScrapeSettings.CleanTolerance);
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
