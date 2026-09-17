#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "Containers/BitArray.h"
#include "Gameplay/Scrape/MD_ScrapeBoundaryState.h"
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
    // 생성 정보가 없는 이전 기록을 현재 표면에 적용하는 상황 차단
    if (Generation.IsValid() == false) return false;
    if (BoundaryPayload.Num())
    {
        FMD_ScrapeBoundaryState Boundary;
        if (LayoutVersion!=1 || !Boundary.Deserialize(BoundaryPayload.Get(),LayoutId,GridDimensions)) return false;
    }
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
        // 현재 전이 오염량 열 누락 차단
        || TransferredAmounts.Num() != Count
        // 현재 패턴 밀도 열 누락 차단
        || TransferredPatternDensities.Num() != Count
        || CoveredFractions.Num() != Count) return false;
    TBitArray<> Seen(false, MaximumCellCount);
    for (const int32 Index : CellIndices)
    {
        if (Index < 0 || Index >= MaximumCellCount || Seen[Index]) return false;
        Seen[Index] = true;
    }
    return true;
}
