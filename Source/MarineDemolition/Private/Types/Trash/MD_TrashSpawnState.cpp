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


bool FMD_TrashGeneratorPlacement::MatchesState(const FMD_TrashGeneratorState& State) const
{
    if (!State.IsStructurallyValid()) return false;
    TMap<FName, int32> Counts = State.RemainingKindCounts;
    TMap<FIntVector, TMap<FName, int32>> Cells;
    for (const auto& Cell : State.FloorCells) Cells.Add(Cell.Cell, Cell.RemainingKindCounts);
    for (const auto& Item : Items)
    {
        const FVector Scale = Item.Transform.GetScale3D();
        if (Item.KindId.IsNone() || Item.VariantId.IsNone() || !Item.Transform.IsValid()
            || Scale.GetAbsMin() <= UE_SMALL_NUMBER || Item.LinearVelocity.ContainsNaN() || Item.AngularVelocity.ContainsNaN()) return false;
        int32* Count = Counts.Find(Item.KindId);
        if (!Count || --*Count < 0) return false;
        if (State.bUsesFloorCells)
        {
            auto* Cell = Cells.Find(Item.OriginCell);
            int32* CellCount = Cell ? Cell->Find(Item.KindId) : nullptr;
            if (!CellCount || --*CellCount < 0) return false;
        }
    }
    for (const auto& Count : Counts) if (Count.Value != 0) return false;
    for (const auto& Cell : Cells)
        for (const auto& Count : Cell.Value) if (Count.Value != 0) return false;
    return true;
}
