#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "Containers/BitArray.h"
#include "Gameplay/Scrape/MD_ScrapeSurfaceLayout.h"

bool FMD_WallContaminationRecord::CompactCells()
{
    if (CellPayload.Num()) return CellIndices.IsEmpty() && InitialAmounts.IsEmpty()
        && RemainingAmounts.IsEmpty() && TransferredAmounts.IsEmpty()
        && TransferredPatternDensities.IsEmpty() && CoveredFractions.IsEmpty();
    if (!CellPayload.Encode(CellIndices, InitialAmounts, RemainingAmounts,
        TransferredAmounts, TransferredPatternDensities, CoveredFractions)) return false;
    CellIndices.Empty();
    InitialAmounts.Empty();
    RemainingAmounts.Empty();
    TransferredAmounts.Empty();
    TransferredPatternDensities.Empty();
    CoveredFractions.Empty();
    return true;
}
bool FMD_WallContaminationRecord::ExpandCells(FMD_WallContaminationRecord& Out) const
{
    if (&Out == this) return false;
    Out = *this;
    if (!CellPayload.Num()) return true;
    Out.CellPayload = FMD_ContaminationSavePayload();
    return CellPayload.Decode(Out.CellIndices, Out.InitialAmounts, Out.RemainingAmounts,
        Out.TransferredAmounts, Out.TransferredPatternDensities, Out.CoveredFractions);
}
bool FMD_WallContaminationRecord::ValidateCellIndices(const int32 MaximumCellCount) const
{
    if (MaximumCellCount <= 0 || MaximumCellCount > FMD_ScrapeSurfaceLayout::MaximumCombinedCellCount)
        return false;
    if (CellPayload.Num())
    {
        return CellIndices.IsEmpty() && InitialAmounts.IsEmpty() && RemainingAmounts.IsEmpty()
            && TransferredAmounts.IsEmpty() && TransferredPatternDensities.IsEmpty()
            && CoveredFractions.IsEmpty() && CellPayload.ValidateIndices(MaximumCellCount);
    }
    const int32 Count = CellIndices.Num();
    if (Count > MaximumCellCount || InitialAmounts.Num() != Count || RemainingAmounts.Num() != Count
        || (!TransferredAmounts.IsEmpty() && TransferredAmounts.Num() != Count)
        || (!TransferredPatternDensities.IsEmpty() && TransferredPatternDensities.Num() != Count)
        || (!CoveredFractions.IsEmpty() && CoveredFractions.Num() != Count)) return false;
    TBitArray<> Seen(false, MaximumCellCount);
    for (const int32 Index : CellIndices)
    {
        if (Index < 0 || Index >= MaximumCellCount || Seen[Index]) return false;
        Seen[Index] = true;
    }
    return true;
}
void FMD_WallContaminationRecord::PostSerialize(const FArchive& Ar)
{
    // Old tagged-property records are converted before the next wall is deserialized.
    // A malformed legacy record remains intact for validation to reject; it is never silently dropped.
    if (Ar.IsLoading() && !Ar.IsError() && !CellPayload.Num()) CompactCells();
}
