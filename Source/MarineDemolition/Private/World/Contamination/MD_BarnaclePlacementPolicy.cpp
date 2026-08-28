#include "World/Contamination/MD_BarnaclePlacementPolicy.h"

#include "Math/RandomStream.h"

namespace
{
	constexpr double SurfaceAxisAlignmentThreshold = 0.8;
	constexpr int32 FrontFaceIndex = 0;
	constexpr int32 BackFaceIndex = 1;
	constexpr int32 TopFaceIndex = 2;
	constexpr int32 BottomFaceIndex = 3;
	constexpr int32 LeftFaceIndex = 4;
	constexpr int32 RightFaceIndex = 5;
	constexpr int32 PlacementFaceCount = 6;
	constexpr int32 PlacementPairCount = 3;
	constexpr int32 MaximumEnumeratedPlacementCount = 30;
	constexpr int32 PairFaceIndices[PlacementPairCount][2] = {
		{ FrontFaceIndex, BackFaceIndex },
		{ TopFaceIndex, BottomFaceIndex },
		{ LeftFaceIndex, RightFaceIndex } };

	struct FPlacementCountChoice
	{
		int32 Counts[PlacementFaceCount] = {};
		int32 NewTotal = 0;
		int32 BalancedPairTotalComparisonCount = 0;
		int32 BalancedInternalPairCount = 0;
		int64 PreferenceDistance = 0;
		double PairBalanceViolation = 0.0;
		double InternalBalanceViolation = 0.0;
		bool bHasRequiredCoverage = false;
		bool bPairTotalsBalanced = true;
		bool bFrontBackBalanced = true;
		bool bTopBottomBalanced = true;
		bool bLeftRightBalanced = true;
		bool bMeetsAllConstraints = false;
	};

	int32 GetFaceIndex(const EMD_BarnaclePlacementFace Face)
	{
		switch (Face)
		{
		case EMD_BarnaclePlacementFace::Front:
			return FrontFaceIndex;
		case EMD_BarnaclePlacementFace::Back:
			return BackFaceIndex;
		case EMD_BarnaclePlacementFace::Top:
			return TopFaceIndex;
		case EMD_BarnaclePlacementFace::Bottom:
			return BottomFaceIndex;
		case EMD_BarnaclePlacementFace::Left:
			return LeftFaceIndex;
		case EMD_BarnaclePlacementFace::Right:
			return RightFaceIndex;
		default:
			return INDEX_NONE;
		}
	}

	bool AreOppositeFaces(const int32 FirstFaceIndex, const int32 SecondFaceIndex)
	{
		return (FirstFaceIndex == FrontFaceIndex && SecondFaceIndex == BackFaceIndex)
			|| (FirstFaceIndex == BackFaceIndex && SecondFaceIndex == FrontFaceIndex)
			|| (FirstFaceIndex == TopFaceIndex && SecondFaceIndex == BottomFaceIndex)
			|| (FirstFaceIndex == BottomFaceIndex && SecondFaceIndex == TopFaceIndex)
			|| (FirstFaceIndex == LeftFaceIndex && SecondFaceIndex == RightFaceIndex)
			|| (FirstFaceIndex == RightFaceIndex && SecondFaceIndex == LeftFaceIndex);
	}

	bool IsAcceptablyBalanced(
		const int64 FirstCount,
		const int64 SecondCount,
		const bool bAllowSmallCountApproximation)
	{
		const int64 TotalCount = FirstCount + SecondCount;
		if (TotalCount <= 0)
		{
			return true;
		}

		// 4개 이하는 40:60을 정수로 정확히 표현하지 못할 수 있으므로 가장 가까운
		// 정수 배분(양쪽 차이 1 이하)을 허용한다.
		if (bAllowSmallCountApproximation && TotalCount < 5)
		{
			return FMath::Abs(FirstCount - SecondCount) <= 1;
		}

		return FirstCount * 10 >= TotalCount * 4
			&& FirstCount * 10 <= TotalCount * 6;
	}

	double GetBalanceViolation(
		const int64 FirstCount,
		const int64 SecondCount,
		const bool bAllowSmallCountApproximation)
	{
		if (IsAcceptablyBalanced(
			FirstCount,
			SecondCount,
			bAllowSmallCountApproximation))
		{
			return 0.0;
		}

		const int64 TotalCount = FirstCount + SecondCount;
		if (TotalCount <= 0)
		{
			return 0.0;
		}

		const double FirstShare =
			static_cast<double>(FirstCount) / static_cast<double>(TotalCount);
		if (FirstShare < 0.4)
		{
			return 0.4 - FirstShare;
		}
		if (FirstShare > 0.6)
		{
			return FirstShare - 0.6;
		}
		return 0.0;
	}

	int64 GetPreferenceDistance(
		const int64 FirstCount,
		const int64 SecondCount,
		const int32 PreferredFirstTenths)
	{
		const int64 TotalCount = FirstCount + SecondCount;
		if (TotalCount <= 0)
		{
			return 0;
		}

		return FMath::Abs(
			FirstCount * 10 - TotalCount * static_cast<int64>(PreferredFirstTenths));
	}

	double GetMinimumDistanceSquared(
		const FVector& Location,
		const TArray<FVector>& ReferenceLocations)
	{
		double MinimumDistanceSquared = TNumericLimits<double>::Max();
		for (const FVector& ReferenceLocation : ReferenceLocations)
		{
			MinimumDistanceSquared = FMath::Min(
				MinimumDistanceSquared,
				FVector::DistSquared(Location, ReferenceLocation));
		}
		return MinimumDistanceSquared;
	}

	TArray<int32> SelectSpreadCandidates(
		const TArray<FMD_BarnaclePlacementCandidate>& Candidates,
		const TArray<int32>& EligibleCandidateIndices,
		const TArray<FVector>& ExistingLocations,
		const TArray<FVector>& AdditionalSpreadLocations,
		const int32 RequestedCount,
		const float MinimumSeparationCm,
		FRandomStream& RandomStream)
	{
		TArray<int32> SelectedIndices;
		if (RequestedCount <= 0 || EligibleCandidateIndices.IsEmpty())
		{
			return SelectedIndices;
		}

		TArray<int32> RemainingIndices = EligibleCandidateIndices;
		TArray<FVector> HardSeparationLocations = ExistingLocations;
		TArray<FVector> SpreadReferenceLocations = ExistingLocations;
		SpreadReferenceLocations.Append(AdditionalSpreadLocations);
		SelectedIndices.Reserve(RequestedCount);
		HardSeparationLocations.Reserve(
			HardSeparationLocations.Num() + RequestedCount);
		SpreadReferenceLocations.Reserve(
			SpreadReferenceLocations.Num() + RequestedCount);

		const double SafeMinimumSeparation = FMath::IsFinite(MinimumSeparationCm)
			? FMath::Max(0.0, static_cast<double>(MinimumSeparationCm))
			: 0.0;
		const double MinimumSeparationSquared = FMath::Square(SafeMinimumSeparation);

		while (SelectedIndices.Num() < RequestedCount && !RemainingIndices.IsEmpty())
		{
			int32 BestRemainingArrayIndex = INDEX_NONE;
			double BestDistanceSquared = -1.0;

			if (SpreadReferenceLocations.IsEmpty())
			{
				BestRemainingArrayIndex =
					RandomStream.RandRange(0, RemainingIndices.Num() - 1);
			}
			else
			{
				for (int32 RemainingArrayIndex = 0;
					RemainingArrayIndex < RemainingIndices.Num();
					++RemainingArrayIndex)
				{
					const int32 CandidateIndex = RemainingIndices[RemainingArrayIndex];
					if (!Candidates.IsValidIndex(CandidateIndex))
					{
						continue;
					}

					const double HardDistanceSquared = GetMinimumDistanceSquared(
						Candidates[CandidateIndex].Location,
						HardSeparationLocations);
					if (!HardSeparationLocations.IsEmpty()
						&& HardDistanceSquared + UE_DOUBLE_KINDA_SMALL_NUMBER
						< MinimumSeparationSquared)
					{
						continue;
					}

					const double DistanceSquared = GetMinimumDistanceSquared(
						Candidates[CandidateIndex].Location,
						SpreadReferenceLocations);

					const double DistanceTieTolerance = FMath::Max(
						1.0,
						FMath::Max(
							FMath::Abs(DistanceSquared),
							FMath::Abs(BestDistanceSquared))) * 1.e-9;
					const int32 BestCandidateIndex = BestRemainingArrayIndex != INDEX_NONE
						? RemainingIndices[BestRemainingArrayIndex]
						: INDEX_NONE;
					const bool bWinsDistanceTie = BestCandidateIndex != INDEX_NONE
						&& FMath::Abs(DistanceSquared - BestDistanceSquared)
							<= DistanceTieTolerance
						&& Candidates[CandidateIndex].SourceIndex
							< Candidates[BestCandidateIndex].SourceIndex;
					if (BestRemainingArrayIndex == INDEX_NONE
						|| DistanceSquared > BestDistanceSquared + DistanceTieTolerance
						|| bWinsDistanceTie)
					{
						BestDistanceSquared = DistanceSquared;
						BestRemainingArrayIndex = RemainingArrayIndex;
					}
				}
			}

			if (BestRemainingArrayIndex == INDEX_NONE)
			{
				break;
			}

			const int32 CandidateIndex = RemainingIndices[BestRemainingArrayIndex];
			if (!Candidates.IsValidIndex(CandidateIndex))
			{
				RemainingIndices.RemoveAtSwap(BestRemainingArrayIndex);
				continue;
			}

			SelectedIndices.Add(CandidateIndex);
			HardSeparationLocations.Add(Candidates[CandidateIndex].Location);
			SpreadReferenceLocations.Add(Candidates[CandidateIndex].Location);
			RemainingIndices.RemoveAtSwap(BestRemainingArrayIndex);
		}

		return SelectedIndices;
	}

	void ShuffleIndices(TArray<int32>& Indices, FRandomStream& RandomStream)
	{
		for (int32 Index = Indices.Num() - 1; Index > 0; --Index)
		{
			Indices.Swap(Index, RandomStream.RandRange(0, Index));
		}
	}

	bool IsStrictlyBetterValidChoice(
		const FPlacementCountChoice& Candidate,
		const FPlacementCountChoice& CurrentBest)
	{
		if (Candidate.NewTotal != CurrentBest.NewTotal)
		{
			return Candidate.NewTotal > CurrentBest.NewTotal;
		}
		return Candidate.PreferenceDistance < CurrentBest.PreferenceDistance;
	}

	bool HasEquivalentValidScore(
		const FPlacementCountChoice& Candidate,
		const FPlacementCountChoice& CurrentBest)
	{
		return Candidate.NewTotal == CurrentBest.NewTotal
			&& Candidate.PreferenceDistance == CurrentBest.PreferenceDistance;
	}

	bool IsStrictlyBetterFallbackChoice(
		const FPlacementCountChoice& Candidate,
		const FPlacementCountChoice& CurrentBest)
	{
		if (Candidate.bHasRequiredCoverage != CurrentBest.bHasRequiredCoverage)
		{
			return Candidate.bHasRequiredCoverage;
		}
		if (Candidate.bPairTotalsBalanced != CurrentBest.bPairTotalsBalanced)
		{
			return Candidate.bPairTotalsBalanced;
		}
		if (Candidate.BalancedPairTotalComparisonCount
			!= CurrentBest.BalancedPairTotalComparisonCount)
		{
			return Candidate.BalancedPairTotalComparisonCount
				> CurrentBest.BalancedPairTotalComparisonCount;
		}
		if (Candidate.BalancedInternalPairCount != CurrentBest.BalancedInternalPairCount)
		{
			return Candidate.BalancedInternalPairCount
				> CurrentBest.BalancedInternalPairCount;
		}
		if (!FMath::IsNearlyEqual(
			Candidate.PairBalanceViolation,
			CurrentBest.PairBalanceViolation))
		{
			return Candidate.PairBalanceViolation < CurrentBest.PairBalanceViolation;
		}
		if (!FMath::IsNearlyEqual(
			Candidate.InternalBalanceViolation,
			CurrentBest.InternalBalanceViolation))
		{
			return Candidate.InternalBalanceViolation
				< CurrentBest.InternalBalanceViolation;
		}
		if (Candidate.NewTotal != CurrentBest.NewTotal)
		{
			return Candidate.NewTotal > CurrentBest.NewTotal;
		}
		return Candidate.PreferenceDistance < CurrentBest.PreferenceDistance;
	}

	bool HasEquivalentFallbackScore(
		const FPlacementCountChoice& Candidate,
		const FPlacementCountChoice& CurrentBest)
	{
		return Candidate.bHasRequiredCoverage == CurrentBest.bHasRequiredCoverage
			&& Candidate.bPairTotalsBalanced == CurrentBest.bPairTotalsBalanced
			&& Candidate.BalancedPairTotalComparisonCount
				== CurrentBest.BalancedPairTotalComparisonCount
			&& Candidate.BalancedInternalPairCount
				== CurrentBest.BalancedInternalPairCount
			&& FMath::IsNearlyEqual(
				Candidate.PairBalanceViolation,
				CurrentBest.PairBalanceViolation)
			&& FMath::IsNearlyEqual(
				Candidate.InternalBalanceViolation,
				CurrentBest.InternalBalanceViolation)
			&& Candidate.NewTotal == CurrentBest.NewTotal
			&& Candidate.PreferenceDistance == CurrentBest.PreferenceDistance;
	}

	struct FPairSplitChoice
	{
		int32 FirstCount = 0;
		int32 SecondCount = 0;
		int32 TieCount = 0;
		int64 PreferenceDistance = 0;
		double BalanceViolation = 0.0;
		bool bIsValid = false;
		bool bHasRequiredCoverage = true;
		bool bInternallyBalanced = true;
	};

	bool IsStrictlyBetterPairSplit(
		const FPairSplitChoice& Candidate,
		const FPairSplitChoice& CurrentBest)
	{
		if (Candidate.bHasRequiredCoverage != CurrentBest.bHasRequiredCoverage)
		{
			return Candidate.bHasRequiredCoverage;
		}
		if (Candidate.bInternallyBalanced != CurrentBest.bInternallyBalanced)
		{
			return Candidate.bInternallyBalanced;
		}
		if (!FMath::IsNearlyEqual(
			Candidate.BalanceViolation,
			CurrentBest.BalanceViolation))
		{
			return Candidate.BalanceViolation < CurrentBest.BalanceViolation;
		}
		return Candidate.PreferenceDistance < CurrentBest.PreferenceDistance;
	}

	bool HasEquivalentPairSplitScore(
		const FPairSplitChoice& Candidate,
		const FPairSplitChoice& CurrentBest)
	{
		return Candidate.bHasRequiredCoverage == CurrentBest.bHasRequiredCoverage
			&& Candidate.bInternallyBalanced == CurrentBest.bInternallyBalanced
			&& FMath::IsNearlyEqual(
				Candidate.BalanceViolation,
				CurrentBest.BalanceViolation)
			&& Candidate.PreferenceDistance == CurrentBest.PreferenceDistance;
	}

	FPlacementCountChoice ChooseSixFaceCounts(
		const int32 (&Capacities)[PlacementFaceCount],
		const int32 (&ExistingCounts)[PlacementFaceCount],
		const bool (&RequiredCoverageFaces)[PlacementFaceCount],
		const int32 SafeRequestedCount,
		const int32 (&PreferredInternalTenths)[PlacementPairCount],
		const int32 (&PreferredPairTenths)[PlacementPairCount][PlacementPairCount],
		FRandomStream& RandomStream,
		bool& bOutFoundChoice)
	{
		FPairSplitChoice PairSplits
			[PlacementPairCount][MaximumEnumeratedPlacementCount + 1] = {};
		bool PairAvailable[PlacementPairCount] = {};
		bool BalanceInternalPair[PlacementPairCount] = {};
		int32 MaximumPairTotals[PlacementPairCount] = {};

		int32 AvailablePairCount = 0;
		for (int32 PairIndex = 0; PairIndex < PlacementPairCount; ++PairIndex)
		{
			const int32 FirstFaceIndex = PairFaceIndices[PairIndex][0];
			const int32 SecondFaceIndex = PairFaceIndices[PairIndex][1];
			PairAvailable[PairIndex] = Capacities[FirstFaceIndex] > 0
				|| Capacities[SecondFaceIndex] > 0;
			BalanceInternalPair[PairIndex] = Capacities[FirstFaceIndex] > 0
				&& Capacities[SecondFaceIndex] > 0;
			AvailablePairCount += PairAvailable[PairIndex] ? 1 : 0;
			MaximumPairTotals[PairIndex] = FMath::Min(
				SafeRequestedCount,
				Capacities[FirstFaceIndex] + Capacities[SecondFaceIndex]);
		}

		const bool bBalancePairTotals = AvailablePairCount >= 2;
		const bool bAllowSmallInternalPairApproximation = bBalancePairTotals
			|| SafeRequestedCount < 5;

		for (int32 PairIndex = 0; PairIndex < PlacementPairCount; ++PairIndex)
		{
			const int32 FirstFaceIndex = PairFaceIndices[PairIndex][0];
			const int32 SecondFaceIndex = PairFaceIndices[PairIndex][1];
			for (int32 NewPairTotal = 0;
				NewPairTotal <= MaximumPairTotals[PairIndex];
				++NewPairTotal)
			{
				FPairSplitChoice& BestSplit = PairSplits[PairIndex][NewPairTotal];
				const int32 MinimumFirstCount = FMath::Max(
					0,
					NewPairTotal - Capacities[SecondFaceIndex]);
				const int32 MaximumFirstCount = FMath::Min(
					NewPairTotal,
					Capacities[FirstFaceIndex]);

				for (int32 FirstCount = MinimumFirstCount;
					FirstCount <= MaximumFirstCount;
					++FirstCount)
				{
					const int32 SecondCount = NewPairTotal - FirstCount;
					const int64 FinalFirstCount =
						static_cast<int64>(ExistingCounts[FirstFaceIndex]) + FirstCount;
					const int64 FinalSecondCount =
						static_cast<int64>(ExistingCounts[SecondFaceIndex]) + SecondCount;

					FPairSplitChoice Candidate;
					Candidate.FirstCount = FirstCount;
					Candidate.SecondCount = SecondCount;
					Candidate.bIsValid = true;
					Candidate.bHasRequiredCoverage =
						(!RequiredCoverageFaces[FirstFaceIndex] || FirstCount > 0)
						&& (!RequiredCoverageFaces[SecondFaceIndex] || SecondCount > 0);
					Candidate.bInternallyBalanced =
						!BalanceInternalPair[PairIndex]
						|| IsAcceptablyBalanced(
							FinalFirstCount,
							FinalSecondCount,
							bAllowSmallInternalPairApproximation);
					Candidate.BalanceViolation = BalanceInternalPair[PairIndex]
						? GetBalanceViolation(
							FinalFirstCount,
							FinalSecondCount,
							bAllowSmallInternalPairApproximation)
						: 0.0;
					Candidate.PreferenceDistance = BalanceInternalPair[PairIndex]
						? GetPreferenceDistance(
							FinalFirstCount,
							FinalSecondCount,
							PreferredInternalTenths[PairIndex])
						: 0;

					if (!BestSplit.bIsValid
						|| IsStrictlyBetterPairSplit(Candidate, BestSplit))
					{
						BestSplit = Candidate;
						BestSplit.TieCount = 1;
					}
					else if (HasEquivalentPairSplitScore(Candidate, BestSplit)
						&& RandomStream.RandRange(1, ++BestSplit.TieCount) == 1)
					{
						const int32 TieCount = BestSplit.TieCount;
						BestSplit = Candidate;
						BestSplit.TieCount = TieCount;
					}
				}
			}
		}

		FPlacementCountChoice BestValidChoice;
		FPlacementCountChoice BestFallbackChoice;
		bool bFoundValidChoice = false;
		bool bFoundFallbackChoice = false;
		int32 ValidTieCount = 0;
		int32 FallbackTieCount = 0;

		for (int32 FrontBackTotal = 0;
			FrontBackTotal <= MaximumPairTotals[0];
			++FrontBackTotal)
		{
			for (int32 TopBottomTotal = 0;
				TopBottomTotal <= MaximumPairTotals[1]
					&& FrontBackTotal + TopBottomTotal <= SafeRequestedCount;
				++TopBottomTotal)
			{
				const int32 MaximumLeftRightTotal = FMath::Min(
					MaximumPairTotals[2],
					SafeRequestedCount - FrontBackTotal - TopBottomTotal);
				for (int32 LeftRightTotal = 0;
					LeftRightTotal <= MaximumLeftRightTotal;
					++LeftRightTotal)
				{
					const int32 PairTotals[PlacementPairCount] = {
						FrontBackTotal,
						TopBottomTotal,
						LeftRightTotal };
					FPlacementCountChoice Choice;
					Choice.bHasRequiredCoverage = true;
					Choice.bPairTotalsBalanced = true;

					int64 FinalPairTotals[PlacementPairCount] = {};
					for (int32 PairIndex = 0;
						PairIndex < PlacementPairCount;
						++PairIndex)
					{
						const FPairSplitChoice& Split =
							PairSplits[PairIndex][PairTotals[PairIndex]];
						if (!Split.bIsValid)
						{
							Choice.NewTotal = 0;
							break;
						}

						const int32 FirstFaceIndex = PairFaceIndices[PairIndex][0];
						const int32 SecondFaceIndex = PairFaceIndices[PairIndex][1];
						Choice.Counts[FirstFaceIndex] = Split.FirstCount;
						Choice.Counts[SecondFaceIndex] = Split.SecondCount;
						Choice.NewTotal += PairTotals[PairIndex];
						Choice.bHasRequiredCoverage &= Split.bHasRequiredCoverage;
						Choice.BalancedInternalPairCount +=
							BalanceInternalPair[PairIndex]
								&& Split.bInternallyBalanced ? 1 : 0;
						Choice.InternalBalanceViolation += Split.BalanceViolation;
						Choice.PreferenceDistance += Split.PreferenceDistance;
						FinalPairTotals[PairIndex] =
							static_cast<int64>(ExistingCounts[FirstFaceIndex])
							+ ExistingCounts[SecondFaceIndex]
							+ PairTotals[PairIndex];
					}

					if (Choice.NewTotal <= 0)
					{
						continue;
					}

					for (int32 FirstPairIndex = 0;
						FirstPairIndex < PlacementPairCount;
						++FirstPairIndex)
					{
						if (!PairAvailable[FirstPairIndex])
						{
							continue;
						}
						for (int32 SecondPairIndex = FirstPairIndex + 1;
							SecondPairIndex < PlacementPairCount;
							++SecondPairIndex)
						{
							if (!PairAvailable[SecondPairIndex])
							{
								continue;
							}

							const bool bComparisonBalanced = IsAcceptablyBalanced(
								FinalPairTotals[FirstPairIndex],
								FinalPairTotals[SecondPairIndex],
								true);
							Choice.bPairTotalsBalanced &= bComparisonBalanced;
							Choice.BalancedPairTotalComparisonCount +=
								bComparisonBalanced ? 1 : 0;
							Choice.PairBalanceViolation += GetBalanceViolation(
								FinalPairTotals[FirstPairIndex],
								FinalPairTotals[SecondPairIndex],
								true);
							Choice.PreferenceDistance += GetPreferenceDistance(
								FinalPairTotals[FirstPairIndex],
								FinalPairTotals[SecondPairIndex],
								PreferredPairTenths[FirstPairIndex][SecondPairIndex]);
						}
					}

					Choice.bFrontBackBalanced =
						PairSplits[0][FrontBackTotal].bInternallyBalanced;
					Choice.bTopBottomBalanced =
						PairSplits[1][TopBottomTotal].bInternallyBalanced;
					Choice.bLeftRightBalanced =
						PairSplits[2][LeftRightTotal].bInternallyBalanced;
					Choice.bMeetsAllConstraints = Choice.bHasRequiredCoverage
						&& Choice.bPairTotalsBalanced
						&& Choice.bFrontBackBalanced
						&& Choice.bTopBottomBalanced
						&& Choice.bLeftRightBalanced;

					if (Choice.bMeetsAllConstraints)
					{
						if (!bFoundValidChoice
							|| IsStrictlyBetterValidChoice(Choice, BestValidChoice))
						{
							BestValidChoice = Choice;
							bFoundValidChoice = true;
							ValidTieCount = 1;
						}
						else if (HasEquivalentValidScore(Choice, BestValidChoice)
							&& RandomStream.RandRange(1, ++ValidTieCount) == 1)
						{
							BestValidChoice = Choice;
						}
					}
					else if (!bFoundFallbackChoice
						|| IsStrictlyBetterFallbackChoice(Choice, BestFallbackChoice))
					{
						BestFallbackChoice = Choice;
						bFoundFallbackChoice = true;
						FallbackTieCount = 1;
					}
					else if (HasEquivalentFallbackScore(Choice, BestFallbackChoice)
						&& RandomStream.RandRange(1, ++FallbackTieCount) == 1)
					{
						BestFallbackChoice = Choice;
					}
				}
			}
		}

		bOutFoundChoice = bFoundValidChoice || bFoundFallbackChoice;
		return bFoundValidChoice ? BestValidChoice : BestFallbackChoice;
	}
}

EMD_BarnaclePlacementFace MDBarnaclePlacementPolicy::ClassifySurfaceFace(
	const FVector& SurfaceNormal,
	const FVector& ForwardAxis,
	const FVector& RightAxis,
	const FVector& UpAxis,
	const bool bAllowFrontBack,
	const bool bAllowLeftRight,
	const bool bAllowTopBottom)
{
	if ((!bAllowFrontBack && !bAllowLeftRight && !bAllowTopBottom)
		|| SurfaceNormal.ContainsNaN()
		|| ForwardAxis.ContainsNaN()
		|| RightAxis.ContainsNaN()
		|| UpAxis.ContainsNaN())
	{
		return EMD_BarnaclePlacementFace::Other;
	}

	const FVector SafeSurfaceNormal = SurfaceNormal.GetSafeNormal();
	const FVector SafeForwardAxis = ForwardAxis.GetSafeNormal();
	const FVector SafeRightAxis = RightAxis.GetSafeNormal();
	const FVector SafeUpAxis = UpAxis.GetSafeNormal();
	if (SafeSurfaceNormal.IsNearlyZero()
		|| (bAllowFrontBack && SafeForwardAxis.IsNearlyZero())
		|| (bAllowLeftRight && SafeRightAxis.IsNearlyZero())
		|| (bAllowTopBottom && SafeUpAxis.IsNearlyZero()))
	{
		return EMD_BarnaclePlacementFace::Other;
	}

	const double ForwardAlignment = bAllowFrontBack
		? FVector::DotProduct(SafeSurfaceNormal, SafeForwardAxis)
		: 0.0;
	const double RightAlignment = bAllowLeftRight
		? FVector::DotProduct(SafeSurfaceNormal, SafeRightAxis)
		: 0.0;
	const double UpAlignment = bAllowTopBottom
		? FVector::DotProduct(SafeSurfaceNormal, SafeUpAxis)
		: 0.0;
	const double ForwardScore = bAllowFrontBack
		? FMath::Abs(ForwardAlignment)
		: -1.0;
	const double RightScore = bAllowLeftRight
		? FMath::Abs(RightAlignment)
		: -1.0;
	const double UpScore = bAllowTopBottom
		? FMath::Abs(UpAlignment)
		: -1.0;

	if (ForwardScore < SurfaceAxisAlignmentThreshold
		&& RightScore < SurfaceAxisAlignmentThreshold
		&& UpScore < SurfaceAxisAlignmentThreshold)
	{
		return EMD_BarnaclePlacementFace::Other;
	}

	// 정상적인 스포너 축은 서로 직교하므로 한 축만 임계값을 넘는다. 비정상 축이
	// 들어오면 더 강하게 정렬된 축을 택하고, 동률은 앞/뒤, 좌/우, 위/아래 순이다.
	if (ForwardScore >= RightScore && ForwardScore >= UpScore)
	{
		return ForwardAlignment >= 0.0
			? EMD_BarnaclePlacementFace::Front
			: EMD_BarnaclePlacementFace::Back;
	}

	if (RightScore >= UpScore)
	{
		return RightAlignment >= 0.0
			? EMD_BarnaclePlacementFace::Right
			: EMD_BarnaclePlacementFace::Left;
	}

	return UpAlignment >= 0.0
		? EMD_BarnaclePlacementFace::Top
		: EMD_BarnaclePlacementFace::Bottom;
}

EMD_BarnaclePlacementFace MDBarnaclePlacementPolicy::ClassifySurfaceFace(
	const FVector& SurfaceNormal,
	const FVector& ForwardAxis,
	const FVector& UpAxis,
	const bool bAllowFrontBack,
	const bool bAllowTopBottom)
{
	return ClassifySurfaceFace(
		SurfaceNormal,
		ForwardAxis,
		FVector::ZeroVector,
		UpAxis,
		bAllowFrontBack,
		false,
		bAllowTopBottom);
}

FMD_BarnaclePlacementPlan MDBarnaclePlacementPolicy::BuildPlan(
	const TArray<FMD_BarnaclePlacementCandidate>& Candidates,
	const TArray<FMD_BarnaclePlacementCandidate>& ExistingPlacements,
	const int32 RequestedCount,
	const float MinimumSeparationCm,
	FRandomStream& RandomStream)
{
	FMD_BarnaclePlacementPlan Plan;
	if (RequestedCount <= 0 || Candidates.IsEmpty())
	{
		return Plan;
	}

	// 프로젝트의 기존 배치 예산 상한(30)을 런타임에서도 지킨다. 비정상적으로
	// 큰 값이 면별 수량 조합 수를 폭증시키는 것도 막는다.
	const int32 SafeRequestedCount = FMath::Min3(
		RequestedCount,
		Candidates.Num(),
		MaximumEnumeratedPlacementCount);
	const int32 PreferredFrontBackTenths = RandomStream.RandRange(4, 6);
	const int32 PreferredFrontTenths = RandomStream.RandRange(4, 6);
	const int32 PreferredTopTenths = RandomStream.RandRange(4, 6);

	TArray<int32> FaceCandidateIndices[PlacementFaceCount];
	TArray<FVector> ExistingLocations[PlacementFaceCount];
	int32 ExistingCounts[PlacementFaceCount] = {};

	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FMD_BarnaclePlacementCandidate& Candidate = Candidates[CandidateIndex];
		const int32 FaceIndex = GetFaceIndex(Candidate.Face);
		if (FaceIndex != INDEX_NONE && !Candidate.Location.ContainsNaN())
		{
			FaceCandidateIndices[FaceIndex].Add(CandidateIndex);
		}
	}

	for (const FMD_BarnaclePlacementCandidate& ExistingPlacement : ExistingPlacements)
	{
		const int32 FaceIndex = GetFaceIndex(ExistingPlacement.Face);
		if (FaceIndex == INDEX_NONE)
		{
			continue;
		}

		++ExistingCounts[FaceIndex];
		if (!ExistingPlacement.Location.ContainsNaN())
		{
			ExistingLocations[FaceIndex].Add(ExistingPlacement.Location);
		}
	}

	TArray<int32> FarthestPrefixes[PlacementFaceCount];
	int32 Capacities[PlacementFaceCount] = {};
	for (int32 FaceIndex = 0; FaceIndex < PlacementFaceCount; ++FaceIndex)
	{
		FarthestPrefixes[FaceIndex] = SelectSpreadCandidates(
			Candidates,
			FaceCandidateIndices[FaceIndex],
			ExistingLocations[FaceIndex],
			{},
			SafeRequestedCount,
			MinimumSeparationCm,
			RandomStream);
		Capacities[FaceIndex] = FarthestPrefixes[FaceIndex].Num();
	}

	const bool bFrontBackPairAvailable =
		Capacities[FrontFaceIndex] > 0 || Capacities[BackFaceIndex] > 0;
	const bool bTopBottomPairAvailable =
		Capacities[TopFaceIndex] > 0 || Capacities[BottomFaceIndex] > 0;
	const bool bLeftRightPairAvailable =
		Capacities[LeftFaceIndex] > 0 || Capacities[RightFaceIndex] > 0;
	const bool bBalancePairTotals =
		bFrontBackPairAvailable && bTopBottomPairAvailable;
	const bool bBalanceFrontBack =
		Capacities[FrontFaceIndex] > 0 && Capacities[BackFaceIndex] > 0;
	const bool bBalanceTopBottom =
		Capacities[TopFaceIndex] > 0 && Capacities[BottomFaceIndex] > 0;
	const bool bBalanceLeftRight =
		Capacities[LeftFaceIndex] > 0 && Capacities[RightFaceIndex] > 0;
	const bool bAllowSmallPairTotalApproximation = SafeRequestedCount < 5;
	const bool bAllowSmallInternalPairApproximation =
		bAllowSmallPairTotalApproximation || bBalancePairTotals;
	const bool bRequireAllFourFaces = SafeRequestedCount >= 4
		&& Capacities[FrontFaceIndex] > 0
		&& Capacities[BackFaceIndex] > 0
		&& Capacities[TopFaceIndex] > 0
		&& Capacities[BottomFaceIndex] > 0;

	Plan.bUsedFrontBackBalance = bBalanceFrontBack;
	Plan.bUsedTopBottomBalance = bBalanceTopBottom;
	Plan.bUsedLeftRightBalance = bBalanceLeftRight;

	FPlacementCountChoice ChosenCounts;
	if (!bLeftRightPairAvailable)
	{
		// 좌/우 후보가 없을 때는 기존 4면 탐색과 난수 소비 순서를 그대로 보존한다.
		FPlacementCountChoice BestValidChoice;
		FPlacementCountChoice BestFallbackChoice;
		bool bFoundValidChoice = false;
		bool bFoundFallbackChoice = false;
		int32 ValidTieCount = 0;
		int32 FallbackTieCount = 0;

		for (int32 FrontCount = 0;
			FrontCount <= Capacities[FrontFaceIndex]
				&& FrontCount <= SafeRequestedCount;
			++FrontCount)
		{
			const int32 RemainingAfterFront = SafeRequestedCount - FrontCount;
			for (int32 BackCount = 0;
				BackCount <= Capacities[BackFaceIndex]
					&& BackCount <= RemainingAfterFront;
				++BackCount)
			{
				const int32 RemainingAfterBack = RemainingAfterFront - BackCount;
				for (int32 TopCount = 0;
					TopCount <= Capacities[TopFaceIndex]
						&& TopCount <= RemainingAfterBack;
					++TopCount)
				{
					const int32 MaximumBottomCount = FMath::Min(
						Capacities[BottomFaceIndex],
							RemainingAfterBack - TopCount);
					for (int32 BottomCount = 0;
						BottomCount <= MaximumBottomCount;
						++BottomCount)
					{
						FPlacementCountChoice Choice;
						Choice.Counts[FrontFaceIndex] = FrontCount;
						Choice.Counts[BackFaceIndex] = BackCount;
						Choice.Counts[TopFaceIndex] = TopCount;
						Choice.Counts[BottomFaceIndex] = BottomCount;
						Choice.NewTotal =
							FrontCount + BackCount + TopCount + BottomCount;
						if (Choice.NewTotal <= 0)
						{
							continue;
						}

						Choice.bHasRequiredCoverage = !bRequireAllFourFaces
							|| (FrontCount > 0
								&& BackCount > 0
								&& TopCount > 0
								&& BottomCount > 0);

						const int64 FinalFrontCount =
							static_cast<int64>(ExistingCounts[FrontFaceIndex]) + FrontCount;
						const int64 FinalBackCount =
							static_cast<int64>(ExistingCounts[BackFaceIndex]) + BackCount;
						const int64 FinalTopCount =
							static_cast<int64>(ExistingCounts[TopFaceIndex]) + TopCount;
						const int64 FinalBottomCount =
							static_cast<int64>(ExistingCounts[BottomFaceIndex]) + BottomCount;
						const int64 FinalFrontBackCount = FinalFrontCount + FinalBackCount;
						const int64 FinalTopBottomCount = FinalTopCount + FinalBottomCount;

						Choice.bPairTotalsBalanced = !bBalancePairTotals
							|| IsAcceptablyBalanced(
								FinalFrontBackCount,
								FinalTopBottomCount,
								bAllowSmallPairTotalApproximation);
						Choice.bFrontBackBalanced = !bBalanceFrontBack
							|| IsAcceptablyBalanced(
								FinalFrontCount,
								FinalBackCount,
								bAllowSmallInternalPairApproximation);
						Choice.bTopBottomBalanced = !bBalanceTopBottom
							|| IsAcceptablyBalanced(
								FinalTopCount,
								FinalBottomCount,
								bAllowSmallInternalPairApproximation);
						Choice.BalancedInternalPairCount =
							(bBalanceFrontBack && Choice.bFrontBackBalanced ? 1 : 0)
								+ (bBalanceTopBottom && Choice.bTopBottomBalanced ? 1 : 0);
						Choice.PairBalanceViolation = bBalancePairTotals
							? GetBalanceViolation(
								FinalFrontBackCount,
								FinalTopBottomCount,
								bAllowSmallPairTotalApproximation)
							: 0.0;
						Choice.InternalBalanceViolation =
							(bBalanceFrontBack
								? GetBalanceViolation(
									FinalFrontCount,
									FinalBackCount,
									bAllowSmallInternalPairApproximation)
								: 0.0)
							+ (bBalanceTopBottom
								? GetBalanceViolation(
									FinalTopCount,
									FinalBottomCount,
									bAllowSmallInternalPairApproximation)
								: 0.0);

						Choice.PreferenceDistance =
							(bBalancePairTotals
								? GetPreferenceDistance(
									FinalFrontBackCount,
									FinalTopBottomCount,
									PreferredFrontBackTenths)
								: 0)
							+ (bBalanceFrontBack
								? GetPreferenceDistance(
									FinalFrontCount,
									FinalBackCount,
									PreferredFrontTenths)
								: 0)
							+ (bBalanceTopBottom
								? GetPreferenceDistance(
									FinalTopCount,
									FinalBottomCount,
									PreferredTopTenths)
								: 0);

						Choice.bMeetsAllConstraints = Choice.bHasRequiredCoverage
							&& Choice.bPairTotalsBalanced
							&& Choice.bFrontBackBalanced
							&& Choice.bTopBottomBalanced;

						if (Choice.bMeetsAllConstraints)
						{
							if (!bFoundValidChoice
								|| IsStrictlyBetterValidChoice(Choice, BestValidChoice))
							{
								BestValidChoice = Choice;
								bFoundValidChoice = true;
								ValidTieCount = 1;
							}
							else if (HasEquivalentValidScore(Choice, BestValidChoice)
								&& RandomStream.RandRange(1, ++ValidTieCount) == 1)
							{
								BestValidChoice = Choice;
							}
						}
						else if (!bFoundFallbackChoice
							|| IsStrictlyBetterFallbackChoice(Choice, BestFallbackChoice))
						{
							BestFallbackChoice = Choice;
							bFoundFallbackChoice = true;
							FallbackTieCount = 1;
						}
						else if (HasEquivalentFallbackScore(Choice, BestFallbackChoice)
							&& RandomStream.RandRange(1, ++FallbackTieCount) == 1)
						{
							BestFallbackChoice = Choice;
						}
					}
				}
			}
		}

		if (!bFoundValidChoice && !bFoundFallbackChoice)
		{
			return Plan;
		}

		ChosenCounts = bFoundValidChoice ? BestValidChoice : BestFallbackChoice;
	}
	else
	{
		// 좌/우가 실제 후보를 가진 경우에만 추가 난수를 소비하고 6면 정책을 쓴다.
		const int32 PreferredLeftTenths = RandomStream.RandRange(4, 6);
		const int32 PreferredFrontBackToLeftRightTenths =
			RandomStream.RandRange(4, 6);
		const int32 PreferredTopBottomToLeftRightTenths =
			RandomStream.RandRange(4, 6);

		bool RequiredCoverageFaces[PlacementFaceCount] = {};
		int32 CompletePairCount = 0;
		for (int32 PairIndex = 0; PairIndex < PlacementPairCount; ++PairIndex)
		{
			const int32 FirstFaceIndex = PairFaceIndices[PairIndex][0];
			const int32 SecondFaceIndex = PairFaceIndices[PairIndex][1];
			if (Capacities[FirstFaceIndex] > 0 && Capacities[SecondFaceIndex] > 0)
			{
				++CompletePairCount;
			}
		}

		if (CompletePairCount >= 2 && SafeRequestedCount >= CompletePairCount * 2)
		{
			for (int32 PairIndex = 0; PairIndex < PlacementPairCount; ++PairIndex)
			{
				const int32 FirstFaceIndex = PairFaceIndices[PairIndex][0];
				const int32 SecondFaceIndex = PairFaceIndices[PairIndex][1];
				if (Capacities[FirstFaceIndex] > 0
					&& Capacities[SecondFaceIndex] > 0)
				{
					RequiredCoverageFaces[FirstFaceIndex] = true;
					RequiredCoverageFaces[SecondFaceIndex] = true;
				}
			}
		}

		const int32 PreferredInternalTenths[PlacementPairCount] = {
			PreferredFrontTenths,
			PreferredTopTenths,
			PreferredLeftTenths };
		int32 PreferredPairTenths[PlacementPairCount][PlacementPairCount] = {};
		PreferredPairTenths[0][1] = PreferredFrontBackTenths;
		PreferredPairTenths[0][2] = PreferredFrontBackToLeftRightTenths;
		PreferredPairTenths[1][2] = PreferredTopBottomToLeftRightTenths;

		bool bFoundSixFaceChoice = false;
		ChosenCounts = ChooseSixFaceCounts(
			Capacities,
			ExistingCounts,
			RequiredCoverageFaces,
			SafeRequestedCount,
			PreferredInternalTenths,
			PreferredPairTenths,
			RandomStream,
			bFoundSixFaceChoice);
		if (!bFoundSixFaceChoice)
		{
			return Plan;
		}
	}

	Plan.FrontCount = ChosenCounts.Counts[FrontFaceIndex];
	Plan.BackCount = ChosenCounts.Counts[BackFaceIndex];
	Plan.TopCount = ChosenCounts.Counts[TopFaceIndex];
	Plan.BottomCount = ChosenCounts.Counts[BottomFaceIndex];
	Plan.LeftCount = ChosenCounts.Counts[LeftFaceIndex];
	Plan.RightCount = ChosenCounts.Counts[RightFaceIndex];

	// 면별 최소 간격은 얇은 메시의 반대편까지 막지 않도록 독립적으로 유지한다.
	// 다만 가구 모서리에서 만나는 인접 면은 이미 고른 위치를 거리 점수에 포함해
	// 가능한 한 서로 먼 후보를 고른다. 인접 면 때문에 수량을 잃으면 면별 원안을 쓴다.
	TArray<int32> FinalSelectedIndices[PlacementFaceCount];
	TArray<FVector> FinalSelectedLocations[PlacementFaceCount];
	TArray<int32> FaceSelectionOrder = {
		FrontFaceIndex,
		BackFaceIndex,
		TopFaceIndex,
		BottomFaceIndex };
	if (bLeftRightPairAvailable)
	{
		FaceSelectionOrder.Add(LeftFaceIndex);
		FaceSelectionOrder.Add(RightFaceIndex);
	}
	ShuffleIndices(FaceSelectionOrder, RandomStream);

	for (const int32 FaceIndex : FaceSelectionOrder)
	{
		const int32 FaceCount = ChosenCounts.Counts[FaceIndex];
		if (FaceCount <= 0)
		{
			continue;
		}

		TArray<FVector> AdjacentSpreadLocations;
		for (int32 OtherFaceIndex = 0;
			OtherFaceIndex < PlacementFaceCount;
			++OtherFaceIndex)
		{
			if (OtherFaceIndex == FaceIndex
				|| AreOppositeFaces(FaceIndex, OtherFaceIndex))
			{
				continue;
			}

			AdjacentSpreadLocations.Append(ExistingLocations[OtherFaceIndex]);
			AdjacentSpreadLocations.Append(FinalSelectedLocations[OtherFaceIndex]);
		}

		FinalSelectedIndices[FaceIndex] = SelectSpreadCandidates(
			Candidates,
			FaceCandidateIndices[FaceIndex],
			ExistingLocations[FaceIndex],
			AdjacentSpreadLocations,
			FaceCount,
			MinimumSeparationCm,
			RandomStream);
		if (FinalSelectedIndices[FaceIndex].Num() != FaceCount)
		{
			FinalSelectedIndices[FaceIndex] = FarthestPrefixes[FaceIndex];
			FinalSelectedIndices[FaceIndex].SetNum(FaceCount);
		}

		FinalSelectedLocations[FaceIndex].Reserve(FaceCount);
		for (const int32 CandidateIndex : FinalSelectedIndices[FaceIndex])
		{
			FinalSelectedLocations[FaceIndex].Add(Candidates[CandidateIndex].Location);
		}
	}

	Plan.OrderedCandidateIndices.Reserve(ChosenCounts.NewTotal);
	for (int32 FaceIndex = 0; FaceIndex < PlacementFaceCount; ++FaceIndex)
	{
		for (const int32 CandidateIndex : FinalSelectedIndices[FaceIndex])
		{
			Plan.OrderedCandidateIndices.Add(Candidates[CandidateIndex].SourceIndex);
		}
	}
	ShuffleIndices(Plan.OrderedCandidateIndices, RandomStream);

	return Plan;
}
