#include "Component/Scrape/MD_ScrapeSurfaceComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"
#include "Math/RotationMatrix.h"
#include "World/Contamination/MD_ContaminationItem.h"

// 파일 분할 기준은 MD_ScrapeSurfaceComponentInternal.h 표 참고.
// 책임: 밤샘 악화 · Day 스냅샷 입출력 · 오염물 드롭

namespace
{
	constexpr double PersistenceRecoveryCarrierComparisonTolerance = 1.0e-9;

	bool AreRecoveryCarrierAmountsEqual(
		const double A,
		const double B)
	{
		return FMath::Abs(A - B)
			<= PersistenceRecoveryCarrierComparisonTolerance
				* FMath::Max(1.0, FMath::Max(FMath::Abs(A), FMath::Abs(B)));
	}

	bool TryAppendPersistenceRecoveryLots(
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

	bool TryReconcilePendingRecoveryLots(
		TArray<FMD_ScrapeRecoveryLot>& InOutPendingLots,
		const double PendingRawAmount)
	{
		if (!FMath::IsFinite(PendingRawAmount) || PendingRawAmount < 0.0)
		{
			return false;
		}
		const double PendingLotCarrierAmount =
			FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(InOutPendingLots);
		if (PendingLotCarrierAmount < 0.0)
		{
			return false;
		}
		if (AreRecoveryCarrierAmountsEqual(
			PendingLotCarrierAmount,
			PendingRawAmount))
		{
			return FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
				InOutPendingLots,
				PendingRawAmount);
		}
		if (PendingLotCarrierAmount < PendingRawAmount)
		{
			// MASS-011 이전 pending raw에는 provenance가 없으므로 reward 0이다.
			return FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				InOutPendingLots,
				PendingRawAmount - PendingLotCarrierAmount);
		}

		// lot이 raw보다 많으면 어느 carrier가 사라졌는지 알 수 없다. 임의로
		// 보상을 옮기거나 지우지 않고 transaction 자체를 거부한다.
		return false;
	}
}

void UMD_ScrapeSurfaceComponent::ReapplyContaminationPatch(float Amount, int32 PatchRadiusCells)
{
	if (!bContaminationEnabled || !HasAuthority())
	{
		return;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	const int32 GridWidth = ContaminationGridDimensions.X;
	const int32 GridHeight = ContaminationGridDimensions.Y;
	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (ContaminationCells.Num() == 0
		|| GridWidth <= 0
		|| GridHeight <= 0
		|| !FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(
			ContaminationGridDimensions,
			CellsPerFace,
			TotalCellCount)
		|| ContaminationCells.Num() != TotalCellCount)
	{
		return;
	}

	const float SafeAmount = FMath::IsFinite(Amount)
		? FMath::Clamp(Amount, 0.0f, 1.0f)
		: 0.0f;
	const int32 SafePatchRadiusCells = FMath::Max(0, PatchRadiusCells);
	if (SafeAmount <= UE_SMALL_NUMBER)
	{
		return;
	}

	static constexpr EMD_ScrapeSurfaceFace PatchTargetFaces[] = {
		EMD_ScrapeSurfaceFace::Front,
		EMD_ScrapeSurfaceFace::Back };
	double AddedBaseCarrierAmounts[2] = { 0.0, 0.0 };

	TArray<FMD_ScrapeCellChange> Changes;
	for (const EMD_ScrapeSurfaceFace TargetFace : PatchTargetFaces)
	{
		const FIntPoint Center(
			FMath::RandRange(0, GridWidth - 1),
			FMath::RandRange(0, GridHeight - 1));
		const int32 FaceSlot = TargetFace == EMD_ScrapeSurfaceFace::Back
			? 1
			: 0;

		for (int32 Y = Center.Y - SafePatchRadiusCells;
			Y <= Center.Y + SafePatchRadiusCells;
			++Y)
		{
			for (int32 X = Center.X - SafePatchRadiusCells;
				X <= Center.X + SafePatchRadiusCells;
				++X)
			{
				if (X < 0 || X >= GridWidth || Y < 0 || Y >= GridHeight)
				{
					continue;
				}


				const int32 CombinedCellIndex =
					FMD_ScrapeSurfaceLayout::ToCombinedCellIndex(
						TargetFace,
						X + Y * GridWidth,
						CellsPerFace);
				if (!ContaminationCells.IsValidIndex(CombinedCellIndex))
				{
					continue;
				}

				FMD_ScrapeSurfaceCell& Cell =
					ContaminationCells[CombinedCellIndex];
				const float PreviousRemaining = Cell.RemainingContamination;
				const float TargetRemaining = FMath::Clamp(
					PreviousRemaining + SafeAmount,
					0.0f,
					1.0f);
				if (FMath::IsNearlyEqual(
					PreviousRemaining,
					TargetRemaining,
					UE_SMALL_NUMBER))
				{
					continue;
				}

				Cell.RemainingContamination = TargetRemaining;
				Cell.InitialContamination = FMath::Max(
					Cell.InitialContamination,
					TargetRemaining);
				// 새 오염이 덮이면 어제 긁어낸 면적은 다시 오염된 상태다.
				// 남겨 두면 오늘 긁는 자국의 경계 신호가 어제 값에 포화되어
				// 자국 경계가 격자 계단으로 돌아간다.
				Cell.CoveredFraction = 0.0f;
				AddedBaseCarrierAmounts[FaceSlot] += static_cast<double>(
					TargetRemaining - PreviousRemaining);

				FMD_ScrapeCellChange Change;
				Change.CellIndex = CombinedCellIndex;
				Change.GridCoordinate = FIntPoint(X, Y);
				Change.RemovedAmount = 0.0f;
				Change.RemainingAmountAfter = Cell.RemainingContamination;
				Change.bBecameClean = false;
				Changes.Add(Change);
			}
		}
	}

	if (Changes.IsEmpty())
	{
		return;
	}
	for (int32 FaceSlot = 0; FaceSlot < 2; ++FaceSlot)
	{
		if (AddedBaseCarrierAmounts[FaceSlot] <= 0.0)
		{
			continue;
		}
		// 밤샘 악화는 새 canonical unit 발행이 아니라 관리용 reward 0 raw다.
		const bool bRecorded =
			FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				BaseContaminationRecoveryLots[FaceSlot],
				AddedBaseCarrierAmounts[FaceSlot]);
		ensureMsgf(
			bRecorded,
			TEXT("Reapplied contamination could not be recorded in recovery ledger."));
	}

	RefreshContaminationCleanRatioCache();
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
	UpdateContaminationMask(Changes);
	PublishContaminationChanges(Changes);
}


void UMD_ScrapeSurfaceComponent::ExportContaminationDeltas(
	TArray<int32>& OutCellIndices,
	TArray<float>& OutInitialAmounts,
	TArray<float>& OutRemainingAmounts) const
{
	TArray<float> IgnoredTransferredAmounts;
	TArray<float> IgnoredTransferredPatternDensities;
	ExportContaminationDeltas(
		OutCellIndices,
		OutInitialAmounts,
		OutRemainingAmounts,
		IgnoredTransferredAmounts,
		IgnoredTransferredPatternDensities);
}


void UMD_ScrapeSurfaceComponent::ExportContaminationDeltas(
	TArray<int32>& OutCellIndices,
	TArray<float>& OutInitialAmounts,
	TArray<float>& OutRemainingAmounts,
	TArray<float>& OutTransferredAmounts,
	TArray<float>& OutTransferredPatternDensities) const
{
	TArray<float> IgnoredCoveredFractions;
	ExportContaminationDeltas(
		OutCellIndices,
		OutInitialAmounts,
		OutRemainingAmounts,
		OutTransferredAmounts,
		OutTransferredPatternDensities,
		IgnoredCoveredFractions);
}


void UMD_ScrapeSurfaceComponent::ExportContaminationDeltas(
	TArray<int32>& OutCellIndices,
	TArray<float>& OutInitialAmounts,
	TArray<float>& OutRemainingAmounts,
	TArray<float>& OutTransferredAmounts,
	TArray<float>& OutTransferredPatternDensities,
	TArray<float>& OutCoveredFractions) const
{
	OutCellIndices.Reset();
	OutInitialAmounts.Reset();
	OutRemainingAmounts.Reset();
	OutTransferredAmounts.Reset();
	OutTransferredPatternDensities.Reset();
	OutCoveredFractions.Reset();
	if (!bContaminationEnabled)
	{
		return;
	}

	for (int32 Index = 0; Index < ContaminationCells.Num(); ++Index)
	{
		const FMD_ScrapeSurfaceCell& Cell = ContaminationCells[Index];
		const bool bScraped = Cell.RemainingContamination != Cell.InitialContamination;

		const bool bBurstSinceGeneration =
			ContaminationGeneratedInitialAmounts.IsValidIndex(Index)
			&& Cell.InitialContamination
				!= ContaminationGeneratedInitialAmounts[Index];

		const bool bHasTransferredPaint =
			Cell.TransferredContamination > UE_SMALL_NUMBER;
		if (bScraped || bBurstSinceGeneration || bHasTransferredPaint)
		{
			OutCellIndices.Add(Index);
			OutInitialAmounts.Add(Cell.InitialContamination);
			OutRemainingAmounts.Add(Cell.RemainingContamination);
			OutTransferredAmounts.Add(Cell.TransferredContamination);
			OutTransferredPatternDensities.Add(
				Cell.TransferredPaintPatternDensity);
			OutCoveredFractions.Add(Cell.CoveredFraction);
		}
	}
}


void UMD_ScrapeSurfaceComponent::RestoreContaminationCells(
	const TArray<int32>& CellIndices,
	const TArray<float>& InitialAmounts,
	const TArray<float>& RemainingAmounts)
{
	RestoreContaminationCells(
		CellIndices,
		InitialAmounts,
		RemainingAmounts,
		TArray<float>(),
		TArray<float>());
}


void UMD_ScrapeSurfaceComponent::RestoreContaminationCells(
	const TArray<int32>& CellIndices,
	const TArray<float>& InitialAmounts,
	const TArray<float>& RemainingAmounts,
	const TArray<float>& TransferredAmounts,
	const TArray<float>& TransferredPatternDensities)
{
	RestoreContaminationCells(
		CellIndices,
		InitialAmounts,
		RemainingAmounts,
		TransferredAmounts,
		TransferredPatternDensities,
		TArray<float>());
}


void UMD_ScrapeSurfaceComponent::RestoreContaminationCells(
	const TArray<int32>& CellIndices,
	const TArray<float>& InitialAmounts,
	const TArray<float>& RemainingAmounts,
	const TArray<float>& TransferredAmounts,
	const TArray<float>& TransferredPatternDensities,
	const TArray<float>& CoveredFractions)
{
	const bool bHasInitialAmounts = !InitialAmounts.IsEmpty();
	const bool bHasTransferredAmounts = !TransferredAmounts.IsEmpty();
	const bool bHasTransferredPatternDensities =
		!TransferredPatternDensities.IsEmpty();
	const bool bHasCoveredFractions = !CoveredFractions.IsEmpty();
	if (!bContaminationEnabled
		|| !HasAuthority()
		|| CellIndices.Num() != RemainingAmounts.Num()
		|| (bHasInitialAmounts && CellIndices.Num() != InitialAmounts.Num())
		|| (bHasTransferredAmounts
			&& CellIndices.Num() != TransferredAmounts.Num())
		|| (bHasTransferredPatternDensities
			&& CellIndices.Num() != TransferredPatternDensities.Num())
		|| (bHasCoveredFractions
			&& CellIndices.Num() != CoveredFractions.Num()))
	{
		return;
	}

	if (!bContaminationSurfaceInitialized)
	{
		InitializeContaminationSurface();
	}

	const int32 GridWidth = ContaminationGridDimensions.X;
	if (ContaminationCells.Num() == 0 || GridWidth <= 0)
	{
		return;
	}

	TArray<FMD_ScrapeCellChange> Changes;
	for (int32 i = 0; i < CellIndices.Num(); ++i)
	{
		const int32 Index = CellIndices[i];
		// 그리드 크기가 바뀐 구세션 스냅샷의 셀은 무시
		if (!ContaminationCells.IsValidIndex(Index))
		{
			continue;
		}

		FMD_ScrapeSurfaceCell& Cell = ContaminationCells[Index];

		if (bHasInitialAmounts && FMath::IsFinite(InitialAmounts[i]))
		{
			Cell.InitialContamination = FMath::Clamp(
				InitialAmounts[i], 0.0f, 1.0f);
		}

		Cell.RemainingContamination = FMath::IsFinite(RemainingAmounts[i])
			? FMath::Clamp(
				RemainingAmounts[i],
				0.0f,
				Cell.InitialContamination)
			: 0.0f;
		Cell.TransferredContamination =
			bHasTransferredAmounts && FMath::IsFinite(TransferredAmounts[i])
			? FMath::Clamp(
				TransferredAmounts[i],
				0.0f,
				1.0f)
			: 0.0f;
		Cell.TransferredPaintPatternDensity =
			Cell.TransferredContamination <= ScrapeSettings.CleanTolerance
			? 0.0f
			: (bHasTransferredPatternDensities
				&& FMath::IsFinite(TransferredPatternDensities[i])
				? FMath::Clamp(
					TransferredPatternDensities[i],
					0.0f,
					1.0f)
				: 1.0f);
		// 구버전 스냅샷에는 피복률이 없다. 잔여량에서 역산하면 얇은 막과 부분
		// 피복이 다시 한 값으로 뭉개져 이 변경 전의 겉모습이 되는데, 어제 저장한
		// 상태를 그때 보였던 모습대로 되살리는 것이므로 이 쪽이 맞다.
		Cell.CoveredFraction =
			bHasCoveredFractions && FMath::IsFinite(CoveredFractions[i])
			? FMath::Clamp(CoveredFractions[i], 0.0f, 1.0f)
			: (Cell.InitialContamination > UE_SMALL_NUMBER
				? FMath::Clamp(
					1.0f
						- Cell.RemainingContamination
							/ Cell.InitialContamination,
					0.0f,
					1.0f)
				: 0.0f);
		Cell.Normalize();

		FMD_ScrapeCellChange Change;
		Change.CellIndex = Index;
		Change.GridCoordinate = FIntPoint(Index % GridWidth, Index / GridWidth);
		Change.RemainingAmountAfter = Cell.RemainingContamination;
		Changes.Add(Change);
	}

	if (Changes.IsEmpty())
	{
		return;
	}

	// MASS-014 형식 전의 save에는 이미 회수된 canonical unit 이력이 없다.
	// 남은 셀을 다시 canonical로 만들면 저장/로드만으로 보상을 재발행하므로,
	// legacy restore가 만든 base/transferred raw는 모두 reward 0으로 보수 복원한다.
	for (int32 FaceSlot = 0; FaceSlot < 2; ++FaceSlot)
	{
		BaseContaminationRecoveryLots[FaceSlot].Reset();
		TransferredContaminationRecoveryLots[FaceSlot].Reset();
		const EMD_ScrapeSurfaceFace Face = FaceSlot == 0
			? EMD_ScrapeSurfaceFace::Front
			: EMD_ScrapeSurfaceFace::Back;
		const TConstArrayView<FMD_ScrapeSurfaceCell> FaceCells =
			GetContaminationCellsForFace(Face);
		double BaseCarrierAmount = 0.0;
		double TransferredCarrierAmount = 0.0;
		for (const FMD_ScrapeSurfaceCell& Cell : FaceCells)
		{
			BaseCarrierAmount +=
				static_cast<double>(Cell.RemainingContamination);
			TransferredCarrierAmount +=
				static_cast<double>(Cell.TransferredContamination);
		}
		const bool bRecordedBase = BaseCarrierAmount <= 0.0
			|| FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				BaseContaminationRecoveryLots[FaceSlot],
				BaseCarrierAmount);
		const bool bRecordedTransferred = TransferredCarrierAmount <= 0.0
			|| FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				TransferredContaminationRecoveryLots[FaceSlot],
				TransferredCarrierAmount);
		ensureMsgf(
			bRecordedBase && bRecordedTransferred,
			TEXT("Legacy contamination restore could not rebuild recovery ledger."));
	}

	// 앞/뒤 제거 비율을 함께 갱신한다.
	RefreshContaminationCleanRatioCache();
	// 어제 완전 제거된 벽이면 완료 이벤트가 재방송되지 않게 이미 방송된 것으로 마킹
	bContaminationCompletionBroadcast = IsContaminationFullyRemoved();
	// 복원한 셀 상태가 화면에 보이도록 런타임 G8 마스크도 함께 갱신한다.
	UpdateContaminationMask(Changes);
	PublishContaminationChanges(Changes);
}


bool UMD_ScrapeSurfaceComponent::TryCalculateContaminationItemSpawnTransform(
	const EMD_ScrapeSurfaceFace SurfaceFace,
	const FVector2D& SurfacePositionCm,
	FTransform& OutSpawnTransform) const
{
	OutSpawnTransform = FTransform::Identity;
	if (!bContaminationEnabled || !SurfaceMesh)
	{
		return false;
	}

	const FTransform SurfaceTransform = SurfaceMesh->GetComponentTransform();

	FVector WorldPosition = FVector::ZeroVector;
	FVector HorizontalAxis = FVector::ZeroVector;
	FVector FrontNormalLocal = FVector::ZeroVector;
	FVector VerticalAxis = FVector::ZeroVector;
	if (!FMD_ScrapeSurfaceGeometry::TryCalculateSurfaceWorldPosition(
			SurfacePositionCm,
			SurfaceFace,
			SurfaceTransform,
			ResolvedContaminationSurfaceNormalAxis,
			ContaminationSurfaceCenterLocal,
			ContaminationMeshLocalBoundsMinimum,
			ContaminationMeshLocalBoundsMaximum,
			WorldPosition)
		|| !FMD_ScrapeSurfaceGeometry::TryGetSurfaceLocalAxes(
			ResolvedContaminationSurfaceNormalAxis,
			HorizontalAxis,
			FrontNormalLocal,
			VerticalAxis))
	{
		return false;
	}

	const FVector OutwardNormal =
		SurfaceTransform.TransformVectorNoScale(
			SurfaceFace == EMD_ScrapeSurfaceFace::Back
				? -FrontNormalLocal
				: FrontNormalLocal).GetSafeNormal();
	if (OutwardNormal.IsNearlyZero())
	{
		return false;
	}

	const float SafeOffsetCm = FMath::IsFinite(ContaminationItemSpawnOffsetCm)
		? FMath::Max(0.0f, ContaminationItemSpawnOffsetCm)	: 0.0f;

	OutSpawnTransform = FTransform(
		FRotationMatrix::MakeFromZ(OutwardNormal).Rotator(),
		WorldPosition + OutwardNormal * SafeOffsetCm);
	return true;
}


bool UMD_ScrapeSurfaceComponent::CanAcceptRecoveryTransaction(
	const EMD_ScrapeSurfaceFace SurfaceFace) const
{
	if (!bContaminationEnabled
		|| !HasAuthority()
		|| (SurfaceFace != EMD_ScrapeSurfaceFace::Front
			&& SurfaceFace != EMD_ScrapeSurfaceFace::Back))
	{
		return false;
	}

	const int32 FaceSlot = SurfaceFace == EMD_ScrapeSurfaceFace::Back
		? 1
		: 0;
	const double BaseCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			BaseContaminationRecoveryLots[FaceSlot]);
	const double TransferredCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			TransferredContaminationRecoveryLots[FaceSlot]);
	const double PendingLotCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			PendingPhysicalRecoveryLots[FaceSlot]);
	const double SpawnedLotCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(
			SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot]);
	const double BaseRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			BaseContaminationRecoveryLots[FaceSlot]);
	const double TransferredRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			TransferredContaminationRecoveryLots[FaceSlot]);
	const double PendingRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			PendingPhysicalRecoveryLots[FaceSlot]);
	const double SpawnedRewardUnits =
		FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
			SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot]);
	const double PendingRawAmount =
		PendingContaminationItemAmounts[FaceSlot];
	if (BaseCarrierAmount < 0.0
		|| TransferredCarrierAmount < 0.0
		|| PendingLotCarrierAmount < 0.0
		|| SpawnedLotCarrierAmount < 0.0
		|| BaseRewardUnits < 0.0
		|| TransferredRewardUnits < 0.0
		|| PendingRewardUnits < 0.0
		|| SpawnedRewardUnits < 0.0
		|| !FMath::IsFinite(PendingRawAmount)
		|| PendingRawAmount < 0.0
		|| (PendingLotCarrierAmount > PendingRawAmount
			&& !AreRecoveryCarrierAmountsEqual(
				PendingLotCarrierAmount,
				PendingRawAmount)))
	{
		return false;
	}

	// cache는 마지막 Surface transaction 뒤 base+transferred의 실제 합으로
	// 갱신된다. 매 0.5cm segment마다 face 전체를 다시 훑지 않고도 이번에
	// physical로 갈 수 있는 보수적 최대량을 얻는다.
	const double MaximumRemovedCarrierAmount = bContaminationSurfaceInitialized
		? (SurfaceFace == EMD_ScrapeSurfaceFace::Back
			? CachedBackCurrentContaminationAmount
			: CachedFrontCurrentContaminationAmount)
		: 0.0;
	if (!FMath::IsFinite(MaximumRemovedCarrierAmount)
		|| MaximumRemovedCarrierAmount < 0.0)
	{
		return false;
	}
	const double SurfaceLotCarrierAmount =
		BaseCarrierAmount + TransferredCarrierAmount;
	if (!FMath::IsFinite(SurfaceLotCarrierAmount)
		|| (SurfaceLotCarrierAmount > MaximumRemovedCarrierAmount
			&& !AreRecoveryCarrierAmountsEqual(
				SurfaceLotCarrierAmount,
				MaximumRemovedCarrierAmount)))
	{
		// 셀 raw보다 많은 lot은 이미 사라진 carrier의 ghost reward다.
		// 부족한 legacy provenance만 이후 extraction에서 reward 0으로 보충한다.
		return false;
	}

	const double MaximumPendingRawAmount =
		PendingRawAmount + MaximumRemovedCarrierAmount;
	const double MaximumSpawnedCarrierAmount =
		SpawnedLotCarrierAmount
			+ PendingRawAmount
			+ MaximumRemovedCarrierAmount;
	const double MaximumCombinedRewardUnits =
		SpawnedRewardUnits
			+ PendingRewardUnits
			+ BaseRewardUnits
			+ TransferredRewardUnits;
	return FMath::IsFinite(MaximumPendingRawAmount)
		&& FMath::IsFinite(MaximumSpawnedCarrierAmount)
		&& FMath::IsFinite(MaximumCombinedRewardUnits);
}


int32 UMD_ScrapeSurfaceComponent::DropContaminationItems(const EMD_ScrapeSurfaceFace SurfaceFace,
	const double RemovedAmount, const FVector2D& SurfacePositionCm,
	const TConstArrayView<FMD_ScrapeRecoveryLot> RecoveryLots,
	bool* const bOutAcceptedToLedger)
{
	if (bOutAcceptedToLedger)
	{
		*bOutAcceptedToLedger = false;
	}
	const int32 FaceSlot = SurfaceFace == EMD_ScrapeSurfaceFace::Back ? 1 : 0;
	if (SurfaceFace != EMD_ScrapeSurfaceFace::Front	&& SurfaceFace != EMD_ScrapeSurfaceFace::Back)
	{
		return 0;
	}

	if (!bContaminationEnabled || !HasAuthority()
		|| !FMath::IsFinite(RemovedAmount) || RemovedAmount <= 0.0f)
	{
		return 0;
	}

	TArray<FMD_ScrapeRecoveryLot> IncomingRecoveryLots;
	const double RemovedCarrierAmount = RemovedAmount;
	if (RecoveryLots.IsEmpty())
	{
		// MASS-011 이전/관리 호출은 raw는 보존하되 canonical unit을 발행하지 않는다.
		if (!FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
			IncomingRecoveryLots,
			RemovedCarrierAmount))
		{
			return 0;
		}
	}
	else if (!TryAppendPersistenceRecoveryLots(IncomingRecoveryLots, RecoveryLots))
	{
		return 0;
	}
	const double IncomingCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(IncomingRecoveryLots);
	if (IncomingCarrierAmount < 0.0
		|| !AreRecoveryCarrierAmountsEqual(
			IncomingCarrierAmount,
			RemovedCarrierAmount)
		|| !FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			IncomingRecoveryLots,
			RemovedCarrierAmount))
	{
		return 0;
	}

	// MASS-005: 설정이나 월드가 아직 물리 아이템을 만들 수 없는 상태여도
	// Tool이 넘긴 잔여 질량부터 원장에 보존한다. 0개 설정은 스폰을 잠시
	// 끄는 뜻일 뿐, 이미 표면에서 사라진 질량을 버리는 뜻이 아니다.
	double& PendingAmount = PendingContaminationItemAmounts[FaceSlot];
	const double NewPendingAmount =
		PendingAmount + RemovedAmount;
	if (!FMath::IsFinite(NewPendingAmount))
	{
		return 0;
	}
	TArray<FMD_ScrapeRecoveryLot> WorkingPendingLots =
		PendingPhysicalRecoveryLots[FaceSlot];
	TArray<FMD_ScrapeRecoveryLot> WorkingSpawnedLots =
		SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot];
	if (!TryReconcilePendingRecoveryLots(
			WorkingPendingLots,
			PendingAmount)
		|| !TryAppendPersistenceRecoveryLots(
			WorkingPendingLots,
			IncomingRecoveryLots))
	{
		return 0;
	}
	PendingAmount = NewPendingAmount;
	PendingPhysicalRecoveryLots[FaceSlot] = MoveTemp(WorkingPendingLots);
	SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot] =
		MoveTemp(WorkingSpawnedLots);
	if (bOutAcceptedToLedger)
	{
		*bOutAcceptedToLedger = true;
	}
	if (ContaminationItemsPerFace <= 0)
	{
		return 0;
	}

	int32 CellsPerFace = 0;
	int32 TotalCellCount = 0;
	if (!FMD_ScrapeSurfaceLayout::TryCalculateCellCounts(ContaminationGridDimensions,
			CellsPerFace,TotalCellCount) || CellsPerFace <= 0)
	{
		return 0;
	}

	const double AmountPerItem = static_cast<double>(CellsPerFace)
		/ static_cast<double>(ContaminationItemsPerFace);
	if (!FMath::IsFinite(AmountPerItem)
		|| AmountPerItem <= UE_KINDA_SMALL_NUMBER)
	{
		return 0;
	}


	const double ItemThreshold =
		AmountPerItem - static_cast<double>(UE_KINDA_SMALL_NUMBER);

	if (PendingAmount < ItemThreshold)
	{
		return 0;
	}
	// 클래스나 월드가 잠시 준비되지 않아도 질량 원장에는 이미 남겼다.
	// 나중 커밋이 임계값을 다시 확인해 스폰할 수 있으므로 조용히 유실하지 않는다.
	if (!ContaminationItemClass)
	{
		return 0;
	}

	FTransform SpawnTransform = FTransform::Identity;
	if (!TryCalculateContaminationItemSpawnTransform(
			SurfaceFace,
			SurfacePositionCm,
			SpawnTransform))
	{
		return 0;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return 0;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	int32 SpawnedCount = 0;
	while (PendingAmount >= ItemThreshold)
	{
		TArray<FMD_ScrapeRecoveryLot> PendingLotsAfterSpawn =
			PendingPhysicalRecoveryLots[FaceSlot];
		TArray<FMD_ScrapeRecoveryLot> SpawnedLotsAfterSpawn =
			SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot];
		TArray<FMD_ScrapeRecoveryLot> ItemRecoveryLots;
		const double PendingCarrierCommittedToItem = FMath::Min(
			PendingAmount,
			AmountPerItem);
		const double ItemCarrierAmount =
			FMD_ScrapeRecoveryLedger::ExtractProportional(
				PendingLotsAfterSpawn,
				PendingCarrierCommittedToItem,
				ItemRecoveryLots);
		if (!AreRecoveryCarrierAmountsEqual(
				ItemCarrierAmount,
				PendingCarrierCommittedToItem)
			|| !TryAppendPersistenceRecoveryLots(
				SpawnedLotsAfterSpawn,
				ItemRecoveryLots))
		{
			break;
		}
		if (World->SpawnActor<AMD_ContaminationItem>(
				ContaminationItemClass,
				SpawnTransform,
				SpawnParams))
		{
			PendingAmount = FMath::Max(
				0.0,
				PendingAmount - AmountPerItem);
			PendingPhysicalRecoveryLots[FaceSlot] =
				MoveTemp(PendingLotsAfterSpawn);
			SpawnedPhysicalRecoveryLotsAwaitingCarrier[FaceSlot] =
				MoveTemp(SpawnedLotsAfterSpawn);
			++SpawnedCount;
			continue;
		}

		// Spawn 실패는 질량을 버리는 이유가 아니다. 대기량을 보존하고
		// 다음 유효 커밋에서 다시 시도한다.
		break;
	}

	return SpawnedCount;
}

UMD_ScrapeSurfaceComponent::FPhysicalCommitResult
UMD_ScrapeSurfaceComponent::CommitContaminationPhysicalDrop(
	const FMD_ScrapeResult& ScrapeResult,
	const FMD_ScrapeMassAllocationResult& Allocation,
	const FVector2D& SurfacePositionCm)
{
	FPhysicalCommitResult CommitResult;
	if (!ScrapeResult.bIsInputValid
		|| !ScrapeResult.bWasStrokeLongEnough
		|| !FMath::IsFinite(ScrapeResult.NetRemovedContaminationAmount)
		|| ScrapeResult.NetRemovedContaminationAmount < 0.0f)
	{
		return CommitResult;
	}

	if (!Allocation.bIsValid)
	{
		return CommitResult;
	}

	float PhysicalDropRatio = 1.0f;
	double PhysicalOwnershipRatio = 1.0;
	if (Allocation.bIsValid)
	{
		if (!FMath::IsFinite(Allocation.LoadOwnedRatio)
			|| !FMath::IsFinite(Allocation.PhysicalDropRatio)
			|| Allocation.LoadOwnedRatio < 0.0f
			|| Allocation.LoadOwnedRatio > 1.0f
			|| Allocation.PhysicalDropRatio < 0.0f
			|| Allocation.PhysicalDropRatio > 1.0f
			|| !FMath::IsNearlyEqual(
				Allocation.LoadOwnedRatio + Allocation.PhysicalDropRatio,
				1.0f,
				UE_SMALL_NUMBER))
		{
			return CommitResult;
		}
		PhysicalDropRatio = Allocation.PhysicalDropRatio;
		if (!FMath::IsFinite(Allocation.RequestedLoadAmount)
			|| !FMath::IsFinite(Allocation.ActualLoadAmount)
			|| Allocation.RequestedLoadAmount < 0.0f
			|| Allocation.ActualLoadAmount < 0.0f
			|| Allocation.ActualLoadAmount
				> Allocation.RequestedLoadAmount)
		{
			return CommitResult;
		}
		if (Allocation.RequestedLoadAmount > 0.0f)
		{
			const double AcceptedOwnershipRatio = FMath::Clamp(
				static_cast<double>(Allocation.ActualLoadAmount)
					/ static_cast<double>(Allocation.RequestedLoadAmount),
				0.0,
				1.0);
			PhysicalOwnershipRatio = 1.0 - AcceptedOwnershipRatio;
		}
		else
		{
			// transferred-only 등 Load 요청 분모가 없는 결과는 호출자가
			// 명시한 physical allocation을 그대로 따른다.
			PhysicalOwnershipRatio =
				static_cast<double>(PhysicalDropRatio);
		}
	}
	if (ScrapeResult.NetRemovedContaminationAmount <= 0.0f)
	{
		CommitResult.Status = EPhysicalCommitStatus::AcceptedNoPhysicalShare;
		return CommitResult;
	}

	// Tool도 같은 double Actual/Requested 비율로 lot을 추출한다. physical은
	// 그 정확한 상보를 써서 소수 unit이 두 갈래 사이에서 생기거나 사라지지 않는다.
	const double PhysicalDropAmount =
		static_cast<double>(ScrapeResult.NetRemovedContaminationAmount)
		* PhysicalOwnershipRatio;
	if (!FMath::IsFinite(PhysicalDropAmount)
		|| PhysicalDropAmount < 0.0f)
	{
		return CommitResult;
	}
	if (PhysicalDropAmount <= 0.0)
	{
		CommitResult.Status = EPhysicalCommitStatus::AcceptedNoPhysicalShare;
		return CommitResult;
	}

	TArray<FMD_ScrapeRecoveryLot> NetRecoveryLots;
	if (ScrapeResult.NetRemovedRecoveryLots.IsEmpty())
	{
		// old/synthetic 결과는 raw를 잃지 않되 canonical unit을 추측하지 않는다.
		if (!FMD_ScrapeRecoveryLedger::AppendZeroRewardCarrier(
				NetRecoveryLots,
				static_cast<double>(
					ScrapeResult.NetRemovedContaminationAmount)))
		{
			return CommitResult;
		}
	}
	else if (!TryAppendPersistenceRecoveryLots(
		NetRecoveryLots,
		ScrapeResult.NetRemovedRecoveryLots))
	{
		return CommitResult;
	}
	const double NetCarrierAmount =
		FMD_ScrapeRecoveryLedger::GetTotalCarrierAmount(NetRecoveryLots);
	const double ExpectedNetCarrierAmount =
		static_cast<double>(ScrapeResult.NetRemovedContaminationAmount);
	if (NetCarrierAmount < 0.0
		|| !AreRecoveryCarrierAmountsEqual(
			NetCarrierAmount,
			ExpectedNetCarrierAmount)
		|| !FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			NetRecoveryLots,
			ExpectedNetCarrierAmount))
	{
		return CommitResult;
	}

	TArray<FMD_ScrapeRecoveryLot> PhysicalRecoveryLots;
	if (!FMD_ScrapeRecoveryLedger::AppendScaledLots(
			PhysicalRecoveryLots,
			NetRecoveryLots,
			PhysicalOwnershipRatio)
		|| !FMD_ScrapeRecoveryLedger::RebaseCarrierAmount(
			PhysicalRecoveryLots,
			PhysicalDropAmount))
	{
		return CommitResult;
	}

	bool bAcceptedToLedger = false;
	CommitResult.SpawnedCount = DropContaminationItems(
		ScrapeResult.SurfaceFace,
		PhysicalDropAmount,
		SurfacePositionCm,
		PhysicalRecoveryLots,
		&bAcceptedToLedger);
	if (bAcceptedToLedger)
	{
		CommitResult.Status = EPhysicalCommitStatus::AcceptedPending;
		CommitResult.CommittedRawAmount = PhysicalDropAmount;
		CommitResult.CommittedRecoveryLots = MoveTemp(PhysicalRecoveryLots);
		CommitResult.CommittedRewardUnits =
			FMD_ScrapeRecoveryLedger::GetTotalRewardUnits(
				CommitResult.CommittedRecoveryLots);
	}
	return CommitResult;
}
