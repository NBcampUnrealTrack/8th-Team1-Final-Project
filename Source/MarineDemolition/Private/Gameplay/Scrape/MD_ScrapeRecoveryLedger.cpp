// Copyright Epic Games, Inc. All Rights Reserved.

#include "Types/Scrape/MD_ScrapeTypes.h"

namespace
{
	bool AreSameRecoveryLot(
		const FMD_ScrapeRecoveryLot& A,
		const FMD_ScrapeRecoveryLot& B)
	{
		return A.SourceId == B.SourceId
			&& A.RewardClass == B.RewardClass;
	}

	// These destinations are owned temporaries. Keep the public append fallback
	// if a floating-point merge invalidates a lot, preserving its failure rules.
	bool AppendRecoveryLotToValidatedDestination(
		TArray<FMD_ScrapeRecoveryLot>& InOutLots,
		const FMD_ScrapeRecoveryLot& Lot,
		bool& bDestinationIsValid)
	{
		if (!bDestinationIsValid)
		{
			return FMD_ScrapeRecoveryLedger::AppendLot(InOutLots, Lot);
		}
		if (!FMD_ScrapeRecoveryLedger::IsLotValid(Lot))
		{
			return false;
		}
		if (Lot.CarrierAmount == 0.0 && Lot.RewardUnits == 0.0)
		{
			return true;
		}
		for (FMD_ScrapeRecoveryLot& ExistingLot : InOutLots)
		{
			if (!AreSameRecoveryLot(ExistingLot, Lot))
			{
				continue;
			}
			const double NewCarrierAmount = ExistingLot.CarrierAmount + Lot.CarrierAmount;
			const double NewRewardUnits = ExistingLot.RewardUnits + Lot.RewardUnits;
			if (!FMath::IsFinite(NewCarrierAmount) || !FMath::IsFinite(NewRewardUnits))
			{
				return false;
			}
			ExistingLot.CarrierAmount = NewCarrierAmount;
			ExistingLot.RewardUnits = NewRewardUnits;
			bDestinationIsValid = FMD_ScrapeRecoveryLedger::IsLotValid(ExistingLot);
			return true;
		}
		InOutLots.Add(Lot);
		return true;
	}
}

bool FMD_ScrapeRecoveryLedger::IsLotValid(
	const FMD_ScrapeRecoveryLot& Lot)
{
	if (!FMath::IsFinite(Lot.CarrierAmount)
		|| Lot.CarrierAmount < 0.0
		|| !FMath::IsFinite(Lot.RewardUnits)
		|| Lot.RewardUnits < 0.0)
	{
		return false;
	}

	if (Lot.RewardUnits > 0.0
		&& (!Lot.SourceId.IsValid() || Lot.RewardClass.IsNone()))
	{
		return false;
	}

	return Lot.CarrierAmount > 0.0
		|| Lot.RewardUnits == 0.0;
}

double FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
	const TConstArrayView<FMD_ScrapeRecoveryLot> Lots)
{
	double Total = 0.0;
	for (const FMD_ScrapeRecoveryLot& Lot : Lots)
	{
		if (!IsLotValid(Lot))
		{
			return -1.0;
		}
		Total += Lot.CarrierAmount;
		if (!FMath::IsFinite(Total))
		{
			return -1.0;
		}
	}
	return Total;
}

double FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
	const TConstArrayView<FMD_ScrapeRecoveryLot> Lots)
{
	double Total = 0.0;
	for (const FMD_ScrapeRecoveryLot& Lot : Lots)
	{
		if (!IsLotValid(Lot))
		{
			return -1.0;
		}
		Total += Lot.RewardUnits;
		if (!FMath::IsFinite(Total))
		{
			return -1.0;
		}
	}
	return Total;
}

bool FMD_ScrapeRecoveryLedger::AppendLot(
	TArray<FMD_ScrapeRecoveryLot>& InOutLots,
	const FMD_ScrapeRecoveryLot& Lot)
{
	if (!IsLotValid(Lot))
	{
		return false;
	}
	if (Lot.CarrierAmount == 0.0
		&& Lot.RewardUnits == 0.0)
	{
		return true;
	}

	for (const FMD_ScrapeRecoveryLot& ExistingLot : InOutLots)
	{
		if (!IsLotValid(ExistingLot))
		{
			return false;
		}
	}

	for (FMD_ScrapeRecoveryLot& ExistingLot : InOutLots)
	{
		if (!AreSameRecoveryLot(ExistingLot, Lot))
		{
			continue;
		}

		const double NewCarrierAmount =
			ExistingLot.CarrierAmount + Lot.CarrierAmount;
		const double NewRewardUnits =
			ExistingLot.RewardUnits + Lot.RewardUnits;
		if (!FMath::IsFinite(NewCarrierAmount)
			|| !FMath::IsFinite(NewRewardUnits))
		{
			return false;
		}
		ExistingLot.CarrierAmount = NewCarrierAmount;
		ExistingLot.RewardUnits = NewRewardUnits;
		return true;
	}

	InOutLots.Add(Lot);
	return true;
}

bool FMD_ScrapeRecoveryLedger::AppendScaledLots(
	TArray<FMD_ScrapeRecoveryLot>& InOutLots,
	const TConstArrayView<FMD_ScrapeRecoveryLot> SourceLots,
	const double Scale)
{
	if (!FMath::IsFinite(Scale) || Scale < 0.0)
	{
		return false;
	}
	if (GetTotalCarrierAmount(InOutLots) < 0.0)
	{
		return false;
	}
	TArray<FMD_ScrapeRecoveryLot> WorkingLots = InOutLots;
	for (const FMD_ScrapeRecoveryLot& SourceLot : SourceLots)
	{
		if (!IsLotValid(SourceLot))
		{
			return false;
		}
		if (Scale == 0.0)
		{
			continue;
		}
		FMD_ScrapeRecoveryLot ScaledLot = SourceLot;
		ScaledLot.CarrierAmount *= Scale;
		ScaledLot.RewardUnits *= Scale;
		if (!AppendLot(WorkingLots, ScaledLot))
		{
			return false;
		}
	}

	InOutLots = MoveTemp(WorkingLots);
	return true;
}

bool FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
	TArray<FMD_ScrapeRecoveryLot>& InOutLots,
	const double NewTotalCarrierAmount)
{
	if (!FMath::IsFinite(NewTotalCarrierAmount)
		|| NewTotalCarrierAmount < 0.0)
	{
		return false;
	}

	const double PreviousTotalCarrierAmount =
		GetTotalCarrierAmount(InOutLots);
	if (PreviousTotalCarrierAmount < 0.0)
	{
		return false;
	}

	if (NewTotalCarrierAmount == 0.0)
	{
		for (const FMD_ScrapeRecoveryLot& Lot : InOutLots)
		{
			if (Lot.RewardUnits > 0.0)
			{
				return false;
			}
		}
		InOutLots.Reset();
		return true;
	}

	if (PreviousTotalCarrierAmount == 0.0)
	{
		return AppendZeroRewardCarrier(
			InOutLots,
			NewTotalCarrierAmount);
	}

	TArray<FMD_ScrapeRecoveryLot> WorkingLots = InOutLots;
	const double Scale = NewTotalCarrierAmount / PreviousTotalCarrierAmount;
	double AssignedCarrierAmount = 0.0;
	int32 LastNonEmptyIndex = INDEX_NONE;
	for (int32 LotIndex = 0; LotIndex < WorkingLots.Num(); ++LotIndex)
	{
		FMD_ScrapeRecoveryLot& Lot = WorkingLots[LotIndex];
		Lot.CarrierAmount *= Scale;
		if (!FMath::IsFinite(Lot.CarrierAmount)
			|| Lot.CarrierAmount < 0.0)
		{
			return false;
		}
		if (Lot.CarrierAmount == 0.0 && Lot.RewardUnits > 0.0)
		{
			// 보상 provenance를 가진 lot의 carrier가 underflow로 0이 되면
			// source identity를 잃지 않는 재분배 방법이 없다. 손실 대신
			// 전체 rebase를 원자적으로 거부한다.
			return false;
		}
		if (Lot.CarrierAmount > 0.0)
		{
			LastNonEmptyIndex = LotIndex;
		}
		AssignedCarrierAmount += Lot.CarrierAmount;
	}
	if (LastNonEmptyIndex == INDEX_NONE)
	{
		return false;
	}

	WorkingLots[LastNonEmptyIndex].CarrierAmount +=
		NewTotalCarrierAmount - AssignedCarrierAmount;
	if (!IsLotValid(WorkingLots[LastNonEmptyIndex]))
	{
		return false;
	}
	InOutLots = MoveTemp(WorkingLots);
	return true;
}

double FMD_ScrapeRecoveryLedger::ExtractProportional(
	TArray<FMD_ScrapeRecoveryLot>& InOutLots,
	const double RequestedCarrierAmount,
	TArray<FMD_ScrapeRecoveryLot>& OutExtractedLots)
{
	OutExtractedLots.Reset();
	if (!FMath::IsFinite(RequestedCarrierAmount)
		|| RequestedCarrierAmount <= 0.0)
	{
		return 0.0;
	}

	const double TotalCarrierAmount = GetTotalCarrierAmount(InOutLots);
	if (TotalCarrierAmount <= 0.0)
	{
		return 0.0;
	}

	const double ActualCarrierAmount = FMath::Min(
		RequestedCarrierAmount,
		TotalCarrierAmount);
	if (ActualCarrierAmount >= TotalCarrierAmount)
	{
		OutExtractedLots = MoveTemp(InOutLots);
		InOutLots.Reset();
		return TotalCarrierAmount;
	}

	const double ExtractRatio =
		ActualCarrierAmount / TotalCarrierAmount;
	TArray<FMD_ScrapeRecoveryLot> RemainingLots;
	TArray<FMD_ScrapeRecoveryLot> ExtractedLots;
	RemainingLots.Reserve(InOutLots.Num());
	ExtractedLots.Reserve(InOutLots.Num());
	bool bRemainingLotsValid = true;
	bool bExtractedLotsValid = true;
	for (const FMD_ScrapeRecoveryLot& SourceLot : InOutLots)
	{
		if (!IsLotValid(SourceLot))
		{
			return 0.0;
		}

		FMD_ScrapeRecoveryLot ExtractedLot = SourceLot;
		ExtractedLot.CarrierAmount *= ExtractRatio;
		ExtractedLot.RewardUnits *= ExtractRatio;
		FMD_ScrapeRecoveryLot RemainingLot = SourceLot;
		RemainingLot.CarrierAmount -= ExtractedLot.CarrierAmount;
		RemainingLot.RewardUnits -= ExtractedLot.RewardUnits;
		if (!AppendRecoveryLotToValidatedDestination(
			ExtractedLots, ExtractedLot, bExtractedLotsValid)
			|| !AppendRecoveryLotToValidatedDestination(
				RemainingLots, RemainingLot, bRemainingLotsValid))
		{
			return 0.0;
		}
	}

	if (!RebaseCarrierAmount(ExtractedLots, ActualCarrierAmount)
		|| !RebaseCarrierAmount(
			RemainingLots,
			TotalCarrierAmount - ActualCarrierAmount))
	{
		return 0.0;
	}

	InOutLots = MoveTemp(RemainingLots);
	OutExtractedLots = MoveTemp(ExtractedLots);
	return ActualCarrierAmount;
}

bool FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
	TArray<FMD_ScrapeRecoveryLot>& InOutLots,
	const double CarrierAmount)
{
	if (!FMath::IsFinite(CarrierAmount)
		|| CarrierAmount <= 0.0)
	{
		return false;
	}

	FMD_ScrapeRecoveryLot ZeroRewardLot;
	ZeroRewardLot.CarrierAmount = CarrierAmount;
	return AppendLot(InOutLots, ZeroRewardLot);
}
