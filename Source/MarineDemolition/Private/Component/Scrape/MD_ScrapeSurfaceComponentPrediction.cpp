// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Gameplay/Scrape/MD_ScrapeEvaluator.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

namespace
{
	TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, uint64> PredictionAllocations;

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

SIZE_T UMD_ScrapeSurfaceComponent::GetLocalScrapePredictionAllocatedBytes() const
{
    SIZE_T Bytes = LocalScrapePredictions.GetAllocatedSize()
        + LocalPredictedPagedCells.GetAllocatedSize()
        + LocalPredictedContaminationCells.GetAllocatedSize()
        + LocalScrapePredictionFaceScratch.GetAllocatedSize()
        + PendingLocalScrapePredictionVisualCellIndices.GetAllocatedSize()
        + PendingLocalScrapePredictionReconcileCells.GetAllocatedSize()
        + (bHasPredictedContaminationBoundary ? PredictedContaminationBoundary.GetAllocatedBytes() : 0);
    for (const FLocalScrapePrediction& Prediction : LocalScrapePredictions)
    {
        Bytes += Prediction.Strokes.GetAllocatedSize() + Prediction.PredictedFinalCellStates.GetAllocatedSize();
    }
    return Bytes;
}

bool UMD_ScrapeSurfaceComponent::UpdateLocalScrapePredictionMemoryBudget()
{
    const uint64 Bytes = GetLocalScrapePredictionAllocatedBytes();
    uint64 WorldBytes = Bytes;
    for (auto It = PredictionAllocations.CreateIterator(); It; ++It)
    {
        UMD_ScrapeSurfaceComponent* Surface = It.Key().Get();
        if (!IsValid(Surface)) { It.RemoveCurrent(); continue; }
        // Re-read actual COW ownership: authoritative writes can detach a shared page.
        It.Value() = Surface->GetLocalScrapePredictionAllocatedBytes();
        if (Surface != this && Surface->GetWorld() == GetWorld()) { WorldBytes += It.Value(); }
    }
    if (Bytes > MaximumLocalScrapePredictionBytes || WorldBytes > MaximumWorldScrapePredictionBytes) { return false; }
    PredictionAllocations.FindOrAdd(this) = Bytes;
    return true;
}

void UMD_ScrapeSurfaceComponent::ReleaseLocalScrapePredictionMemoryBudget()
{
    PredictionAllocations.Remove(this);
}

FMD_ScrapeConstCellView
UMD_ScrapeSurfaceComponent::GetVisualContaminationCells() const
{
	if (UsesMeshUVSurface())
	{
		if (!LocalPredictedPagedCells.IsEmpty()
			&& LocalPredictedPagedCells.Num() == GetContaminationStorageCellCount())
		{
			return FMD_ScrapeConstCellView(&LocalPredictedPagedCells, ContaminationCellLayout.Get());
		}
	}
	else if (!LocalPredictedContaminationCells.IsEmpty()
		&& LocalPredictedContaminationCells.Num() == GetContaminationStorageCellCount())
	{
		return FMD_ScrapeConstCellView(MakeArrayView(LocalPredictedContaminationCells));
	}
	return GetContaminationCellView();
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
	if (!AreContaminationCarrierWeightsCurrent() || !IsValid(PredictionOwner)
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

	if (!UpdateLocalScrapePredictionMemoryBudget())
    {
        for (FLocalScrapePrediction& Pending : LocalScrapePredictions) { Pending.Owner.Reset(); }
        RebuildLocalScrapePredictionShadow();
        return false;
    }

	if (UsesMeshUVSurface())
	{
		if (LocalPredictedPagedCells.Num() != GetContaminationStorageCellCount())
		{
			// Fork the current pages; either writer separates only a changed page.
			LocalPredictedPagedCells = ContaminationPagedCells;
		}
	}
	else if (LocalPredictedContaminationCells.Num() != GetContaminationStorageCellCount())
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
			LocalPredictedPagedCells.Reset();
			LocalPredictedContaminationCells.Empty();
			LocalScrapePredictionFaceScratch.Empty();
		}
		return false;
	}

    // Conservative map growth headroom prevents a single wide stroke from
    // constructing an oversized retained per-batch state map before admission.
    if (GetLocalScrapePredictionAllocatedBytes()
        + static_cast<uint64>(ChangedCellIndices.Num()) * 128 + sizeof(FLocalScrapePredictionStroke)
        > MaximumLocalScrapePredictionBytes)
    {
        PendingLocalScrapePredictionVisualCellIndices.Append(ChangedCellIndices);
        for (FLocalScrapePrediction& Pending : LocalScrapePredictions) { Pending.Owner.Reset(); }
        RebuildLocalScrapePredictionShadow();
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
		const int32 StorageIndex = GetContaminationStorageIndex(CellIndex);
		if (UsesMeshUVSurface() ? LocalPredictedPagedCells.IsValidIndex(StorageIndex)
			: LocalPredictedContaminationCells.IsValidIndex(StorageIndex))
		{
			Prediction->PredictedFinalCellStates.FindOrAdd(CellIndex) = UsesMeshUVSurface()
				? LocalPredictedPagedCells.Read(StorageIndex)
				: LocalPredictedContaminationCells[StorageIndex];
		}
	}
	PendingLocalScrapePredictionVisualCellIndices.Append(ChangedCellIndices);
    if (!UpdateLocalScrapePredictionMemoryBudget())
    {
        // Input still reaches the server. Drop only speculative visuals, including
        // this stroke's addresses, so the fallback restores the authoritative mask.
        for (FLocalScrapePrediction& Pending : LocalScrapePredictions) { Pending.Owner.Reset(); }
        RebuildLocalScrapePredictionShadow();
        return false;
    }

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
	FMD_ScrapeEvaluationScratch LocalEvaluationScratch;
    LocalEvaluationScratch.bCaptureBoundary = UsesMeshUVSurface();
	TArray<FMD_ScrapeSurfaceCell> PredictionFaceScratch;
	OutChangedCellIndices.Reset();
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!TryCalculateContaminationCellCounts(
			CellsPerFace,
			TotalCellCount)
		|| (UsesMeshUVSurface() ? LocalPredictedPagedCells.Num()
			: LocalPredictedContaminationCells.Num()) != GetContaminationStorageCellCount()
		|| (UsesMeshUVSurface() && Stroke.SurfaceFace != EMD_ScrapeSurfaceFace::Front))
	{
		return false;
	}

	const int32 FaceOffset =
		Stroke.SurfaceFace == EMD_ScrapeSurfaceFace::Back
			? CellsPerFace
			: 0;
	FMD_ScrapeCellView FaceCells = (UsesMeshUVSurface()
		? FMD_ScrapeCellView(&LocalPredictedPagedCells, ContaminationCellLayout.Get())
		: FMD_ScrapeCellView(MakeArrayView(LocalPredictedContaminationCells))).Slice(FaceOffset, CellsPerFace);
    PredictionFaceScratch.Reset();
    if (!UsesMeshUVSurface())
    {
        PredictionFaceScratch.Append(FaceCells.GetData(), FaceCells.Num());
    }

	FMD_ScrapeResult Result;
	if (UsesMeshUVSurface())
	{
		FMD_ScrapeMeshUVStroke MeshStroke;
		const UMD_ScrapeMeshUVData* Data = GetContaminationMeshUVData();
		if (!Data || !SurfaceMesh || !TryBuildMeshUVStroke(Stroke, MeshStroke))
		{
			return false;
		}
		Result = FMD_ScrapeEvaluator::ApplyStrokeToMeshUVGrid(*Data,
			SurfaceMesh->GetComponentTransform(), MeshStroke, ToolSpec, ScrapeSettings,
			FaceCells, nullptr, &LocalEvaluationScratch,
			GetContaminationCellCarrierWeights());
	}
	else
	{
		Result = FMD_ScrapeEvaluator::ApplyStrokeToGrid(
		Stroke,
		ToolSpec,
		ScrapeSettings,
		ContaminationGridDimensions,
		ResolvedContaminationSurfaceSizeCm,
		MakeArrayView(PredictionFaceScratch),
		LocalEvaluationScratch);
	}

    if (UsesMeshUVSurface())
    {
        // Whole-path validation and first-contact snapshots retain rollback using O(K)
        // touched state. No face-sized working copy is needed for MeshUV prediction.
        TArray<int32, TInlineAllocator<8>> RestoredPageIndices;
        int32 PreviousRestoredPage = INDEX_NONE;
        const auto RestoreCell = [&](const FMD_ScrapeTouchedCell& Touched)
        {
            const int32 StorageIndex = GetContaminationStorageIndex(Touched.CellIndex);
            LocalPredictedPagedCells.Set(StorageIndex, Touched.Before);
            const int32 PageIndex = StorageIndex / FMD_ScrapePagedState::PageSize;
            // Touched atlas IDs and their active storage indices are ascending.
            if (PageIndex != PreviousRestoredPage)
            {
                RestoredPageIndices.Add(StorageIndex);
                PreviousRestoredPage = PageIndex;
            }
        };
        for (const FMD_ScrapeTouchedCell& Touched : LocalEvaluationScratch.MeshUVTouchedCells)
        {
            if (!Result.bIsInputValid || !Result.bWasStrokeLongEnough)
            {
                RestoreCell(Touched);
            }
            else if (!HaveSameVisualState(FaceCells.Read(Touched.CellIndex), Touched.Before))
            {
                OutChangedCellIndices.Add(Touched.CellIndex);
            }
            else
            {
                // The former scratch-copy path discarded changes below its visual
                // comparison tolerance. Preserve that exact commit predicate.
                RestoreCell(Touched);
            }
        }
        // Finish every write first, then release only exactly restored touched pages.
        for (const int32 StorageIndex : RestoredPageIndices)
        {
            if (LocalPredictedPagedCells.IsValidIndex(StorageIndex))
            {
                LocalPredictedPagedCells.CompactUnchangedPageForIndex(StorageIndex);
            }
        }
        if (!OutChangedCellIndices.IsEmpty()) CommitContaminationBoundary(LocalEvaluationScratch, true);
        return !OutChangedCellIndices.IsEmpty();
    }
	if (!Result.bIsInputValid || !Result.bWasStrokeLongEnough)
	{
		return false;
	}

	for (int32 LocalCellIndex = 0;
		LocalCellIndex < PredictionFaceScratch.Num();
		++LocalCellIndex)
	{
		if (HaveSameVisualState(
				FaceCells.Read(LocalCellIndex),
				PredictionFaceScratch[LocalCellIndex]))
		{
			continue;
		}
		FaceCells.Set(LocalCellIndex,
			PredictionFaceScratch[LocalCellIndex]);
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
	if (GetContaminationStorageIndex(CellIndex) == INDEX_NONE)
	{
		return;
	}
	if (PendingLocalScrapePredictionReconcileCells.Num()
		!= GetContaminationAtlasCellCount())
	{
		PendingLocalScrapePredictionReconcileCells.Init(
			false,
			GetContaminationAtlasCellCount());
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
    ResetPredictedContaminationBoundary();
	const int32 PreviousVisualCellCount = GetVisualContaminationCells().Num();
	// Only these addresses can differ from the authoritative state. Include old
	// predictions before removing expired owners, and new addresses during replay.
	// The mask compares texel values itself, so no full previous-state copy is needed.
	for (const int32 CellIndex : PendingLocalScrapePredictionVisualCellIndices)
	{
		MarkLocalScrapePredictionReconcileCell(CellIndex);
	}
	for (const FLocalScrapePrediction& Prediction : LocalScrapePredictions)
	{
		for (const TPair<int32, FMD_ScrapeSurfaceCell>& PredictedCell
			: Prediction.PredictedFinalCellStates)
		{
			MarkLocalScrapePredictionReconcileCell(PredictedCell.Key);
		}
	}
	PendingLocalScrapePredictionVisualCellIndices.Reset();
	LocalScrapePredictions.RemoveAll(
		[](const FLocalScrapePrediction& Prediction)
		{
			return !Prediction.Owner.IsValid();
		});
	if (LocalScrapePredictions.IsEmpty())
	{
		TArray<FMD_ScrapeCellChange> CellChanges;
		for (TConstSetBitIterator<> It(PendingLocalScrapePredictionReconcileCells); It; ++It)
		{
			const int32 CellIndex = It.GetIndex();
			if (GetContaminationStorageIndex(CellIndex) == INDEX_NONE)
			{
				continue;
			}
			FMD_ScrapeCellChange& CellChange =
				CellChanges.AddDefaulted_GetRef();
			CellChange.CellIndex = CellIndex;
		}
		PendingLocalScrapePredictionReconcileCells.Empty();
		LocalPredictedPagedCells.Reset();
		LocalPredictedContaminationCells.Empty();
		LocalScrapePredictionFaceScratch.Empty();
		PendingLocalScrapePredictionVisualCellIndices.Empty();
		LocalScrapePredictions.Empty();
		ReleaseLocalScrapePredictionMemoryBudget();
		UpdateContaminationMask(CellChanges);
		return;
	}

	if (UsesMeshUVSurface())
	{
		LocalPredictedPagedCells = ContaminationPagedCells;
	}
	else
	{
		LocalPredictedContaminationCells = ContaminationCells;
	}
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
				const int32 StorageIndex = GetContaminationStorageIndex(CellIndex);
				if (UsesMeshUVSurface() ? LocalPredictedPagedCells.IsValidIndex(StorageIndex)
					: LocalPredictedContaminationCells.IsValidIndex(StorageIndex))
				{
					Prediction.PredictedFinalCellStates.FindOrAdd(CellIndex) = UsesMeshUVSurface()
						? LocalPredictedPagedCells.Read(StorageIndex)
						: LocalPredictedContaminationCells[StorageIndex];
					MarkLocalScrapePredictionReconcileCell(CellIndex);
				}
			}
		}
	}

    if (!UpdateLocalScrapePredictionMemoryBudget())
    {
        for (FLocalScrapePrediction& Pending : LocalScrapePredictions) { Pending.Owner.Reset(); }
        RebuildLocalScrapePredictionShadow();
        return;
    }
	const FMD_ScrapeConstCellView CurrentVisual =
		GetVisualContaminationCells();
	if (PreviousVisualCellCount != CurrentVisual.Num())
	{
		PendingLocalScrapePredictionReconcileCells.Reset();
		InitializeContaminationMask();
		InitializeTransferredPaintMask();
		RefreshContaminationDecalMaterialParameters();
		return;
	}

	TArray<FMD_ScrapeCellChange> CellChanges;
	for (TConstSetBitIterator<> It(PendingLocalScrapePredictionReconcileCells); It; ++It)
	{
		const int32 CellIndex = It.GetIndex();
		if (CurrentVisual.IsValidIndex(CellIndex))
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
