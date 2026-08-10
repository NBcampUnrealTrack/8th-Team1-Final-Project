#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponentInternal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Scrape/MD_ScrapeReplicationCodec.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: Fast Array 셀 복제 · 수신 상태 반영

using namespace MD_ScrapeSurfaceComponentInternal;

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
	if (!Owner
		|| !Owner->HasAuthority()
		|| !Owner->ContaminationCells.IsValidIndex(CellIndex))
	{
		return;
	}

	uint16 EncodedCellIndex = FMD_ScrapeReplicationCodec::InvalidCellIndex;
	if (!FMD_ScrapeReplicationCodec::TryEncodeCellIndex(
			CellIndex,
			EncodedCellIndex))
	{
		return;
	}

	const FMD_ScrapeSurfaceCell& Cell = Owner->ContaminationCells[CellIndex];
	const float InitialContamination = FMath::Clamp(
		Cell.InitialContamination,
		0.0f,
		1.0f);
	const float SafeRemaining = FMath::Clamp(
		Cell.RemainingContamination,
		0.0f,
		InitialContamination);
	const float GeneratedInitial =
		Owner->ContaminationGeneratedInitialAmounts.IsValidIndex(CellIndex)
		? FMath::Clamp(
			Owner->ContaminationGeneratedInitialAmounts[CellIndex],
			0.0f,
			1.0f)
		: InitialContamination;
	const bool bMatchesGeneratedState = FMath::IsNearlyEqual(
		InitialContamination,
		GeneratedInitial,
		UE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(
		SafeRemaining,
		GeneratedInitial,
		UE_SMALL_NUMBER);
	// 실제로 보내는 값은 양자화 결과다. 같은 바이트로 떨어지는 갱신은
	// 네트워크로 내보내지 않는다.
	const uint8 QuantizedInitial =
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			InitialContamination,
			0.0f);
	const uint8 QuantizedRemaining =
		FMD_ScrapeReplicationCodec::QuantizeRemaining(
			SafeRemaining,
			Owner->ScrapeSettings.CleanTolerance);

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
			if (Item.QuantizedInitialContamination != QuantizedInitial
				|| Item.QuantizedRemainingContamination != QuantizedRemaining)
			{
				Item.QuantizedInitialContamination = QuantizedInitial;
				Item.QuantizedRemainingContamination = QuantizedRemaining;
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

	const int32 NewItemIndex = Items.Emplace(
		EncodedCellIndex,
		QuantizedInitial,
		QuantizedRemaining);
	CellToItemIndex.Add(CellIndex, NewItemIndex);
	MarkItemDirty(Items[NewItemIndex]);
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
	if (HasAuthority() || !bContaminationSurfaceInitialized)
	{
		return;
	}

	const bool bWasFullyRemoved = IsContaminationFullyRemoved();
	const TArray<FMD_ReplicatedScrapeCellState>& StateItems =
		ReplicatedContaminationState.GetItems();
	TArray<FMD_ScrapeCellChange> CellChanges;
	CellChanges.Reserve(StateItemIndices.Num());

	for (const int32 StateItemIndex : StateItemIndices)
	{
		if (!StateItems.IsValidIndex(StateItemIndex))
		{
			continue;
		}

		const FMD_ReplicatedScrapeCellState& StateItem =
			StateItems[StateItemIndex];
		const int32 StateCellIndex = StateItem.GetCellIndex();
		if (!ContaminationCells.IsValidIndex(StateCellIndex))
		{
			continue;
		}

		FMD_ScrapeSurfaceCell& Cell = ContaminationCells[StateCellIndex];
		const float PreviousInitial = Cell.InitialContamination;
		const float PreviousRemaining = Cell.RemainingContamination;
		const float GeneratedInitial =
			ContaminationGeneratedInitialAmounts.IsValidIndex(StateCellIndex)
			? FMath::Clamp(
				ContaminationGeneratedInitialAmounts[StateCellIndex],
				0.0f,
				1.0f)
			: PreviousInitial;
		const float ReceivedInitial = StateItem.GetInitialContamination();
		const float ReceivedRemaining = StateItem.GetRemainingContamination();
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

		if (FMath::IsNearlyEqual(
			PreviousInitial,
			TargetInitial,
			UE_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(
			PreviousRemaining,
			TargetRemaining,
			UE_SMALL_NUMBER))
		{
			continue;
		}

		Cell.InitialContamination = TargetInitial;
		Cell.RemainingContamination = TargetRemaining;

		EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
		int32 LocalCellIndex = INDEX_NONE;
		const int32 CellsPerFace = ContaminationCells.Num()
			/ FMD_ScrapeSurfaceLayout::FaceCount;
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
			PreviousRemaining - TargetRemaining);
		CellChange.RemainingAmountAfter = TargetRemaining;
		CellChange.bBecameClean =
			PreviousRemaining > ScrapeSettings.CleanTolerance
			&& TargetRemaining <= ScrapeSettings.CleanTolerance;
	}

	RefreshContaminationCleanRatioCache();
	UpdateContaminationMask(CellChanges);
	UpdateContaminationVisualization(CellChanges);

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


void UMD_ScrapeSurfaceComponent::ApplyAllReplicatedContaminationState()
{
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
}


void UMD_ScrapeSurfaceComponent::PublishContaminationChanges(
	const TArray<FMD_ScrapeCellChange>& CellChanges)
{
	SCOPE_CYCLE_COUNTER(STAT_MD_ScrapePublish);

	if (!HasAuthority() || CellChanges.IsEmpty())
	{
		return;
	}

	for (const FMD_ScrapeCellChange& CellChange : CellChanges)
	{
		if (ContaminationCells.IsValidIndex(CellChange.CellIndex))
		{
			ReplicatedContaminationState.SetCellState(CellChange.CellIndex);
		}
	}

	if (AActor* const OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
}
