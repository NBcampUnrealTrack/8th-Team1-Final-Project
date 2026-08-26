// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

namespace
{
	bool HaveSameVisualState(
		const FMD_ScrapeSurfaceCell& Left,
		const FMD_ScrapeSurfaceCell& Right)
	{
		return FMath::IsNearlyEqual(
				Left.InitialContamination,
				Right.InitialContamination,
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				Left.RemainingContamination,
				Right.RemainingContamination,
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				Left.TransferredContamination,
				Right.TransferredContamination,
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				Left.TransferredPaintPatternDensity,
				Right.TransferredPaintPatternDensity,
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				Left.CoveredFraction,
				Right.CoveredFraction,
				UE_SMALL_NUMBER);
	}
}

TConstArrayView<FMD_ScrapeSurfaceCell>
UMD_ScrapeSurfaceComponent::GetVisualContaminationCells() const
{
	if (!LocalPredictedContaminationCells.IsEmpty()
		&& LocalPredictedContaminationCells.Num() == ContaminationCells.Num())
	{
		return MakeArrayView(LocalPredictedContaminationCells);
	}
	return MakeArrayView(ContaminationCells);
}

bool UMD_ScrapeSurfaceComponent::ApplyLocalScrapePrediction(
	UMD_ScrapeToolComponent* const PredictionOwner,
	const uint64 FocusSessionId,
	const uint32 AimSequence,
	const double CreatedSeconds,
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	const bool bDeferVisualUpdate)
{
	FLocalScrapePrediction* Prediction = LocalScrapePredictions.FindByPredicate(
		[PredictionOwner, FocusSessionId, AimSequence](
			FLocalScrapePrediction& Candidate)
		{
			return Candidate.Owner.Get() == PredictionOwner
				&& Candidate.FocusSessionId == FocusSessionId
				&& Candidate.AimSequence == AimSequence;
		});
	if (!IsValid(PredictionOwner)
		|| HasAuthority()
		|| !bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| FocusSessionId == 0
		|| AimSequence == 0
		|| !FMath::IsFinite(CreatedSeconds)
		|| (!Prediction
			&& LocalScrapePredictions.Num()
				>= MaximumLocalScrapePredictionBatches)
		|| (Prediction
			&& Prediction->Strokes.Num()
				>= MaximumLocalScrapePredictionStrokesPerBatch))
	{
		return false;
	}

	if (LocalPredictedContaminationCells.Num() != ContaminationCells.Num())
	{
		LocalPredictedContaminationCells = ContaminationCells;
	}

	TArray<int32> ChangedCellIndices;
	if (!ApplyLocalScrapePredictionToShadow(
			Stroke,
			ToolSpec,
			ChangedCellIndices))
	{
		if (LocalScrapePredictions.IsEmpty())
		{
			LocalPredictedContaminationCells.Reset();
		}
		return false;
	}

	if (!Prediction)
	{
		Prediction = &LocalScrapePredictions.AddDefaulted_GetRef();
		Prediction->Owner = PredictionOwner;
		Prediction->FocusSessionId = FocusSessionId;
		Prediction->AimSequence = AimSequence;
		Prediction->CreatedSeconds = CreatedSeconds;
	}
	FLocalScrapePredictionStroke& PredictedStroke =
		Prediction->Strokes.AddDefaulted_GetRef();
	PredictedStroke.Stroke = Stroke;
	PredictedStroke.ToolSpec = ToolSpec;
	for (const int32 CellIndex : ChangedCellIndices)
	{
		if (LocalPredictedContaminationCells.IsValidIndex(CellIndex))
		{
			Prediction->PredictedFinalCellStates.FindOrAdd(CellIndex) =
				LocalPredictedContaminationCells[CellIndex];
		}
	}
	PendingLocalScrapePredictionVisualCellIndices.Append(ChangedCellIndices);
	if (!bDeferVisualUpdate)
	{
		FlushLocalScrapePredictionVisualUpdates();
	}
	return true;
}

bool UMD_ScrapeSurfaceComponent::ApplyLocalScrapePredictionToShadow(
	const FMD_ScrapeStroke& Stroke,
	const FMD_ScrapeToolSpec& ToolSpec,
	TArray<int32>& OutChangedCellIndices)
{
	OutChangedCellIndices.Reset();
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| LocalPredictedContaminationCells.Num() != TotalCellCount)
	{
		return false;
	}

	const int32 FaceOffset =
		Stroke.SurfaceFace == EMD_ScrapeSurfaceFace::Back
			? CellsPerFace
			: 0;
	TArrayView<FMD_ScrapeSurfaceCell> FaceCells(
		LocalPredictedContaminationCells.GetData() + FaceOffset,
		CellsPerFace);
	LocalScrapePredictionFaceScratch.Reset();
	LocalScrapePredictionFaceScratch.Append(
		FaceCells.GetData(),
		FaceCells.Num());

	const FMD_ScrapeResult Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
		Stroke,
		ToolSpec,
		ScrapeSettings,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm,
		MakeArrayView(LocalScrapePredictionFaceScratch),
		ScrapeEvaluationScratch);
	if (!Result.bIsInputValid || !Result.bWasStrokeLongEnough)
	{
		return false;
	}

	for (int32 LocalCellIndex = 0;
		LocalCellIndex < LocalScrapePredictionFaceScratch.Num();
		++LocalCellIndex)
	{
		if (HaveSameVisualState(
				FaceCells[LocalCellIndex],
				LocalScrapePredictionFaceScratch[LocalCellIndex]))
		{
			continue;
		}
		FaceCells[LocalCellIndex] =
			LocalScrapePredictionFaceScratch[LocalCellIndex];
		OutChangedCellIndices.Add(
			FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
				Stroke.SurfaceFace,
				LocalCellIndex,
				CellsPerFace));
	}
	return !OutChangedCellIndices.IsEmpty();
}

void UMD_ScrapeSurfaceComponent::MarkLocalScrapePredictionReconcileCell(
	const int32 CellIndex)
{
	if (!ContaminationCells.IsValidIndex(CellIndex))
	{
		return;
	}
	if (PendingLocalScrapePredictionReconcileCells.Num()
		!= ContaminationCells.Num())
	{
		PendingLocalScrapePredictionReconcileCells.Init(
			false,
			ContaminationCells.Num());
	}
	PendingLocalScrapePredictionReconcileCells[CellIndex] = true;
}

void UMD_ScrapeSurfaceComponent::FlushLocalScrapePredictionVisualUpdates()
{
	if (PendingLocalScrapePredictionVisualCellIndices.IsEmpty())
	{
		return;
	}

	TArray<FMD_ScrapeCellChange> CellChanges;
	CellChanges.Reserve(PendingLocalScrapePredictionVisualCellIndices.Num());
	PendingLocalScrapePredictionVisualCellIndices.Sort();
	int32 PreviousCellIndex = INDEX_NONE;
	for (const int32 CellIndex
		: PendingLocalScrapePredictionVisualCellIndices)
	{
		if (CellIndex == PreviousCellIndex)
		{
			continue;
		}
		FMD_ScrapeCellChange& CellChange = CellChanges.AddDefaulted_GetRef();
		CellChange.CellIndex = CellIndex;
		PreviousCellIndex = CellIndex;
	}
	PendingLocalScrapePredictionVisualCellIndices.Reset();
	UpdateContaminationMask(CellChanges);
}

void UMD_ScrapeSurfaceComponent::ResolveLocalScrapePredictions(
	UMD_ScrapeToolComponent* const PredictionOwner,
	const uint64 FocusSessionId,
	const uint32 AimSequence,
	const FMD_ScrapeAuthoritativeVisualDelta* const Resolution,
	const bool bRebuildIfPredictionMissing)
{
	bool bCanKeepCurrentShadow = true;
	const int32 RemovedCount = LocalScrapePredictions.RemoveAll(
		[this,
			PredictionOwner,
			FocusSessionId,
			AimSequence,
			Resolution,
			&bCanKeepCurrentShadow](
			const FLocalScrapePrediction& Prediction)
		{
			const bool bOwnerExpired = !Prediction.Owner.IsValid();
			const bool bShouldResolve =
				Prediction.Owner.Get() == PredictionOwner
				&& Prediction.FocusSessionId == FocusSessionId
				&& IsAimSequenceAtOrBefore(
					Prediction.AimSequence,
					AimSequence);
			if (!bOwnerExpired && !bShouldResolve)
			{
				return false;
			}

			if (bOwnerExpired
				|| Prediction.AimSequence != AimSequence
				|| !Resolution
				|| !DoesLocalScrapePredictionMatchResolution(
					Prediction,
					*Resolution))
			{
				bCanKeepCurrentShadow = false;
			}
			for (const TPair<int32, FMD_ScrapeSurfaceCell>& PredictedCell
				: Prediction.PredictedFinalCellStates)
			{
				MarkLocalScrapePredictionReconcileCell(PredictedCell.Key);
			}
			return true;
		});
	if (RemovedCount > 0
		&& (LocalScrapePredictions.IsEmpty() || !bCanKeepCurrentShadow))
	{
		RebuildLocalScrapePredictionShadow();
	}
	else if (RemovedCount == 0
		&& (Resolution || bRebuildIfPredictionMissing)
		&& LocalScrapePredictions.ContainsByPredicate(
			[PredictionOwner, FocusSessionId](
				const FLocalScrapePrediction& Prediction)
			{
				return Prediction.Owner.Get() == PredictionOwner
					&& Prediction.FocusSessionId == FocusSessionId;
			}))
	{
		RebuildLocalScrapePredictionShadow();
	}
}

void UMD_ScrapeSurfaceComponent::ExpireLocalScrapePredictions(
	UMD_ScrapeToolComponent* const PredictionOwner,
	const double ExpireBeforeSeconds)
{
	bool bHasOwnedPrediction = false;
	bool bAllOwnedPredictionsExpired = true;
	for (const FLocalScrapePrediction& Prediction : LocalScrapePredictions)
	{
		if (Prediction.Owner.Get() != PredictionOwner)
		{
			continue;
		}
		bHasOwnedPrediction = true;
		bAllOwnedPredictionsExpired &=
			Prediction.CreatedSeconds <= ExpireBeforeSeconds;
	}
	if (bHasOwnedPrediction && bAllOwnedPredictionsExpired)
	{
		ClearLocalScrapePredictions(PredictionOwner);
	}
}

void UMD_ScrapeSurfaceComponent::ClearLocalScrapePredictions(
	UMD_ScrapeToolComponent* const PredictionOwner)
{
	const int32 RemovedCount = LocalScrapePredictions.RemoveAll(
		[this, PredictionOwner](const FLocalScrapePrediction& Prediction)
		{
			const bool bShouldRemove = !Prediction.Owner.IsValid()
				|| Prediction.Owner.Get() == PredictionOwner;
			if (bShouldRemove)
			{
				for (const TPair<int32, FMD_ScrapeSurfaceCell>& PredictedCell
					: Prediction.PredictedFinalCellStates)
				{
					MarkLocalScrapePredictionReconcileCell(
						PredictedCell.Key);
				}
			}
			return bShouldRemove;
		});
	if (RemovedCount > 0)
	{
		RebuildLocalScrapePredictionShadow();
	}
}

bool UMD_ScrapeSurfaceComponent::HasLocalScrapePredictions(
	const UMD_ScrapeToolComponent* const PredictionOwner) const
{
	return LocalScrapePredictions.ContainsByPredicate(
		[PredictionOwner](const FLocalScrapePrediction& Prediction)
		{
			return Prediction.Owner.Get() == PredictionOwner;
		});
}

bool UMD_ScrapeSurfaceComponent::DoesLocalScrapePredictionMatchResolution(
	const FLocalScrapePrediction& Prediction,
	const FMD_ScrapeAuthoritativeVisualDelta& Resolution) const
{
	if (Resolution.Surface != this
		|| Prediction.PredictedFinalCellStates.Num()
			!= Resolution.CellStates.Num())
	{
		return false;
	}

	for (const FMD_ReplicatedScrapeCellState& State : Resolution.CellStates)
	{
		const FMD_ScrapeSurfaceCell* const PredictedCell =
			Prediction.PredictedFinalCellStates.Find(State.GetCellIndex());
		if (!PredictedCell
			|| FMD_ScrapeReplicationCodec::QuantizeRemaining(
				PredictedCell->InitialContamination,
				0.0f) != State.QuantizedInitialContamination
			|| FMD_ScrapeReplicationCodec::QuantizeRemaining(
				PredictedCell->RemainingContamination,
				ScrapeSettings.CleanTolerance)
					!= State.QuantizedRemainingContamination
			|| FMD_ScrapeReplicationCodec::QuantizeRemaining(
				PredictedCell->TransferredContamination,
				ScrapeSettings.CleanTolerance)
					!= State.QuantizedTransferredContamination
			|| FMD_ScrapeReplicationCodec::QuantizeRemaining(
				PredictedCell->TransferredPaintPatternDensity,
				0.0f) != State.QuantizedTransferredPatternDensity
			|| FMD_ScrapeReplicationCodec::QuantizeRemaining(
				PredictedCell->CoveredFraction,
				0.0f) != State.QuantizedCoveredFraction)
		{
			return false;
		}
	}
	return true;
}

void UMD_ScrapeSurfaceComponent::RebuildLocalScrapePredictionShadow()
{
	PendingLocalScrapePredictionVisualCellIndices.Reset();
	LocalScrapePredictions.RemoveAll(
		[](const FLocalScrapePrediction& Prediction)
		{
			return !Prediction.Owner.IsValid();
		});
	if (LocalScrapePredictions.IsEmpty())
	{
		TArray<FMD_ScrapeCellChange> CellChanges;
		for (int32 CellIndex = 0;
			CellIndex < PendingLocalScrapePredictionReconcileCells.Num();
			++CellIndex)
		{
			if (!PendingLocalScrapePredictionReconcileCells[CellIndex]
				|| !ContaminationCells.IsValidIndex(CellIndex))
			{
				continue;
			}
			FMD_ScrapeCellChange& CellChange =
				CellChanges.AddDefaulted_GetRef();
			CellChange.CellIndex = CellIndex;
		}
		PendingLocalScrapePredictionReconcileCells.Reset();
		LocalPredictedContaminationCells.Reset();
		UpdateContaminationMask(CellChanges);
		return;
	}

	TArray<FMD_ScrapeSurfaceCell> PreviousVisualCells;
	const TConstArrayView<FMD_ScrapeSurfaceCell> PreviousVisual =
		GetVisualContaminationCells();
	PreviousVisualCells.Append(PreviousVisual.GetData(), PreviousVisual.Num());
	LocalPredictedContaminationCells = ContaminationCells;
	TArray<int32> ChangedCellIndices;
	for (FLocalScrapePrediction& Prediction : LocalScrapePredictions)
	{
		Prediction.PredictedFinalCellStates.Reset();
		for (const FLocalScrapePredictionStroke& PredictedStroke
			: Prediction.Strokes)
		{
			if (!ApplyLocalScrapePredictionToShadow(
				PredictedStroke.Stroke,
				PredictedStroke.ToolSpec,
				ChangedCellIndices))
			{
				continue;
			}
			for (const int32 CellIndex : ChangedCellIndices)
			{
				if (LocalPredictedContaminationCells.IsValidIndex(CellIndex))
				{
					Prediction.PredictedFinalCellStates.FindOrAdd(CellIndex) =
						LocalPredictedContaminationCells[CellIndex];
				}
			}
		}
	}

	const TConstArrayView<FMD_ScrapeSurfaceCell> CurrentVisual =
		GetVisualContaminationCells();
	if (PreviousVisualCells.Num() != CurrentVisual.Num())
	{
		InitializeContaminationMask();
		InitializeTransferredPaintMask();
		RefreshContaminationDecalMaterialParameters();
		return;
	}

	TArray<FMD_ScrapeCellChange> CellChanges;
	for (int32 CellIndex = 0; CellIndex < CurrentVisual.Num(); ++CellIndex)
	{
		if (!HaveSameVisualState(PreviousVisualCells[CellIndex], CurrentVisual[CellIndex]))
		{
			FMD_ScrapeCellChange& CellChange = CellChanges.AddDefaulted_GetRef();
			CellChange.CellIndex = CellIndex;
		}
	}
	PendingLocalScrapePredictionReconcileCells.Reset();
	UpdateContaminationMask(CellChanges);
}

bool UMD_ScrapeSurfaceComponent::IsAimSequenceAtOrBefore(
	const uint32 Candidate,
	const uint32 Baseline)
{
	return Candidate == Baseline
		|| static_cast<int32>(Baseline - Candidate) > 0;
}
