#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Components/StaticMeshComponent.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 긁기 스트로크 적용 · 제거율 조회

using namespace MD_ScrapeSurfaceComponentInternal;

namespace
{
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

}

FMD_ScrapeResult UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeWithToolSpec(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec)
{
	float IgnoredRemovedCleanRatioDelta = 0.0f;
	FMD_ScrapeResult Result = ApplyLocalScrapeStrokeInternal(
		Stroke,
		ToolSpec,
		0.0f,
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
		0.0f,
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
	return ApplyLocalScrapeStrokeInternal(
		Stroke,
		ToolSpec,
		StampRatio,
		OutRemovedCleanRatioDelta,
		bDeferCompletionBroadcast);
}


double UMD_ScrapeSurfaceComponent::GetScrapeSampleSpacingCm(
	const FMD_ScrapeToolSpec& ToolSpec) const
{
	if (UsesMeshUVSurface() && GetContaminationMeshUVData() && SurfaceMesh)
	{
		return FMath::Min3(GetContaminationMeshUVData()->TargetCellSizeCm
			* SurfaceMesh->GetComponentScale().GetAbsMin() * ScrapeSettings.SampleSpacingRatioOfSmallestCell,
			static_cast<double>(ScrapeSettings.MaximumSampleSpacingCm),
			0.5 * FMath::Min(ToolSpec.BladeWidthCm, ToolSpec.BladeDepthCm));
	}

	return FMD_ScrapeEvaluator::CalculateStrokeSampleSpacingCm(
		ToolSpec,
		ScrapeSettings,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm);
}


FMD_ScrapeResult UMD_ScrapeSurfaceComponent::ApplyLocalScrapeStrokeInternal(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const float StampRatio,
	float& OutRemovedCleanRatioDelta,
	const bool bDeferCompletionBroadcast)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeStrokeTotal);
	// Contact snapshots are released on every transaction exit.
	FMD_ScrapeEvaluationScratch LocalEvaluationScratch;
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

	const bool bMeshUVSurface = UsesMeshUVSurface();
	const float ConfiguredCleanTolerance = FMath::IsFinite(ScrapeSettings.CleanTolerance)
		? FMath::Max(0.0f, ScrapeSettings.CleanTolerance) : 0.0f;
	if (bMeshUVSurface && (!CachedMeshUVProgress.bIsValid
		|| CachedMeshUVProgress.UncleanCellCount < 0
		|| CachedMeshUVProgress.UncleanCellCountAtZero < 0
		|| CachedMeshUVProgress.CleanTolerance != ConfiguredCleanTolerance))
	{
		RefreshContaminationCleanRatioCache(EMD_ScrapeSurfaceFace::Front);
		if (!CachedMeshUVProgress.bIsValid)
		{
			FMD_ScrapeResult InvalidResult;
			InvalidResult.SurfaceFace = Stroke.SurfaceFace;
			return InvalidResult;
		}
	}

	bool bWasFullyRemoved = false;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeFullyRemovedCheck);
		bWasFullyRemoved = IsContaminationFullyRemoved();
	}

	FMD_ScrapeCellView FaceCells =
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

	// Saturated scraping deposits requested work only; it never removes either existing layer.
	const bool bApplyExactRequestedAmountStamp =
		FMath::IsFinite(StampRatio)
		&& StampRatio > 0.0f;
	TArray<FMD_ScrapeSurfaceCell> OriginalFaceCells;
	if (bApplyExactRequestedAmountStamp && !bMeshUVSurface)
	{
		// The planar evaluator normalizes the entire face; keep its full rollback contract.
		OriginalFaceCells.Append(FaceCells.GetData(), FaceCells.Num());
	}
	FMD_ScrapeResult Result;
	FMD_ScrapeMeshUVProgress MeshUVProgressAfter;
	TArray<float> RequestedRemovalAmounts;
	{
		SCOPE_CYCLE_COUNTER(STAT_MD_ScrapeEvaluate);
		if (UsesMeshUVSurface())
		{
			FMD_ScrapeMeshUVStroke MeshStroke;
			const UMD_ScrapeMeshUVData* Data = GetContaminationMeshUVData();
			if (!Data || !SurfaceMesh || !TryBuildMeshUVStroke(Stroke, MeshStroke))
			{
				Result.SurfaceFace = Stroke.SurfaceFace;
				return Result;
			}
			// Authoritative evaluation captures only first-contact cells and consumes the current cache.
			Result = FMD_ScrapeEvaluator::ApplyStrokeToMeshUVGrid(*Data,
				SurfaceMesh->GetComponentTransform(), MeshStroke, ToolSpec,
				EffectiveScrapeSettings, FaceCells, nullptr,
				&LocalEvaluationScratch, GetContaminationCellCarrierWeights(),
				&CachedMeshUVProgress, &MeshUVProgressAfter, bApplyExactRequestedAmountStamp,
				!bApplyExactRequestedAmountStamp);
		}
		else if (bApplyExactRequestedAmountStamp)
		{
			Result =
				FMD_ScrapeEvaluator::ApplyStrokeToGridAndCaptureRequestedAmounts(
					Stroke,
					ToolSpec,
					EffectiveScrapeSettings,
					ContaminationGridDimensions,
					ResolvedContaminationSurfaceSizeCm,
					FaceCells,
					RequestedRemovalAmounts,
					LocalEvaluationScratch,
					false);
		}
		else
		{
			Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
				Stroke,
				ToolSpec,
				EffectiveScrapeSettings,
				ContaminationGridDimensions,
				ResolvedContaminationSurfaceSizeCm,
				FaceCells,
				LocalEvaluationScratch);
		}
	}

	// 잘못된 입력은 상태를 변경하지 않음
	if (!Result.bIsInputValid)
	{
		return Result;
	}
	if (bMeshUVSurface && !Result.bWasStrokeLongEnough)
	{
		// A valid stationary/short MeshUV stroke has not mutated cells or produced progress.
		return Result;
	}
	const auto RestoreResidueTransaction = [&]()
	{
		if (bMeshUVSurface)
		{
			for (const FMD_ScrapeTouchedCell& Touched : LocalEvaluationScratch.MeshUVTouchedCells)
			{
				FaceCells.Set(Touched.CellIndex, Touched.Before);
			}
            // Only pages touched by this rejected transaction can have returned to initial.
            TSet<int32> RestoredPages;
            for (const FMD_ScrapeTouchedCell& Touched : LocalEvaluationScratch.MeshUVTouchedCells)
            {
                const int32 StorageIndex = GetContaminationStorageIndex(Touched.CellIndex);
                if (StorageIndex != INDEX_NONE && !ContaminationPagedCells.IsEmpty())
                    RestoredPages.Add(StorageIndex / FMD_ScrapePagedState::PageSize);
            }
            for (const int32 Page : RestoredPages)
                ContaminationPagedCells.CompactUnchangedPageForIndex(Page * FMD_ScrapePagedState::PageSize);
		}
		else
		{
			for (int32 CellIndex = 0; CellIndex < FaceCells.Num(); ++CellIndex)
			{
				FaceCells.Set(CellIndex, OriginalFaceCells[CellIndex]);
			}
		}
	};
	const auto KeepUnstampedEvaluationProgress = [&]()
	{
		// Preserve the existing non-stamped rejection behavior: its evaluated cells remain changed.
		if (bMeshUVSurface && MeshUVProgressAfter.bIsValid)
		{
			CommitContaminationMeshUVProgressCache(MeshUVProgressAfter);
		}
		else
		{
			RefreshContaminationCleanRatioCache(Stroke.SurfaceFace);
		}
	};
	const float GrossRemovedContaminationAmount =
		Result.RemovedContaminationAmount;
	const float GrossRemovedCleanRatioDelta = Result.CleanRatioDelta;
	TArray<FMD_ScrapeCellChange> StampChanges;
	float AddedContaminationAmount = 0.0f;
	if (bApplyExactRequestedAmountStamp && Result.bWasStrokeLongEnough
		&& (bMeshUVSurface ? !LocalEvaluationScratch.MeshUVTouchedCells.IsEmpty()
			: !RequestedRemovalAmounts.IsEmpty()))
	{
		const bool bAcceptedStamp = bMeshUVSurface
			? FMD_ScrapeEvaluator::TryApplyContaminationStampToTouchedCells(
				ContaminationGridDimensions, LocalEvaluationScratch.MeshUVTouchedCells,
				StampRatio, FaceCells, StampChanges, AddedContaminationAmount,
				GetContaminationCellCarrierWeights())
			: FMD_ScrapeEvaluator::TryApplyContaminationStampToGrid(
				ContaminationGridDimensions, RequestedRemovalAmounts, StampRatio,
				FaceCells, StampChanges, AddedContaminationAmount);
		if (!bAcceptedStamp)
		{
			RestoreResidueTransaction();
			FMD_ScrapeResult RejectedResult;
			RejectedResult.SurfaceFace = Stroke.SurfaceFace;
			RejectedResult.EvaluatedTravelDistanceCm = Result.EvaluatedTravelDistanceCm;
			return RejectedResult;
		}
	}
	FMD_ScrapeMeshUVProgress FinalMeshUVProgress = MeshUVProgressAfter;
	if (bMeshUVSurface && (!FinalMeshUVProgress.bIsValid
		|| (!StampChanges.IsEmpty() && !FMD_ScrapeEvaluator::UpdateMeshUVProgressFromTouchedCells(
			CachedMeshUVProgress, LocalEvaluationScratch.MeshUVTouchedCells,
			FaceCells, GetContaminationCellCarrierWeights(), FinalMeshUVProgress))))
	{
		if (bApplyExactRequestedAmountStamp) { RestoreResidueTransaction(); }
		else { KeepUnstampedEvaluationProgress(); }
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		RejectedResult.EvaluatedTravelDistanceCm = Result.EvaluatedTravelDistanceCm;
		return RejectedResult;
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
	double FinalBaseCarrierAmount = FinalMeshUVProgress.BaseCarrierAmount;
	double FinalTransferredCarrierAmount = FinalMeshUVProgress.TransferredCarrierAmount;
	if (!bMeshUVSurface)
	{
		FinalBaseCarrierAmount = FinalTransferredCarrierAmount = 0.0;
		for (const FMD_ScrapeSurfaceCell& Cell : FaceCells)
		{
			FinalBaseCarrierAmount += static_cast<double>(Cell.RemainingContamination);
			FinalTransferredCarrierAmount += static_cast<double>(Cell.TransferredContamination);
		}
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
			RestoreResidueTransaction();
		}
		else
		{
			KeepUnstampedEvaluationProgress();
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		RejectedResult.EvaluatedTravelDistanceCm = Result.EvaluatedTravelDistanceCm;
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
			RestoreResidueTransaction();
		}
		else
		{
			KeepUnstampedEvaluationProgress();
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		RejectedResult.EvaluatedTravelDistanceCm = Result.EvaluatedTravelDistanceCm;
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
			RestoreResidueTransaction();
		}
		else
		{
			KeepUnstampedEvaluationProgress();
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		RejectedResult.EvaluatedTravelDistanceCm = Result.EvaluatedTravelDistanceCm;
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
			RestoreResidueTransaction();
		}
		else
		{
			KeepUnstampedEvaluationProgress();
		}
		FMD_ScrapeResult RejectedResult;
		RejectedResult.SurfaceFace = Stroke.SurfaceFace;
		RejectedResult.EvaluatedTravelDistanceCm = Result.EvaluatedTravelDistanceCm;
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
		Result.CleanRatioAfter = bMeshUVSurface
			? FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
				FinalMeshUVProgress.InitialCarrierAmount, FinalMeshUVProgress.CurrentCarrierAmount)
			: FMD_ScrapeEvaluator::CalculateCleanRatio(FaceCells);
		Result.CleanRatioDelta = FMath::Max(
			0.0f,
			Result.CleanRatioAfter - Result.CleanRatioBefore);
		Result.bIsSurfaceFullyClean = bMeshUVSurface
			? (EffectiveCompletionTolerance <= 0.0f
				? FinalMeshUVProgress.UncleanCellCountAtZero == 0
				: FinalMeshUVProgress.UncleanCellCount == 0)
			: FMD_ScrapeEvaluator::IsSurfaceFullyClean(FaceCells, EffectiveCompletionTolerance);

		Result.NewlyCleanedCellCount = 0;
		for (FMD_ScrapeCellChange& CellChange : Result.CellChanges)
		{
			if (!FaceCells.IsValidIndex(CellChange.CellIndex))
			{
				continue;
			}

			CellChange.RemainingAmountAfter =
				FaceCells.Read(CellChange.CellIndex).RemainingContamination;
			CellChange.bBecameClean = CellChange.bBecameClean
				&& CellChange.RemainingAmountAfter
					<= EffectiveCompletionTolerance
				&& FaceCells.Read(CellChange.CellIndex).TransferredContamination
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

	TArray<FMD_ScrapeCellChange> MergedChanges;
	MergedChanges.Reserve(Result.CellChanges.Num() + StampChanges.Num());
	int32 RemovedIndex = 0;
	int32 StampIndex = 0;
	while (RemovedIndex < Result.CellChanges.Num() || StampIndex < StampChanges.Num())
	{
		if (StampIndex >= StampChanges.Num()
			|| (RemovedIndex < Result.CellChanges.Num()
				&& Result.CellChanges[RemovedIndex].CellIndex < StampChanges[StampIndex].CellIndex))
		{
			MergedChanges.Add(Result.CellChanges[RemovedIndex++]);
		}
		else if (RemovedIndex >= Result.CellChanges.Num()
			|| StampChanges[StampIndex].CellIndex < Result.CellChanges[RemovedIndex].CellIndex)
		{
			MergedChanges.Add(StampChanges[StampIndex++]);
		}
		else
		{
			FMD_ScrapeCellChange Change = Result.CellChanges[RemovedIndex++];
			Change.RemainingAmountAfter = StampChanges[StampIndex++].RemainingAmountAfter;
			Change.bBecameClean = false;
			MergedChanges.Add(Change);
		}
	}
	Result.CellChanges = MoveTemp(MergedChanges);
	const TArray<FMD_ScrapeCellChange>& SurfaceChanges = Result.CellChanges;
	Result.ModifiedCellCount = SurfaceChanges.Num();

	if (bMeshUVSurface)
	{
		CommitContaminationMeshUVProgressCache(FinalMeshUVProgress);
	}
	else
	{
		RefreshContaminationCleanRatioCache(Stroke.SurfaceFace);
	}
	if (ContaminationVisualUpdateBatchDepth > 0)
	{
		for (const FMD_ScrapeCellChange& SurfaceChange : SurfaceChanges)
		{
			PendingBatchedContaminationVisualCellIndices.Add(
				SurfaceChange.CellIndex);
		}
	}
	else
	{
		UpdateContaminationMask(SurfaceChanges);
	}
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

	if (UsesMeshUVSurface())
	{
		return FMath::Clamp(CachedFrontContaminationCleanRatio, 0.0f, 1.0f);
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

	return FMD_ScrapeFaceProgress::IsSurfaceComplete(
		IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Front),
		IsContaminationFullyRemoved(EMD_ScrapeSurfaceFace::Back), UsesMeshUVSurface());
}

bool UMD_ScrapeSurfaceComponent::IsContaminationFullyRemoved(
	const EMD_ScrapeSurfaceFace Face) const
{
	if (!bContaminationEnabled)
	{
		return true;
	}
	if (UsesMeshUVSurface() && Face == EMD_ScrapeSurfaceFace::Back)
	{
		// Physical back faces use canonical Front atlas addresses; no second state exists.
		return bContaminationSurfaceInitialized;
	}

	const FMD_ScrapeConstCellView FaceCells =
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
	const float SafeTolerance = FMath::IsFinite(EffectiveCleanTolerance)
		? FMath::Max(0.0f, EffectiveCleanTolerance) : 0.0f;
	if (UsesMeshUVSurface() && CachedMeshUVProgress.bIsValid
		&& CachedMeshUVProgress.UncleanCellCount >= 0
		&& CachedMeshUVProgress.UncleanCellCountAtZero >= 0
		&& (SafeTolerance == 0.0f || SafeTolerance == CachedMeshUVProgress.CleanTolerance))
	{
		const int32 UncleanCellCount = SafeTolerance == 0.0f
			? CachedMeshUVProgress.UncleanCellCountAtZero : CachedMeshUVProgress.UncleanCellCount;
		return bContaminationSurfaceInitialized && !FaceCells.IsEmpty() && UncleanCellCount == 0;
	}
	return bContaminationSurfaceInitialized
		&& !FaceCells.IsEmpty()
		&& FMD_ScrapeEvaluator::IsSurfaceFullyClean(
			FaceCells,
			EffectiveCleanTolerance);
}

void UMD_ScrapeSurfaceComponent::CommitContaminationMeshUVProgressCache(
	const FMD_ScrapeMeshUVProgress& Progress)
{
	CachedMeshUVProgress = Progress;
	CachedFrontInitialContaminationAmount = Progress.InitialCarrierAmount;
	CachedFrontCurrentContaminationAmount = Progress.CurrentCarrierAmount;
	CachedFrontContaminationCleanRatio = FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
		Progress.InitialCarrierAmount, Progress.CurrentCarrierAmount);
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
	if (UsesMeshUVSurface())
	{
		CachedInitialContaminationAmount = CachedCurrentContaminationAmount = 0.0;
		if (Face == EMD_ScrapeSurfaceFace::Front)
		{
			FMD_ScrapeMeshUVProgress Progress;
			FMD_ScrapeEvaluator::CalculateMeshUVProgress(GetContaminationCellsForFace(Face),
				GetContaminationCellCarrierWeights(), ScrapeSettings.CleanTolerance, Progress);
			CommitContaminationMeshUVProgressCache(Progress);
		}
		else
		{
			CachedCleanRatio = FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(0.0, 0.0);
		}
	}
	else
	{
		CachedCleanRatio = FMD_ScrapeEvaluator::CalculateCurrentCleanRatio(
			GetContaminationCellsForFace(Face),
			CachedInitialContaminationAmount,
			CachedCurrentContaminationAmount);
	}
}
