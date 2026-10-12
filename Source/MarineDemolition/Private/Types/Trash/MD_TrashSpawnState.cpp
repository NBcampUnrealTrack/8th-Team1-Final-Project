#include "Types/Trash/MD_TrashSpawnState.h"

bool FMD_TrashGeneratorState::IsStructurallyValid() const
{
	if (GeneratorName.IsNone()) return false;
	int64 Total = 0;
	for (const auto& Entry : RemainingKindCounts)
	{
		if (Entry.Key.IsNone() || Entry.Value < 0) return false;
		Total += Entry.Value;
		if (Total > MAX_int32) return false;
	}
	if (!bUsesFloorCells)
	{
		return !bUsesTowerPlacement && FloorCells.IsEmpty() && !FloorLayoutId.IsValid()
			&& FloorCellSize == 0.0f && FloorLayerHeight == 0.0f;
	}
	if (!FMath::IsFinite(FloorCellSize) || FloorCellSize < 50.0f
		|| !FMath::IsFinite(FloorLayerHeight) || FloorLayerHeight < 50.0f
		|| !FloorLayoutId.IsValid() || FloorCells.IsEmpty()) return false;
	TSet<FIntVector> SeenCells;
	TMap<FName, int64> CellTotals;
	int64 CellTotal = 0;
	for (const FMD_TrashFloorCellState& State : FloorCells)
	{
		if (SeenCells.Contains(State.Cell)) return false;
		SeenCells.Add(State.Cell);
		for (const auto& Entry : State.RemainingKindCounts)
		{
			if (Entry.Key.IsNone() || Entry.Value < 0 || !RemainingKindCounts.Contains(Entry.Key)) return false;
			CellTotal += Entry.Value;
			if (CellTotal > MAX_int32) return false;
			CellTotals.FindOrAdd(Entry.Key) += Entry.Value;
		}
	}
	if (CellTotal != Total) return false;
	for (const auto& Entry : RemainingKindCounts)
	{
		if (CellTotals.FindRef(Entry.Key) != Entry.Value) return false;
	}
	return true;
}

