#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSyncSubsystem.h"
#include "Engine/World.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: Fast Array 셀 복제 · 수신 상태 반영

using namespace MD_ScrapeSurfaceComponentInternal;


bool FMD_ReplicatedScrapeCellStateArray::NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
{
	// MeshUV uses the bounded connection transport, including initial relevance/JIP.
	if (DeltaParameters.Writer && Owner && Owner->UsesMeshUVSurface()) { return false; }
	return FastArrayDeltaSerialize<FMD_ReplicatedScrapeCellState,
		FMD_ReplicatedScrapeCellStateArray>(Items, DeltaParameters, *this);
}

void FMD_ReplicatedScrapeCellStateArray::SetOwner(
	UMD_ScrapeSurfaceComponent* InOwner)
{
	Owner = InOwner;
	CellToItemIndex.Reset();

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const int32 CellIndex = Items[ItemIndex].GetCellIndex();
		if (CellIndex != INDEX_NONE)
		{
			CellToItemIndex.Add(CellIndex, ItemIndex);
		}
	}
}

void FMD_ReplicatedScrapeCellStateArray::SetCellState(
	const int32 CellIndex)
{
	if (!Owner || !Owner->HasAuthority() || Owner->UsesMeshUVSurface())
	{
		return;
	}

	FMD_ReplicatedScrapeCellState NewState;
	bool bMatchesGeneratedState = false;
	if (!Owner->TryBuildReplicatedCellState(
			CellIndex,
			NewState,
			bMatchesGeneratedState))
	{
		return;
	}

	if (const int32* ExistingItemIndex = CellToItemIndex.Find(CellIndex))
	{
		if (Items.IsValidIndex(*ExistingItemIndex))
		{
			if (bMatchesGeneratedState)
			{
				const int32 RemovedItemIndex = *ExistingItemIndex;
				Items.RemoveAtSwap(RemovedItemIndex, 1, EAllowShrinking::No);
				CellToItemIndex.Remove(CellIndex);
				if (Items.IsValidIndex(RemovedItemIndex))
				{
					CellToItemIndex.FindOrAdd(
						Items[RemovedItemIndex].GetCellIndex()) =
							RemovedItemIndex;
				}
				MarkArrayDirty();
				return;
			}

			FMD_ReplicatedScrapeCellState& Item = Items[*ExistingItemIndex];
			if (Item.QuantizedInitialContamination
					!= NewState.QuantizedInitialContamination
				|| Item.QuantizedRemainingContamination
					!= NewState.QuantizedRemainingContamination
				|| Item.QuantizedTransferredContamination
					!= NewState.QuantizedTransferredContamination
				|| Item.QuantizedTransferredPatternDensity
					!= NewState.QuantizedTransferredPatternDensity
				|| Item.QuantizedCoveredFraction
					!= NewState.QuantizedCoveredFraction)
			{
				Item.QuantizedInitialContamination =
					NewState.QuantizedInitialContamination;
				Item.QuantizedRemainingContamination =
					NewState.QuantizedRemainingContamination;
				Item.QuantizedTransferredContamination =
					NewState.QuantizedTransferredContamination;
				Item.QuantizedTransferredPatternDensity =
					NewState.QuantizedTransferredPatternDensity;
				Item.QuantizedCoveredFraction =
					NewState.QuantizedCoveredFraction;
				MarkItemDirty(Item);
			}
			return;
		}

		CellToItemIndex.Remove(CellIndex);
	}

	if (bMatchesGeneratedState)
	{
		return;
	}

	const int32 NewItemIndex = Items.Add(MoveTemp(NewState));
	CellToItemIndex.Add(CellIndex, NewItemIndex);
	MarkItemDirty(Items[NewItemIndex]);
}

bool UMD_ScrapeSurfaceComponent::TryBuildReplicatedCellState(
	const int32 CellIndex,
	FMD_ReplicatedScrapeCellState& OutState,
	bool& bOutMatchesGeneratedState) const
{
	OutState = FMD_ReplicatedScrapeCellState();
	bOutMatchesGeneratedState = false;
	if (!IsContaminationCellActive(CellIndex))
	{
		return false;
	}

	uint32 EncodedCellIndex = FMD_ScrapeReplicationCodec::InvalidCellIndex;
	if (!FMD_ScrapeReplicationCodec::TryEncodeCellIndex(
			CellIndex,
			EncodedCellIndex))
	{
		return false;
	}

	const int32 StorageIndex = GetContaminationStorageIndex(CellIndex);
	const FMD_ScrapeSurfaceCell Cell = ReadContaminationStorageCell(StorageIndex);
	const float InitialContamination = FMath::Clamp(
		Cell.InitialContamination,
		0.0f,
		1.0f);
	const float SafeRemaining = FMath::Clamp(
		Cell.RemainingContamination,
		0.0f,
		InitialContamination);
	const float SafeTransferred = FMath::Clamp(
		Cell.TransferredContamination,
		0.0f,
		1.0f);
	const float SafeTransferredPatternDensity =
		SafeTransferred > ScrapeSettings.CleanTolerance
		? FMath::Clamp(
			Cell.TransferredPaintPatternDensity,
			0.0f,
			1.0f)
		: 0.0f;
	const float GeneratedInitial =
		HasGeneratedInitialContamination(StorageIndex)
		? FMath::Clamp(
			GetGeneratedInitialContamination(StorageIndex),
			0.0f,
			1.0f)
		: InitialContamination;
	const float SafeCoveredFraction = FMath::Clamp(
		Cell.CoveredFraction,
		0.0f,
		1.0f);
	bOutMatchesGeneratedState = FMath::IsNearlyEqual(
		InitialContamination,
		GeneratedInitial,
		UE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(
			SafeRemaining,
			GeneratedInitial,
			UE_SMALL_NUMBER)
		&& SafeTransferred <= UE_SMALL_NUMBER
		&& SafeTransferredPatternDensity <= UE_SMALL_NUMBER
		&& SafeCoveredFraction <= UE_SMALL_NUMBER;

	// FastArray와 빠른 visual delta가 같은 1/255 표현을 사용해야 나중의
	// 정본 복제가 도착해도 화면이 미세하게 되돌아가지 않는다.
	OutState = FMD_ReplicatedScrapeCellState(
		EncodedCellIndex,
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			InitialContamination,
			0.0f),
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			SafeRemaining,
			ScrapeSettings.CleanTolerance),
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			SafeTransferred,
			ScrapeSettings.CleanTolerance),
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			SafeTransferredPatternDensity,
			0.0f),
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			SafeCoveredFraction,
			0.0f));
	return true;
}

void FMD_ReplicatedScrapeCellStateArray::ResetState()
{
	if (!Items.IsEmpty())
	{
		Items.Reset();
		MarkArrayDirty();
	}

	CellToItemIndex.Reset();
}

void FMD_ReplicatedScrapeCellStateArray::PreReplicatedRemove(
	const TArrayView<int32>& RemovedIndices,
	const int32)
{
	if (Owner)
	{
		Owner->HandleReplicatedContaminationState(RemovedIndices, true);
	}
}

void FMD_ReplicatedScrapeCellStateArray::PostReplicatedAdd(
	const TArrayView<int32>& AddedIndices,
	const int32)
{
	for (const int32 ItemIndex : AddedIndices)
	{
		if (Items.IsValidIndex(ItemIndex))
		{
			const int32 CellIndex = Items[ItemIndex].GetCellIndex();
			if (CellIndex != INDEX_NONE)
			{
				CellToItemIndex.Add(CellIndex, ItemIndex);
			}
		}
	}

	if (Owner)
	{
		Owner->HandleReplicatedContaminationState(AddedIndices, false);
	}
}

void FMD_ReplicatedScrapeCellStateArray::PostReplicatedChange(
	const TArrayView<int32>& ChangedIndices,
	const int32)
{
	if (Owner)
	{
		Owner->HandleReplicatedContaminationState(ChangedIndices, false);
	}
}

void UMD_ScrapeSurfaceComponent::HandleReplicatedContaminationState(
	const TConstArrayView<int32> StateItemIndices,
	const bool bResetToInitial)
{
	if (UsesMeshUVSurface()) { return; }
	if (ReplicatedContaminationVisualRevision != 0
		&& LastAppliedContaminationVisualRevision != 0
		&& IsVisualRevisionNewer(
			LastAppliedContaminationVisualRevision,
			ReplicatedContaminationVisualRevision))
	{
		// revision property는 FastArray보다 먼저 직렬화한다. 이미 더 새
		// transient/canonical 상태를 본 client는 뒤늦은 셀 bunch를 통째로 버린다.
		return;
	}

	const TArray<FMD_ReplicatedScrapeCellState>& AllStateItems =
		ReplicatedContaminationState.GetItems();
	TArray<FMD_ReplicatedScrapeCellState, TInlineAllocator<16>> StateItems;
	StateItems.Reserve(StateItemIndices.Num());
	for (const int32 StateItemIndex : StateItemIndices)
	{
		if (AllStateItems.IsValidIndex(StateItemIndex))
		{
			StateItems.Add(AllStateItems[StateItemIndex]);
		}
	}

	ApplyReplicatedContaminationStates(StateItems, bResetToInitial);
	if (ReplicatedContaminationVisualRevision != 0
		&& (LastAppliedContaminationVisualRevision == 0
			|| IsVisualRevisionNewer(
				ReplicatedContaminationVisualRevision,
				LastAppliedContaminationVisualRevision)))
	{
		LastAppliedContaminationVisualRevision =
			ReplicatedContaminationVisualRevision;
	}
}

void UMD_ScrapeSurfaceComponent::ApplyReplicatedContaminationStates(
	const TConstArrayView<FMD_ReplicatedScrapeCellState> StateItems,
	const bool bResetToInitial)
{
	if (!bContaminationEnabled
		|| HasAuthority()
		|| !bContaminationSurfaceInitialized)
	{
		return;
	}

	bool bWasFullyRemoved = false;
	bool bCapturedPreviousCompletion = false;
	TArray<FMD_ScrapeCellChange> CellChanges;
	CellChanges.Reserve(StateItems.Num());
	TSet<int32> ResetPages;
    FMD_ScrapeMeshUVProgress UpdatedProgress = CachedMeshUVProgress;
    bool bIncrementalProgress = UsesMeshUVSurface() && UpdatedProgress.bIsValid
        && UpdatedProgress.CleanTolerance == ScrapeSettings.CleanTolerance;
    const auto CarrierWeights = GetContaminationCellCarrierWeights();

	for (const FMD_ReplicatedScrapeCellState& StateItem : StateItems)
	{
		const int32 StateCellIndex = StateItem.GetCellIndex();
		if (!IsContaminationCellActive(StateCellIndex))
		{
			continue;
		}

		const int32 StorageIndex = GetContaminationStorageIndex(StateCellIndex);
		FMD_ScrapeSurfaceCell Cell = ReadContaminationStorageCell(StorageIndex);
		const float PreviousInitial = Cell.InitialContamination;
		const float PreviousRemaining = Cell.RemainingContamination;
		const float PreviousTransferred = Cell.TransferredContamination;
		const float PreviousTransferredPatternDensity =
			Cell.TransferredPaintPatternDensity;
		const float PreviousCoveredFraction = Cell.CoveredFraction;
		const float GeneratedInitial =
			HasGeneratedInitialContamination(StorageIndex)
			? FMath::Clamp(
				GetGeneratedInitialContamination(StorageIndex),
				0.0f,
				1.0f)
			: PreviousInitial;
		const float ReceivedInitial = StateItem.GetInitialContamination();
		const float ReceivedRemaining = StateItem.GetRemainingContamination();
		const float ReceivedTransferred =
			StateItem.GetTransferredContamination();
		const float ReceivedTransferredPatternDensity =
			StateItem.GetTransferredPatternDensity();
		const float TargetInitial = bResetToInitial
			? GeneratedInitial
			: (FMath::IsFinite(ReceivedInitial)
				? FMath::Clamp(ReceivedInitial, 0.0f, 1.0f)
				: GeneratedInitial);
		const float TargetRemaining = bResetToInitial
			? GeneratedInitial
			: (FMath::IsFinite(ReceivedRemaining)
				? FMath::Clamp(
					ReceivedRemaining,
					0.0f,
					TargetInitial)
				: TargetInitial);
		const float TargetTransferred = bResetToInitial
			? 0.0f
			: (FMath::IsFinite(ReceivedTransferred)
				? FMath::Clamp(
					ReceivedTransferred,
					0.0f,
					1.0f)
				: 0.0f);
		const float TargetTransferredPatternDensity =
			TargetTransferred > ScrapeSettings.CleanTolerance
			? (FMath::IsFinite(ReceivedTransferredPatternDensity)
				? FMath::Clamp(
					ReceivedTransferredPatternDensity,
					0.0f,
					1.0f)
				: 0.0f)
			: 0.0f;
		const float ReceivedCoveredFraction = StateItem.GetCoveredFraction();
		const float TargetCoveredFraction = bResetToInitial
			? 0.0f
			: (FMath::IsFinite(ReceivedCoveredFraction)
				? FMath::Clamp(ReceivedCoveredFraction, 0.0f, 1.0f)
				: 0.0f);

		if (FMath::IsNearlyEqual(
			PreviousInitial,
			TargetInitial,
			UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
			PreviousRemaining,
			TargetRemaining,
			UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				PreviousTransferred,
				TargetTransferred,
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				PreviousTransferredPatternDensity,
				TargetTransferredPatternDensity,
				UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
				PreviousCoveredFraction,
				TargetCoveredFraction,
				UE_SMALL_NUMBER))
		{
			continue;
		}

		if (!bCapturedPreviousCompletion)
		{
			bWasFullyRemoved = IsContaminationFullyRemoved();
			bCapturedPreviousCompletion = true;
		}

        FMD_ScrapeTouchedCell ProgressChange;
        ProgressChange.CellIndex = StateCellIndex;
        ProgressChange.Before = Cell;
		Cell.InitialContamination = TargetInitial;
		Cell.RemainingContamination = TargetRemaining;
		Cell.TransferredContamination = TargetTransferred;
		Cell.TransferredPaintPatternDensity =
			TargetTransferredPatternDensity;
		Cell.CoveredFraction = TargetCoveredFraction;
		SetContaminationStorageCell(StorageIndex, Cell);
        if (bIncrementalProgress)
        {
            bIncrementalProgress = FMD_ScrapeEvaluator::UpdateMeshUVProgressFromTouchedCells(
                UpdatedProgress, MakeArrayView(&ProgressChange, 1), GetContaminationCellView(),
                CarrierWeights, UpdatedProgress);
        }
		if (bResetToInitial && !ContaminationPagedCells.IsEmpty())
		{
			ResetPages.Add(StorageIndex / FMD_ScrapePagedState::PageSize);
		}

        // Intact procedural dirt has the same progress signal before and after
        // transport quantization (remaining / initial == 1). Paint and coverage
        // must also be identical, and neither initial amount may create an override.
        // Commit every received float and the gameplay totals above; only omit a
        // provably unchanged visual update. Solid geometry and predictions retain
        // the full-precision reconciliation path.
        const bool bUnchangedGeneratedVisual = !bResetToInitial && UsesMeshUVSurface()
            && !ContaminationSolidVisual && !HasLocalPredictedContaminationState()
            && ContaminationGeneration.bProcedural && ContaminationGeneration.Version >= 2
            && PreviousInitial > UE_SMALL_NUMBER && TargetInitial > UE_SMALL_NUMBER
            && PreviousInitial == GeneratedInitial && PreviousRemaining == PreviousInitial
            && TargetRemaining == TargetInitial
            && TargetInitial <= GeneratedInitial + .5f / 255.f + 1.e-6f
            && PreviousTransferred == TargetTransferred
            && PreviousTransferredPatternDensity == TargetTransferredPatternDensity
            && PreviousCoveredFraction == TargetCoveredFraction;
        if (bUnchangedGeneratedVisual) { continue; }

		EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
		int32 LocalCellIndex = INDEX_NONE;
		const int32 CellsPerFace = ContaminationGridDimensions.X
			* ContaminationGridDimensions.Y;
		if (!FMD_ScrapeSurfaceLayout::TryDecodeCombinedCellIndex(
				StateCellIndex,
				CellsPerFace,
				SurfaceFace,
				LocalCellIndex))
		{
			continue;
		}

		FMD_ScrapeCellChange& CellChange = CellChanges.AddDefaulted_GetRef();
		CellChange.CellIndex = StateCellIndex;
		CellChange.GridCoordinate = FIntPoint(
			LocalCellIndex % ContaminationGridDimensions.X,
			LocalCellIndex / ContaminationGridDimensions.X);
		CellChange.RemovedAmount = FMath::Max(
			0.0f,
			PreviousRemaining - TargetRemaining)
			+ FMath::Max(
				0.0f,
				PreviousTransferred - TargetTransferred);
		CellChange.RemovedAmount *= static_cast<float>(GetContaminationCellCarrierWeight(StateCellIndex));
		CellChange.RemainingAmountAfter = TargetRemaining;
		CellChange.bBecameClean =
			(PreviousRemaining > ScrapeSettings.CleanTolerance
				|| PreviousTransferred > ScrapeSettings.CleanTolerance)
			&& TargetRemaining <= ScrapeSettings.CleanTolerance
			&& TargetTransferred <= ScrapeSettings.CleanTolerance;
	}

	for (const int32 PageIndex : ResetPages)
	{
		ContaminationPagedCells.CompactUnchangedPageForIndex(PageIndex * FMD_ScrapePagedState::PageSize);
	}
	if (!bCapturedPreviousCompletion)
	{
		return;
	}

    if (bIncrementalProgress) { CommitContaminationMeshUVProgressCache(UpdatedProgress); }
    else { RefreshContaminationCleanRatioCache(); }
	if (!HasLocalPredictedContaminationState())
	{
		UpdateContaminationMask(CellChanges);
	}
	else
	{
		for (const FMD_ScrapeCellChange& CellChange : CellChanges)
		{
			MarkLocalScrapePredictionReconcileCell(CellChange.CellIndex);
		}
	}

	const bool bIsFullyRemoved = IsContaminationFullyRemoved();
	if (!bWasFullyRemoved
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
}

bool UMD_ScrapeSurfaceComponent::IsVisualRevisionNewer(
	const uint32 Candidate,
	const uint32 Baseline)
{
	return Candidate != Baseline
		&& static_cast<int32>(Candidate - Baseline) > 0;
}

void UMD_ScrapeSurfaceComponent::AdvanceContaminationVisualRevision()
{
	check(HasAuthority());
	++ReplicatedContaminationVisualRevision;
	if (ReplicatedContaminationVisualRevision == 0)
	{
		// 0은 아직 visual transaction을 받지 않은 client/JIP 표식으로 남긴다.
		++ReplicatedContaminationVisualRevision;
	}
}

void UMD_ScrapeSurfaceComponent::OnRep_ContaminationVisualRevision()
{
	// MeshUV revision is only an advertisement; bounded snapshots own canonical state.
	if (UsesMeshUVSurface()) { return; }
	if (ReplicatedContaminationVisualRevision != 0
		&& (LastAppliedContaminationVisualRevision == 0
			|| IsVisualRevisionNewer(
				ReplicatedContaminationVisualRevision,
				LastAppliedContaminationVisualRevision)))
	{
		// 실제 셀 데이터는 FastArray callback이 적용한다. 여기서는 더 늦게 온
		// unreliable delta가 이전 화면으로 되감지 못하도록 순서 바닥만 올린다.
		LastAppliedContaminationVisualRevision =
			ReplicatedContaminationVisualRevision;
	}
}

bool UMD_ScrapeSurfaceComponent::BuildAuthoritativeVisualDelta(
	const TArray<FMD_ScrapeCellChange>& CellChanges,
	const float ScrapeLoadAfter,
	FMD_ScrapeAuthoritativeVisualDelta& OutDelta)
{
	OutDelta = FMD_ScrapeAuthoritativeVisualDelta();
	if (!HasAuthority()
		|| !bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| !FMath::IsFinite(ScrapeLoadAfter)
		|| ScrapeLoadAfter < 0.0f)
	{
		return false;
	}

	TSet<int32> UniqueCellIndices;
	UniqueCellIndices.Reserve(CellChanges.Num());
	OutDelta.CellStates.Reserve(CellChanges.Num());
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (UniqueCellIndices.Contains(CellChange.CellIndex))
		{
			continue;
		}
		UniqueCellIndices.Add(CellChange.CellIndex);

		FMD_ReplicatedScrapeCellState State;
		bool bMatchesGeneratedState = false;
		if (!TryBuildReplicatedCellState(
				CellChange.CellIndex,
				State,
				bMatchesGeneratedState))
		{
			OutDelta = FMD_ScrapeAuthoritativeVisualDelta();
			return false;
		}
		OutDelta.CellStates.Add(MoveTemp(State));
	}

	// Bare-surface feedback can be a valid transaction without a cell delta.
	// It still needs a unique sequence number so duplicate RPC feedback is rejected.
	if (CellChanges.IsEmpty()
		|| ReplicatedContaminationVisualRevision == 0)
	{
		AdvanceContaminationVisualRevision();
		if (AActor* const OwnerActor = GetOwner())
		{
			OwnerActor->ForceNetUpdate();
		}
	}

	OutDelta.Surface = this;
	OutDelta.SurfaceVisualRevision =
		ReplicatedContaminationVisualRevision;
	OutDelta.ScrapeLoadAfter = ScrapeLoadAfter;
	return OutDelta.SurfaceVisualRevision != 0;
}

bool UMD_ScrapeSurfaceComponent::ValidateAuthoritativeVisualDeltaCells(
	const FMD_ScrapeAuthoritativeVisualDelta& Delta) const
{
	TSet<int32> UniqueCellIndices;
	UniqueCellIndices.Reserve(Delta.CellStates.Num());
	for (const FMD_ReplicatedScrapeCellState& State : Delta.CellStates)
	{
		const int32 CellIndex = State.GetCellIndex();
		if (!IsContaminationCellActive(CellIndex)
			|| UniqueCellIndices.Contains(CellIndex))
		{
			return false;
		}
		UniqueCellIndices.Add(CellIndex);
	}
	return true;
}

bool UMD_ScrapeSurfaceComponent::ApplyAuthoritativeVisualDelta(
	const FMD_ScrapeAuthoritativeVisualDelta& Delta)
{
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| HasAuthority()
		|| Delta.Surface != this
		|| Delta.SurfaceVisualRevision == 0
		|| (LastConsumedContaminationVisualRevision != 0
			&& !IsVisualRevisionNewer(
				Delta.SurfaceVisualRevision,
				LastConsumedContaminationVisualRevision))
		|| (LastAppliedContaminationVisualRevision != 0
			&& IsVisualRevisionNewer(
				LastAppliedContaminationVisualRevision,
				Delta.SurfaceVisualRevision))
		|| !ValidateAuthoritativeVisualDeltaCells(Delta))
	{
		return false;
	}

	if (UsesMeshUVSurface())
	{
		if (UMD_ScrapeMeshUVSyncSubsystem* Sync = GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>())
		{ Sync->RecordClientVisualDelta(Delta); }
	}
	ApplyReplicatedContaminationStates(Delta.CellStates, false);
	LastAppliedContaminationVisualRevision = Delta.SurfaceVisualRevision;
	LastConsumedContaminationVisualRevision = Delta.SurfaceVisualRevision;
	return true;
}

bool UMD_ScrapeSurfaceComponent::ApplyAuthoritativePredictionResolution(
	const FMD_ScrapeAuthoritativeVisualDelta& Delta)
{
	if (!bContaminationEnabled
		|| !bContaminationSurfaceInitialized
		|| HasAuthority()
		|| Delta.Surface != this
		|| Delta.SurfaceVisualRevision == 0
		|| (LastAppliedContaminationVisualRevision != 0
			&& IsVisualRevisionNewer(
				LastAppliedContaminationVisualRevision,
				Delta.SurfaceVisualRevision))
		|| !ValidateAuthoritativeVisualDeltaCells(Delta))
	{
		return false;
	}

	if (UsesMeshUVSurface())
	{
		if (UMD_ScrapeMeshUVSyncSubsystem* Sync = GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>())
		{ Sync->RecordClientVisualDelta(Delta); }
	}
	ApplyReplicatedContaminationStates(Delta.CellStates, false);
	LastAppliedContaminationVisualRevision = Delta.SurfaceVisualRevision;
	return true;
}


void UMD_ScrapeSurfaceComponent::ApplyAllReplicatedContaminationState()
{
	if (!bContaminationEnabled || UsesMeshUVSurface())
	{
		return;
	}

	const TArray<FMD_ReplicatedScrapeCellState>& StateItems =
		ReplicatedContaminationState.GetItems();
	TArray<int32> StateItemIndices;
	StateItemIndices.Reserve(StateItems.Num());

	for (int32 StateItemIndex = 0;
		StateItemIndex < StateItems.Num();
		++StateItemIndex)
	{
		StateItemIndices.Add(StateItemIndex);
	}

	HandleReplicatedContaminationState(StateItemIndices, false);
	OnRep_ContaminationVisualRevision();
}


void UMD_ScrapeSurfaceComponent::PublishContaminationChanges(
	const TArray<FMD_ScrapeCellChange>& CellChanges, bool bBoundaryChanged)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapePublish);

	if (!bContaminationEnabled
		|| !HasAuthority()
		|| (CellChanges.IsEmpty() && !bBoundaryChanged))
	{
		return;
	}

	bool bPublishedAnyCell = bBoundaryChanged;
	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (IsContaminationCellActive(CellChange.CellIndex))
		{
			if (!UsesMeshUVSurface())
			{ ReplicatedContaminationState.SetCellState(CellChange.CellIndex); }
			bPublishedAnyCell = true;
		}
	}
	if (!bPublishedAnyCell)
	{
		return;
	}
	AdvanceContaminationVisualRevision();
	if (UsesMeshUVSurface())
	{
		if (UMD_ScrapeMeshUVSyncSubsystem* Sync = GetWorld()->GetSubsystem<UMD_ScrapeMeshUVSyncSubsystem>())
		{ Sync->RecordSurfaceChanges(*this, CellChanges,bBoundaryChanged); }
	}

	if (AActor* const OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
}
