// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"
#include "GameFramework/Pawn.h"

void UMD_ScrapeToolComponent::ProcessLocalPredictedScrapeAim(
	const FMD_ScrapeAimNetSample& Sample,
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime)
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn
		|| OwnerPawn->HasAuthority()
		|| !OwnerPawn->IsLocallyControlled()
		|| !bIsScraping
		|| Sample.FocusSessionId == 0
		|| Sample.Sequence == 0
		|| Sample.FocusSessionId
			!= ScrapeFocusSession.ActiveScrapeFocusSessionId)
	{
		return;
	}

	ActiveLocalScrapePredictionSessionId = Sample.FocusSessionId;
	ActiveLocalScrapePredictionAimSequence = Sample.Sequence;
	CurrentScrapeAim = AimResult;
	ProcessScrapeAimResult(AimResult, SampleDeltaTime, true, 0);
	ActiveLocalScrapePredictionSessionId = 0;
	ActiveLocalScrapePredictionAimSequence = 0;
	for (const TWeakObjectPtr<UMD_ScrapeSurfaceComponent>& WeakSurface
		: LocalPredictedScrapeSurfaces)
	{
		if (UMD_ScrapeSurfaceComponent* const Surface = WeakSurface.Get())
		{
			Surface->FlushLocalScrapePredictionVisualUpdates();
		}
	}
}

void UMD_ScrapeToolComponent::ProcessAuthoritativeScrapeAimResult(
	const FMD_ScrapeAimQueryResult& AimResult,
	const float SampleDeltaTime,
	const bool bSequenceGapKnown,
	const int32 MissingAimSequences,
	const uint64 FocusSessionId,
	const uint32 AimSequence)
{
	if (!GetOwner()
		|| !GetOwner()->HasAuthority()
		|| FocusSessionId == 0
		|| AimSequence == 0)
	{
		return;
	}

	ActiveAuthoritativeScrapePredictionResolutions.Reset();
	ActiveAuthoritativeScrapePredictionSessionId = FocusSessionId;
	ActiveAuthoritativeScrapePredictionAimSequence = AimSequence;
	ProcessScrapeAimResult(
		AimResult,
		SampleDeltaTime,
		bSequenceGapKnown,
		MissingAimSequences);
	SendScrapePredictionResolution(FocusSessionId, AimSequence);
	ActiveAuthoritativeScrapePredictionResolutions.Reset();
	ActiveAuthoritativeScrapePredictionSessionId = 0;
	ActiveAuthoritativeScrapePredictionAimSequence = 0;
}

void UMD_ScrapeToolComponent::AccumulateAuthoritativeScrapePredictionResolution(
	const FMD_ScrapeAuthoritativeVisualDelta& VisualDelta)
{
	if (ActiveAuthoritativeScrapePredictionSessionId == 0
		|| ActiveAuthoritativeScrapePredictionAimSequence == 0
		|| !IsValid(VisualDelta.Surface)
		|| VisualDelta.SurfaceVisualRevision == 0)
	{
		return;
	}

	FActiveAuthoritativeScrapePredictionResolution* Resolution =
		ActiveAuthoritativeScrapePredictionResolutions.FindByPredicate(
			[&VisualDelta](
				const FActiveAuthoritativeScrapePredictionResolution& Candidate)
			{
				return Candidate.VisualDelta.Surface == VisualDelta.Surface;
			});
	if (!Resolution)
	{
		Resolution =
			&ActiveAuthoritativeScrapePredictionResolutions.AddDefaulted_GetRef();
		Resolution->VisualDelta.Surface = VisualDelta.Surface;
	}

	for (const FMD_ReplicatedScrapeCellState& CellState
		: VisualDelta.CellStates)
	{
		const int32 CellIndex = CellState.GetCellIndex();
		if (int32* const ExistingIndex =
			Resolution->CellStateIndices.Find(CellIndex))
		{
			Resolution->VisualDelta.CellStates[*ExistingIndex] = CellState;
			continue;
		}
		const int32 AddedIndex =
			Resolution->VisualDelta.CellStates.Add(CellState);
		Resolution->CellStateIndices.Add(CellIndex, AddedIndex);
	}
	Resolution->VisualDelta.SurfaceVisualRevision =
		VisualDelta.SurfaceVisualRevision;
	Resolution->VisualDelta.ScrapeLoadAfter = VisualDelta.ScrapeLoadAfter;
}

void UMD_ScrapeToolComponent::SendScrapePredictionResolution(
	const uint64 FocusSessionId,
	const uint32 AimSequence)
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn
		|| !OwnerPawn->HasAuthority()
		|| !OwnerPawn->IsPlayerControlled()
		|| OwnerPawn->IsLocallyControlled()
		|| FocusSessionId == 0
		|| AimSequence == 0)
	{
		return;
	}

	if (ActiveAuthoritativeScrapePredictionResolutions.IsEmpty())
	{
		ClientResolveScrapePrediction(
			FocusSessionId,
			AimSequence,
			true,
			{});
		return;
	}

	for (int32 ResolutionIndex = 0;
		ResolutionIndex < ActiveAuthoritativeScrapePredictionResolutions.Num();
		++ResolutionIndex)
	{
		const FMD_ScrapeAuthoritativeVisualDelta& VisualDelta =
			ActiveAuthoritativeScrapePredictionResolutions[ResolutionIndex]
				.VisualDelta;
		const int32 ChunkCount = FMath::Max(
			1,
			FMath::DivideAndRoundUp(
				VisualDelta.CellStates.Num(),
				MaximumAuthoritativePredictionResolutionCellStates));
		for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			const int32 FirstCellStateIndex =
				ChunkIndex * MaximumAuthoritativePredictionResolutionCellStates;
			const int32 CellStateCount = FMath::Min(
				MaximumAuthoritativePredictionResolutionCellStates,
				VisualDelta.CellStates.Num() - FirstCellStateIndex);
			FMD_ScrapeAuthoritativeVisualDelta Chunk;
			Chunk.Surface = VisualDelta.Surface;
			Chunk.SurfaceVisualRevision = VisualDelta.SurfaceVisualRevision;
			Chunk.ScrapeLoadAfter = VisualDelta.ScrapeLoadAfter;
			Chunk.CellStates.Reserve(CellStateCount);
			if (CellStateCount > 0)
			{
				Chunk.CellStates.Append(
					VisualDelta.CellStates.GetData() + FirstCellStateIndex,
					CellStateCount);
			}

			TArray<FMD_ScrapeAuthoritativeVisualDelta> ChunkResolutions;
			ChunkResolutions.Add(MoveTemp(Chunk));
			const bool bFinalChunk =
				ResolutionIndex
					== ActiveAuthoritativeScrapePredictionResolutions.Num() - 1
				&& ChunkIndex == ChunkCount - 1;
			ClientResolveScrapePrediction(
				FocusSessionId,
				AimSequence,
				bFinalChunk,
				ChunkResolutions);
		}
	}
}

void UMD_ScrapeToolComponent::ResolveLocalScrapePredictions(
	const uint64 FocusSessionId,
	const uint32 AimSequence,
	const TArray<FMD_ScrapeAuthoritativeVisualDelta>& Resolutions,
	const bool bRebuildIfPredictionMissing)
{
	if (FocusSessionId == 0 || AimSequence == 0)
	{
		return;
	}

	for (int32 SurfaceIndex = LocalPredictedScrapeSurfaces.Num() - 1;
		SurfaceIndex >= 0;
		--SurfaceIndex)
	{
		UMD_ScrapeSurfaceComponent* const Surface =
			LocalPredictedScrapeSurfaces[SurfaceIndex].Get();
		if (!IsValid(Surface))
		{
			LocalPredictedScrapeSurfaces.RemoveAtSwap(
				SurfaceIndex,
				1,
				EAllowShrinking::No);
			continue;
		}
		const FMD_ScrapeAuthoritativeVisualDelta* const Resolution =
			Resolutions.FindByPredicate(
				[Surface](const FMD_ScrapeAuthoritativeVisualDelta& Candidate)
				{
					return Candidate.Surface == Surface;
				});
		Surface->ResolveLocalScrapePredictions(
			this,
			FocusSessionId,
			AimSequence,
			Resolution,
			bRebuildIfPredictionMissing);
		if (!Surface->HasLocalScrapePredictions(this))
		{
			LocalPredictedScrapeSurfaces.RemoveAtSwap(
				SurfaceIndex,
				1,
				EAllowShrinking::No);
		}
	}
}

void UMD_ScrapeToolComponent::UpdateLocalScrapePredictionResolution(
	const double NowSeconds)
{
	if (!FMath::IsFinite(NowSeconds))
	{
		return;
	}

	const double ExpireBeforeSeconds =
		NowSeconds - LocalScrapePredictionTimeoutSeconds;
	for (int32 SurfaceIndex = LocalPredictedScrapeSurfaces.Num() - 1;
		SurfaceIndex >= 0;
		--SurfaceIndex)
	{
		UMD_ScrapeSurfaceComponent* const Surface =
			LocalPredictedScrapeSurfaces[SurfaceIndex].Get();
		if (!IsValid(Surface))
		{
			LocalPredictedScrapeSurfaces.RemoveAtSwap(
				SurfaceIndex,
				1,
				EAllowShrinking::No);
			continue;
		}
		Surface->ExpireLocalScrapePredictions(this, ExpireBeforeSeconds);
		if (!Surface->HasLocalScrapePredictions(this))
		{
			LocalPredictedScrapeSurfaces.RemoveAtSwap(
				SurfaceIndex,
				1,
				EAllowShrinking::No);
		}
	}
}

void UMD_ScrapeToolComponent::ClearLocalScrapePredictions()
{
	for (const TWeakObjectPtr<UMD_ScrapeSurfaceComponent>& WeakSurface
		: LocalPredictedScrapeSurfaces)
	{
		if (UMD_ScrapeSurfaceComponent* const Surface = WeakSurface.Get())
		{
			Surface->ClearLocalScrapePredictions(this);
		}
	}
	LocalPredictedScrapeSurfaces.Reset();
	ActiveLocalScrapePredictionSessionId = 0;
	ActiveLocalScrapePredictionAimSequence = 0;
	PendingLocalScrapePredictionResolutionChunks.Reset();
	PendingLocalScrapePredictionResolutionSessionId = 0;
	PendingLocalScrapePredictionResolutionAimSequence = 0;
}

void UMD_ScrapeToolComponent::ClientResolveScrapePrediction_Implementation(
	const uint64 FocusSessionId,
	const uint32 AimSequence,
	const bool bFinalChunk,
	const TArray<FMD_ScrapeAuthoritativeVisualDelta>& Resolutions)
{
	if (FocusSessionId == 0 || AimSequence == 0)
	{
		return;
	}

	if (Resolutions.Num() > 1)
	{
		return;
	}
	if (PendingLocalScrapePredictionResolutionSessionId != FocusSessionId
		|| PendingLocalScrapePredictionResolutionAimSequence != AimSequence)
	{
		PendingLocalScrapePredictionResolutionChunks.Reset();
		PendingLocalScrapePredictionResolutionSessionId = FocusSessionId;
		PendingLocalScrapePredictionResolutionAimSequence = AimSequence;
	}

	for (const FMD_ScrapeAuthoritativeVisualDelta& Resolution : Resolutions)
	{
		if (!IsValid(Resolution.Surface)
			|| !FMath::IsFinite(Resolution.ScrapeLoadAfter)
			|| Resolution.ScrapeLoadAfter < 0.0f
			|| Resolution.ScrapeLoadAfter > MaximumScrapeLoad)
		{
			PendingLocalScrapePredictionResolutionChunks.Reset();
			return;
		}

		FMD_ScrapeAuthoritativeVisualDelta* PendingResolution =
			PendingLocalScrapePredictionResolutionChunks.FindByPredicate(
				[&Resolution](
					const FMD_ScrapeAuthoritativeVisualDelta& Candidate)
				{
					return Candidate.Surface == Resolution.Surface;
				});
		if (!PendingResolution)
		{
			PendingResolution =
				&PendingLocalScrapePredictionResolutionChunks
					.AddDefaulted_GetRef();
			PendingResolution->Surface = Resolution.Surface;
			PendingResolution->SurfaceVisualRevision =
				Resolution.SurfaceVisualRevision;
			PendingResolution->ScrapeLoadAfter =
				Resolution.ScrapeLoadAfter;
		}
		if (PendingResolution->SurfaceVisualRevision
				!= Resolution.SurfaceVisualRevision
			|| PendingResolution->CellStates.Num()
					> static_cast<int32>(MAX_uint16)
						- Resolution.CellStates.Num())
		{
			PendingLocalScrapePredictionResolutionChunks.Reset();
			return;
		}
		PendingResolution->ScrapeLoadAfter = Resolution.ScrapeLoadAfter;
		PendingResolution->CellStates.Append(Resolution.CellStates);
	}
	if (!bFinalChunk)
	{
		return;
	}

	TArray<FMD_ScrapeAuthoritativeVisualDelta> FinalResolutions =
		MoveTemp(PendingLocalScrapePredictionResolutionChunks);
	PendingLocalScrapePredictionResolutionSessionId = 0;
	PendingLocalScrapePredictionResolutionAimSequence = 0;
	bool bAppliedAllResolutions = true;
	for (const FMD_ScrapeAuthoritativeVisualDelta& Resolution
		: FinalResolutions)
	{
		if (!IsValid(Resolution.Surface)
			|| !Resolution.Surface->ApplyAuthoritativePredictionResolution(
				Resolution))
		{
			bAppliedAllResolutions = false;
		}
	}
	if (bAppliedAllResolutions)
	{
		ResolveLocalScrapePredictions(
			FocusSessionId,
			AimSequence,
			FinalResolutions);
	}
	else
	{
		ResolveLocalScrapePredictions(
			FocusSessionId,
			AimSequence,
			{},
			true);
	}
}
