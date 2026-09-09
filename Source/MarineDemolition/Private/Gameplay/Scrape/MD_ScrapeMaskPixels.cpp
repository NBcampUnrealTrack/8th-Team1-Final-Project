#include "Gameplay/Scrape/MD_ScrapeMaskPixels.h"
#include "Misc/Crc.h"
#include "Algo/BinarySearch.h"

FMD_ScrapeMaskPixels& FMD_ScrapeMaskPixels::operator=(const FMD_ScrapeMaskPixels& Other)
{
    if (this != &Other)
    {
        Initial = Other.Initial; Count = Other.Count; LivePageCount = Other.LivePageCount;
        if (Other.Pages.IsEmpty()) Pages.Empty(); else Pages = Other.Pages;
        if (Other.DirtyPages.IsEmpty()) DirtyPages.Empty(); else DirtyPages = Other.DirtyPages;
    }
    return *this;
}
FMD_ScrapeMaskPixels::FMD_ScrapeMaskPixels(FMD_ScrapeMaskPixels&& Other) noexcept
{
    *this = MoveTemp(Other);
}
FMD_ScrapeMaskPixels& FMD_ScrapeMaskPixels::operator=(FMD_ScrapeMaskPixels&& Other) noexcept
{
    if (this != &Other)
    {
        Initial = MoveTemp(Other.Initial); Pages = MoveTemp(Other.Pages);
        DirtyPages = MoveTemp(Other.DirtyPages); Count = Other.Count; LivePageCount = Other.LivePageCount;
        Other.Count = Other.LivePageCount = 0;
    }
    return *this;
}

FMD_ScrapeMaskPixels& FMD_ScrapeMaskPixels::operator=(TArray<uint8>&& Values)
{
    check(IsInGameThread());
    Empty();
    Count = Values.Num();
    if (!Count) return *this;
    const uint32 Hash = HashCombineFast(FCrc::MemCrc32(Values.GetData(), Count), GetTypeHash(Count));
    // The weak registry is also the content cache: no second retained hash table.
    PruneExpiredMetadata();
    for (const auto& Weak : InitialRegistry())
    {
        const auto Candidate = Weak.Pin();
        if (!Candidate || Candidate->Hash != Hash || Candidate->Count != Count) continue;
        bool bEqual = true;
        for (int32 I = 0; I < Count; ++I)
            if (Values[I] != (Candidate->Values.IsEmpty() ? Candidate->Uniform : Candidate->Values[I]))
            { bEqual = false; break; }
        if (bEqual) { Initial = Candidate; Values.Empty(); return *this; }
    }
    auto NewInitial = MakeShared<FInitial, ESPMode::ThreadSafe>();
    NewInitial->Hash = Hash; NewInitial->Count = Count;
    NewInitial->Uniform = Values[0];
    bool bUniform = true;
    for (uint8 V : Values) if (V != NewInitial->Uniform) { bUniform = false; break; }
    if (!bUniform) NewInitial->Values = MoveTemp(Values); else Values.Empty();
    Initial = NewInitial;
    InitialRegistry().RemoveAll([](const auto& Weak) { return !Weak.IsValid(); });
    InitialRegistry().Add(Initial);
    return *this;
}

uint8 FMD_ScrapeMaskPixels::operator[](int32 Index) const
{
    check(IsValidIndex(Index));
    if (!Pages.IsEmpty())
        if (const auto& Page = Pages[Index / PageSize])
        {
            const int32 Offset = Index % PageSize;
            if (Page->bDense) return Page->Values[Offset];
            const int32 Slot = Algo::LowerBound(Page->Offsets, uint16(Offset));
            if (Page->Offsets.IsValidIndex(Slot) && Page->Offsets[Slot] == Offset) return Page->Values[Slot];
            if (Page->bUniformBase) return Page->Uniform;
        }
    return Initial->Values.IsEmpty() ? Initial->Uniform : Initial->Values[Index];
}

bool FMD_ScrapeMaskPixels::Set(int32 Index, uint8 Value)
{
    check(IsValidIndex(Index));
    const uint8 OldValue = (*this)[Index];
    if (OldValue == Value) return false;
    const int32 PageIndex = Index / PageSize, Offset = Index % PageSize;
    const int32 PageCount = FMath::DivideAndRoundUp(Count, PageSize);
    if (Pages.IsEmpty()) Pages.SetNum(PageCount);
    if (DirtyPages.IsEmpty()) DirtyPages.Init(false, PageCount);
    DirtyPages[PageIndex] = true;
    auto& Page = Pages[PageIndex];
    const int32 Start = PageIndex * PageSize;
    const int32 Length = FMath::Min(PageSize, Count - Start);
    if (!Page)
    {
        auto NewPage = MakeShared<FPage, ESPMode::ThreadSafe>();
        if (Initial->Values.IsEmpty())
        {
            NewPage->Sum = uint32(Initial->Uniform) * Length;
            NewPage->SumSquares = uint32(Initial->Uniform) * Initial->Uniform * Length;
        }
        else for (int32 I = Start; I < Start + Length; ++I)
        {
            const uint8 V = Initial->Values[I];
            NewPage->Sum += V; NewPage->SumSquares += uint32(V) * V;
        }
        Page = MoveTemp(NewPage); ++LivePageCount;
        RegisterPage(Page);
    }
    else if (!Page.IsUnique())
    {
        Page = MakeShared<FPage, ESPMode::ThreadSafe>(*Page);
        RegisterPage(Page);
    }
    const uint8 OriginalValue = Initial->Values.IsEmpty() ? Initial->Uniform : Initial->Values[Index];
    Page->OriginalDeviations += int32(Value != OriginalValue) - int32(OldValue != OriginalValue);
    if (Page->OriginalDeviations == 0)
    {
        Page.Reset();
        if (--LivePageCount == 0) Pages.Empty();
        return true; // Dirty flags still restore these pixels on the GPU.
    }
    const auto BaseAt = [&](int32 LocalIndex)
    {
        return Page->bUniformBase ? Page->Uniform : (Initial->Values.IsEmpty() ? Initial->Uniform : Initial->Values[Start + LocalIndex]);
    };
    const uint8 Base = BaseAt(Offset);
    Page->Deviations += int32(Value != Base) - int32(OldValue != Base);
    if (Page->bDense) Page->Values[Offset] = Value;
    else
    {
        const int32 Slot = Algo::LowerBound(Page->Offsets, uint16(Offset));
        const bool bFound = Page->Offsets.IsValidIndex(Slot) && Page->Offsets[Slot] == Offset;
        if (Value == Base)
        {
            check(bFound);
            Page->Offsets.RemoveAt(Slot); Page->Values.RemoveAt(Slot);
        }
        else if (bFound) Page->Values[Slot] = Value;
        else { Page->Offsets.Insert(uint16(Offset), Slot); Page->Values.Insert(Value, Slot); }
        if (Page->Offsets.Num() > 128)
        {
            TArray<uint8> Dense; Dense.SetNumUninitialized(Length);
            for (int32 I = 0; I < Length; ++I) Dense[I] = BaseAt(I);
            for (int32 I = 0; I < Page->Offsets.Num(); ++I) Dense[Page->Offsets[I]] = Page->Values[I];
            Page->Values = MoveTemp(Dense); Page->Offsets.Empty(); Page->bDense = true;
        }
    }
    Page->Sum = Page->Sum - OldValue + Value;
    Page->SumSquares = Page->SumSquares - uint32(OldValue) * OldValue + uint32(Value) * Value;
    // Zero integer variance proves exact byte uniformity in constant time.
    if (uint64(Page->Sum) * Page->Sum == uint64(Length) * Page->SumSquares)
    {
        Page->Uniform = Value; Page->bUniformBase = true; Page->bDense = false;
        Page->Values.Empty(); Page->Offsets.Empty(); Page->Deviations = 0;
    }
    else if (Page->bDense && Page->Deviations <= 64)
    {
        TArray<uint8> Sparse; TArray<uint16> Offsets;
        for (int32 I = 0; I < Length; ++I)
            if (Page->Values[I] != BaseAt(I)) { Offsets.Add(uint16(I)); Sparse.Add(Page->Values[I]); }
        Page->Offsets = MoveTemp(Offsets); Page->Values = MoveTemp(Sparse); Page->bDense = false;
    }
    return true;
}

void FMD_ScrapeMaskPixels::Empty()
{
    Initial.Reset(); Pages.Empty(); DirtyPages.Empty(); Count = LivePageCount = 0;
}

SIZE_T FMD_ScrapeMaskPixels::GetAllocatedSize() const
{
    SIZE_T Bytes = Pages.GetAllocatedSize() + DirtyPages.GetAllocatedSize();
    for (const auto& Page : Pages) if (Page) Bytes += sizeof(FPage) + Page->Values.GetAllocatedSize() + Page->Offsets.GetAllocatedSize();
    return Bytes;
}
SIZE_T FMD_ScrapeMaskPixels::GetSharedInitialAllocatedSize() const
{
    return Initial ? sizeof(FInitial) + Initial->Values.GetAllocatedSize() : 0;
}
bool FMD_ScrapeMaskPixels::operator==(const TArray<uint8>& Other) const
{
    if (Count != Other.Num()) return false;
    for (int32 I = 0; I < Count; ++I) if ((*this)[I] != Other[I]) return false;
    return true;
}
void FMD_ScrapeMaskPixels::CopyRange(int32 Start, int32 Length, uint8* Out) const
{
    check(Start >= 0 && Length >= 0 && Start + Length <= Count);
    for (int32 I = 0; I < Length; ++I) Out[I] = (*this)[Start + I];
}

TArray<TWeakPtr<const FMD_ScrapeMaskPixels::FInitial, ESPMode::ThreadSafe>>& FMD_ScrapeMaskPixels::InitialRegistry()
{
    static TArray<TWeakPtr<const FInitial, ESPMode::ThreadSafe>> Registry;
    return Registry;
}
TArray<TWeakPtr<FMD_ScrapeMaskPixels::FPage, ESPMode::ThreadSafe>>& FMD_ScrapeMaskPixels::PageRegistry()
{
    static TArray<TWeakPtr<FPage, ESPMode::ThreadSafe>> Registry;
    return Registry;
}
FMD_ScrapeMaskPixelMemoryStats FMD_ScrapeMaskPixels::GetGlobalMemoryStats()
{
    check(IsInGameThread());
    FMD_ScrapeMaskPixelMemoryStats Result;
    auto& Initials = InitialRegistry();
    auto& PageItems = PageRegistry();
    for (int32 I = Initials.Num() - 1; I >= 0; --I)
    {
        if (auto Item = Initials[I].Pin()) { ++Result.InitialCount; Result.InitialBytes += sizeof(FInitial) + Item->Values.GetAllocatedSize(); }
        else Initials.RemoveAtSwap(I);
    }
    for (int32 I = PageItems.Num() - 1; I >= 0; --I)
    {
        if (auto Item = PageItems[I].Pin()) { ++Result.PageCount; Result.PageBytes += sizeof(FPage) + Item->Values.GetAllocatedSize() + Item->Offsets.GetAllocatedSize(); }
        else PageItems.RemoveAtSwap(I);
    }
    if (Initials.IsEmpty()) Initials.Empty();
    if (PageItems.IsEmpty()) PageItems.Empty();
    Result.RegistryBytes = Initials.GetAllocatedSize() + PageItems.GetAllocatedSize();
    return Result;
}

void FMD_ScrapeMaskPixels::RegisterPage(const TSharedPtr<FPage, ESPMode::ThreadSafe>& Page)
{
    auto& Registry = PageRegistry();
    static int32 Cursor = 0;
    // Bounded amortized metadata reclamation; never scan every live page per edit.
    for (int32 I = 0; I < 16 && !Registry.IsEmpty(); ++I)
    {
        Cursor %= Registry.Num();
        if (!Registry[Cursor].IsValid()) Registry.RemoveAtSwap(Cursor, EAllowShrinking::No);
        else ++Cursor;
    }
    Registry.Add(Page);
}


bool FMD_ScrapeMaskPixels::PruneExpiredMetadata(int32 WorkBudget)
{
    check(IsInGameThread());
    auto& Initials = InitialRegistry();
    auto& PageItems = PageRegistry();
    static int32 InitialCursor = 0, PageCursor = 0;
    auto Sweep = [](auto& Entries, int32& Cursor, int32 Budget)
    {
        const int32 Steps = FMath::Min(Budget, Entries.Num());
        for (int32 I = 0; I < Steps && !Entries.IsEmpty(); ++I)
        {
            Cursor %= Entries.Num();
            if (!Entries[Cursor].IsValid()) Entries.RemoveAtSwap(Cursor, EAllowShrinking::No);
            else ++Cursor;
        }
        if (Entries.IsEmpty()) { Entries.Empty(); Cursor = 0; }
        else if (Entries.Max() > 256 && int64(Entries.Num()) * 4 < Entries.Max()) Entries.Shrink();
    };
    const int32 InitialBudget = FMath::Min(Initials.Num(), FMath::Max(1, WorkBudget / 8));
    Sweep(Initials, InitialCursor, InitialBudget);
    Sweep(PageItems, PageCursor, FMath::Max(0, WorkBudget - InitialBudget));
    return !Initials.IsEmpty() || !PageItems.IsEmpty();
}
