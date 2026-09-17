#include "Gameplay/Scrape/MD_ScrapePagedState.h"

#include "Hash/xxhash.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <type_traits>

TRACE_DECLARE_INT_COUNTER(MD_ScrapeState_LiveInitialBytes, TEXT("MD/ScrapeState/LiveInitialBytes"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeState_LiveInitialTemplates, TEXT("MD/ScrapeState/LiveInitialTemplates"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeState_LivePageBytes, TEXT("MD/ScrapeState/LivePageBytes"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeState_LivePages, TEXT("MD/ScrapeState/LivePages"));

namespace MD_ScrapePagedStatePrivate
{
    // The byte oracle deliberately fails compilation if the serialized cell contract grows.
    static_assert(sizeof(FMD_ScrapeSurfaceCell) == 5 * sizeof(float));
    static_assert(std::is_trivially_copyable_v<FMD_ScrapeSurfaceCell>);
    FMD_ScrapePagedStateMemoryStats LiveStats;

    bool IsCanonicalInitial(const FMD_ScrapeSurfaceCell& Cell)
    {
        uint32 Bits[5];
        FMemory::Memcpy(Bits, &Cell, sizeof(Bits));
        return Bits[0] == Bits[1] && Bits[2] == 0 && Bits[3] == 0 && Bits[4] == 0;
    }

    void TraceInitialStats()
    {
        TRACE_COUNTER_SET(MD_ScrapeState_LiveInitialBytes, LiveStats.LiveInitialBytes);
        TRACE_COUNTER_SET(MD_ScrapeState_LiveInitialTemplates, LiveStats.LiveInitialTemplates);
    }
    void TracePageStats()
    {
        TRACE_COUNTER_SET(MD_ScrapeState_LivePageBytes, LiveStats.LivePageBytes);
        TRACE_COUNTER_SET(MD_ScrapeState_LivePages, LiveStats.LivePages);
    }
}

struct FMD_ScrapePagedState::FInitial
{
    TArray<FMD_ScrapeSurfaceCell> Cells;
    TArray<float> InitialAmounts;
    FMD_ScrapeSurfaceCell UniformCell;
    int32 Count;
    bool bUniform;
    bool bCanonicalAmounts;

    FInitial(TArray<FMD_ScrapeSurfaceCell>&& InCells, const FMD_ScrapeSurfaceCell& InUniform,
        const int32 InCount, const bool bInUniform, const bool bInCanonicalAmounts)
        : Count(InCount), bUniform(bInUniform), bCanonicalAmounts(bInCanonicalAmounts)
    {
        check(IsInGameThread());
        FMemory::Memcpy(&UniformCell, &InUniform, sizeof(UniformCell));
        if (bCanonicalAmounts)
        {
            InitialAmounts.SetNumUninitialized(Count);
            for (int32 Index = 0; Index < Count; ++Index)
                FMemory::Memcpy(&InitialAmounts[Index], &InCells[Index].InitialContamination, sizeof(float));
            InCells.Empty();
        }
        else
        {
            Cells = MoveTemp(InCells);
        }
        auto& Stats = MD_ScrapePagedStatePrivate::LiveStats;
        Stats.LiveInitialBytes += GetAllocatedSize();
        ++Stats.LiveInitialTemplates;
        MD_ScrapePagedStatePrivate::TraceInitialStats();
    }
    explicit FInitial(TArray<float>&& InAmounts)
        : InitialAmounts(MoveTemp(InAmounts)), Count(InitialAmounts.Num()), bUniform(false), bCanonicalAmounts(true)
    {
        check(IsInGameThread());
        auto& Stats = MD_ScrapePagedStatePrivate::LiveStats;
        Stats.LiveInitialBytes += GetAllocatedSize();
        ++Stats.LiveInitialTemplates;
        MD_ScrapePagedStatePrivate::TraceInitialStats();
    }
    ~FInitial()
    {
        auto& Stats = MD_ScrapePagedStatePrivate::LiveStats;
        Stats.LiveInitialBytes -= GetAllocatedSize();
        --Stats.LiveInitialTemplates;
        MD_ScrapePagedStatePrivate::TraceInitialStats();
    }
    SIZE_T GetAllocatedSize() const { return sizeof(*this) + Cells.GetAllocatedSize() + InitialAmounts.GetAllocatedSize(); }
};

uint32 FMD_ScrapePagedState::FChannel::ReadBits(const int32 Offset, const uint32 InitialBits) const
{
    if (Mode == EMode::Initial) return InitialBits;
    if (Mode == EMode::Uniform) return UniformBits;
    if (Mode == EMode::Dense) return Values[Offset];
    int32 Low = 0;
    int32 High = Indices.Num();
    while (Low < High)
    {
        const int32 Mid = (Low + High) / 2;
        if (Indices[Mid] < Offset) Low = Mid + 1;
        else High = Mid;
    }
    if (Indices.IsValidIndex(Low) && Indices[Low] == Offset) return Values[Low];
    return Mode == EMode::SparseInitial ? InitialBits : UniformBits;
}

void FMD_ScrapePagedState::FChannel::Reset(const int32 Count)
{
    Values.Empty();
    Indices.Empty();
    SumBits = 0;
    UniformBits = 0;
    InitialMatches = static_cast<uint16>(Count);
    Mode = EMode::Initial;
}

void FMD_ScrapePagedState::FChannel::Initialize(
    const uint32* Current, const uint32* Original, const int32 Count)
{
    Reset(Count);
    int32 Matches = 0;
    bool bUniform = true;
    for (int32 Offset = 0; Offset < Count; ++Offset)
    {
        Matches += Current[Offset] == Original[Offset] ? 1 : 0;
        bUniform &= Current[Offset] == Current[0];
    }
    InitialMatches = static_cast<uint16>(Matches);
    if (Matches == Count) return;
    if (bUniform)
    {
        Mode = EMode::Uniform;
        UniformBits = Current[0];
    }
    else if (Count - Matches <= 64)
    {
        Mode = EMode::SparseInitial;
        Values.Reserve(Count - Matches);
        Indices.Reserve(Count - Matches);
        for (int32 Offset = 0; Offset < Count; ++Offset)
        {
            if (Current[Offset] != Original[Offset])
            {
                Indices.Add(static_cast<uint8>(Offset));
                Values.Add(Current[Offset]);
            }
        }
    }
    else
    {
        Mode = EMode::Dense;
        Values.SetNumUninitialized(Count);
        FMemory::Memcpy(Values.GetData(), Current, Count * sizeof(uint32));
        for (int32 Offset = 0; Offset < Count; ++Offset) SumBits += Current[Offset];
    }
}

void FMD_ScrapePagedState::FChannel::SetBits(const int32 Offset, const uint32 Bits,
    const uint32 InitialBits, const FMD_ScrapePagedState& Owner, const int32 PageIndex,
    const int32 ChannelIndex, const int32 Count)
{
    const uint32 Previous = ReadBits(Offset, InitialBits);
    if (Previous == Bits) return;
    InitialMatches = static_cast<uint16>(static_cast<int32>(InitialMatches)
        + (Bits == InitialBits ? 1 : 0) - (Previous == InitialBits ? 1 : 0));
    if (InitialMatches == Count)
    {
        Reset(Count);
        return;
    }
    if (Mode == EMode::Dense)
    {
        Values[Offset] = Bits;
        SumBits = SumBits - Previous + Bits;
        // An exact integer sum is only a candidate filter. Verify every bit before
        // collapsing; neither float arithmetic nor hash collisions may lose state.
        if (SumBits == static_cast<uint64>(Bits) * Count)
        {
            bool bUniform = true;
            for (const uint32 Value : Values) bUniform &= Value == Bits;
            if (bUniform)
            {
                Values.Empty();
                Mode = EMode::Uniform;
                UniformBits = Bits;
                SumBits = 0;
                return;
            }
        }
        // Hysteresis avoids sparse/dense allocation churn near the promotion limit.
        if (Count - InitialMatches <= 32)
        {
            uint32 Current[PageSize];
            uint32 Original[PageSize];
            FMemory::Memcpy(Current, Values.GetData(), Count * sizeof(uint32));
            Owner.ReadInitialChannel(PageIndex, ChannelIndex, Original);
            Initialize(Current, Original, Count);
        }
        return;
    }
    if (Mode == EMode::Initial) Mode = EMode::SparseInitial;
    else if (Mode == EMode::Uniform) Mode = EMode::SparseUniform;
    const uint32 DefaultBits = Mode == EMode::SparseInitial ? InitialBits : UniformBits;
    int32 Low = 0;
    int32 High = Indices.Num();
    while (Low < High)
    {
        const int32 Mid = (Low + High) / 2;
        if (Indices[Mid] < Offset) Low = Mid + 1;
        else High = Mid;
    }
    const bool bPresent = Indices.IsValidIndex(Low) && Indices[Low] == Offset;
    if (Bits == DefaultBits)
    {
        check(bPresent);
        Values.RemoveAt(Low, 1, EAllowShrinking::No);
        Indices.RemoveAt(Low, 1, EAllowShrinking::No);
        // Return formerly dense/sizable sparse capacity as exceptions disappear.
        // Quarter-full hysteresis avoids reallocating on each rollback cell.
        if (Values.Num() > 0 && Values.Num() <= Values.Max() / 4)
        {
            Values.Shrink();
            Indices.Shrink();
        }
    }
    else if (bPresent)
    {
        Values[Low] = Bits;
    }
    else
    {
        Values.Insert(Bits, Low);
        Indices.Insert(static_cast<uint8>(Offset), Low);
    }
    if (Values.IsEmpty())
    {
        Values.Empty();
        Indices.Empty();
        Mode = Mode == EMode::SparseInitial ? EMode::Initial : EMode::Uniform;
    }
    else if (Values.Num() > 64 || (Values.Num() == Count && !bPresent))
    {
        uint32 Current[PageSize];
        uint32 Original[PageSize];
        Owner.ReadInitialChannel(PageIndex, ChannelIndex, Original);
        for (int32 CellIndex = 0; CellIndex < Count; ++CellIndex)
            Current[CellIndex] = ReadBits(CellIndex, Original[CellIndex]);
        Initialize(Current, Original, Count);
    }
}

FMD_ScrapePagedState::FPage::FPage(const int32 InCount) : Count(InCount)
{
    for (FChannel& Channel : Channels) Channel.InitialMatches = static_cast<uint16>(Count);
    auto& Stats = MD_ScrapePagedStatePrivate::LiveStats;
    Stats.LivePageBytes += GetAllocatedSize();
    ++Stats.LivePages;
    MD_ScrapePagedStatePrivate::TracePageStats();
}

FMD_ScrapePagedState::FPage::FPage(const FPage& Other) : FPage(Other.Count)
{
    const SIZE_T Before = GetAllocatedSize();
    for (int32 ChannelIndex = 0; ChannelIndex < 5; ++ChannelIndex)
        Channels[ChannelIndex] = Other.Channels[ChannelIndex];
    ExplicitCells.SetNumUninitialized(Other.ExplicitCells.Num());
    if (!ExplicitCells.IsEmpty())
        FMemory::Memcpy(ExplicitCells.GetData(), Other.ExplicitCells.GetData(), ExplicitCells.Num() * sizeof(FMD_ScrapeSurfaceCell));
    AccountResize(Before);
}

FMD_ScrapePagedState::FPage::~FPage()
{
    auto& Stats = MD_ScrapePagedStatePrivate::LiveStats;
    Stats.LivePageBytes -= GetAllocatedSize();
    --Stats.LivePages;
    MD_ScrapePagedStatePrivate::TracePageStats();
}

FMD_ScrapeSurfaceCell FMD_ScrapePagedState::FPage::Read(
    const int32 Offset, const FMD_ScrapeSurfaceCell& InitialCell) const
{
    FMD_ScrapeSurfaceCell Cell;
    if (!ExplicitCells.IsEmpty())
    {
        FMemory::Memcpy(&Cell, &ExplicitCells[Offset], sizeof(Cell));
        return Cell;
    }
    uint32 Bits[5];
    FMemory::Memcpy(Bits, &InitialCell, sizeof(Bits));
    for (int32 ChannelIndex = 0; ChannelIndex < 5; ++ChannelIndex)
        Bits[ChannelIndex] = Channels[ChannelIndex].ReadBits(Offset, Bits[ChannelIndex]);
    FMemory::Memcpy(&Cell, Bits, sizeof(Cell));
    return Cell;
}

SIZE_T FMD_ScrapePagedState::FPage::GetAllocatedSize() const
{
    SIZE_T Bytes = sizeof(*this) + ExplicitCells.GetAllocatedSize();
    for (const FChannel& Channel : Channels) Bytes += Channel.GetAllocatedSize();
    return Bytes;
}

void FMD_ScrapePagedState::FPage::AccountResize(const SIZE_T PreviousBytes) const
{
    const SIZE_T CurrentBytes = GetAllocatedSize();
    if (PreviousBytes != CurrentBytes)
    {
        auto& Stats = MD_ScrapePagedStatePrivate::LiveStats;
        Stats.LivePageBytes = Stats.LivePageBytes - PreviousBytes + CurrentBytes;
        MD_ScrapePagedStatePrivate::TracePageStats();
    }
}

bool FMD_ScrapePagedState::FPage::IsInitial() const
{
    if (!ExplicitCells.IsEmpty()) return false;
    for (const FChannel& Channel : Channels)
        if (Channel.Mode != FChannel::EMode::Initial) return false;
    return true;
}

bool FMD_ScrapePagedState::AreCellsBitIdentical(
    const FMD_ScrapeSurfaceCell& A, const FMD_ScrapeSurfaceCell& B)
{
    return FMemory::Memcmp(&A, &B, sizeof(A)) == 0;
}

TSharedPtr<const FMD_ScrapePagedState::FInitial, ESPMode::NotThreadSafe> FMD_ScrapePagedState::FindOrAddInitial(
    TArray<FMD_ScrapeSurfaceCell>&& Cells, const FMD_ScrapeSurfaceCell& UniformCell,
    const int32 Count, const bool bUniform, const bool bCanonicalAmounts)
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ScrapeState_InternInitial);
    if (bCanonicalAmounts)
    {
        TArray<float> Amounts;
        Amounts.SetNumUninitialized(Count);
        for (int32 Index = 0; Index < Count; ++Index)
            FMemory::Memcpy(&Amounts[Index], &Cells[Index].InitialContamination, sizeof(float));
        Cells.Empty();
        return FindOrAddInitialAmounts(MoveTemp(Amounts));
    }
    struct FCacheEntry
    {
        uint64 Hash;
        TWeakPtr<const FInitial, ESPMode::NotThreadSafe> Initial;
    };
    // Weak entries never retain the dense baseline. Expired control blocks are pruned
    // on the next bind; cache metadata is outside the live payload counter contract.
    static TArray<FCacheEntry> Cache;
    const uint64 Hash = FXxHash64::HashBuffer(
        bUniform ? static_cast<const void*>(&UniformCell) : static_cast<const void*>(Cells.GetData()),
        bUniform ? sizeof(UniformCell) : static_cast<uint64>(Count) * sizeof(FMD_ScrapeSurfaceCell)).Hash;
    Cache.RemoveAllSwap([](const FCacheEntry& Entry) { return !Entry.Initial.IsValid(); }, EAllowShrinking::No);

    for (int32 Index = Cache.Num() - 1; Index >= 0; --Index)
    {
        TSharedPtr<const FInitial, ESPMode::NotThreadSafe> Existing = Cache[Index].Initial.Pin();
        if (!Existing)
        {
            Cache.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            continue;
        }
        if (Cache[Index].Hash != Hash || Existing->Count != Count || Existing->bUniform != bUniform
            || Existing->bCanonicalAmounts != bCanonicalAmounts)
        {
            continue;
        }
        bool bIdentical;
        if (bUniform)
        {
            bIdentical = AreCellsBitIdentical(Existing->UniformCell, UniformCell);
        }
        else if (bCanonicalAmounts)
        {
            // Both sides have already proved the other four fields are exact functions
            // of this amount (+0 is required for the three empty fields).
            bIdentical = true;
            for (int32 CellIndex = 0; CellIndex < Count; ++CellIndex)
            {
                if (FMemory::Memcmp(&Existing->InitialAmounts[CellIndex],
                    &Cells[CellIndex].InitialContamination, sizeof(float)) != 0)
                {
                    bIdentical = false;
                    break;
                }
            }
        }
        else
        {
            bIdentical = FMemory::Memcmp(Existing->Cells.GetData(), Cells.GetData(),
                static_cast<SIZE_T>(Count) * sizeof(FMD_ScrapeSurfaceCell)) == 0;
        }
        if (bIdentical)
        {
            return Existing;
        }
    }
    TSharedPtr<const FInitial, ESPMode::NotThreadSafe> Result = MakeShared<FInitial, ESPMode::NotThreadSafe>(MoveTemp(Cells), UniformCell, Count, bUniform, bCanonicalAmounts);
    Cache.Add({Hash, Result});
    return Result;
}

TSharedPtr<const FMD_ScrapePagedState::FInitial, ESPMode::NotThreadSafe>
FMD_ScrapePagedState::FindOrAddInitialAmounts(TArray<float>&& Amounts)
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ScrapeState_InternAmounts);
    struct FEntry { uint64 Hash; TWeakPtr<const FInitial, ESPMode::NotThreadSafe> Initial; };
    static TArray<FEntry> Cache;
    const SIZE_T Bytes = Amounts.Num() * sizeof(float);
    const uint64 Hash = FXxHash64::HashBuffer(Amounts.GetData(), Bytes).Hash;
    Cache.RemoveAllSwap([](const FEntry& Entry) { return !Entry.Initial.IsValid(); }, EAllowShrinking::No);
    for (const auto& Entry : Cache)
    {
        const auto Existing = Entry.Initial.Pin();
        if (Entry.Hash == Hash && Existing && Existing->Count == Amounts.Num()
            && FMemory::Memcmp(Existing->InitialAmounts.GetData(), Amounts.GetData(), Bytes) == 0)
        {
            Amounts.Empty();
            return Existing;
        }
    }
    auto Result = MakeShared<FInitial, ESPMode::NotThreadSafe>(MoveTemp(Amounts));
    Cache.Add({Hash, Result});
    return Result;
}

void FMD_ScrapePagedState::InitializeFromAmounts(TArray<float>&& Amounts)
{
    check(IsInGameThread());
    if (Amounts.IsEmpty()) { Reset(); return; }
    uint32 FirstBits;
    FMemory::Memcpy(&FirstBits, Amounts.GetData(), sizeof(float));
    bool bUniform = true;
    for (const float& Amount : Amounts)
    {
        uint32 Bits;
        FMemory::Memcpy(&Bits, &Amount, sizeof(float));
        if (Bits != FirstBits) { bUniform = false; break; }
    }
    if (bUniform)
    {
        FMD_ScrapeSurfaceCell Cell;
        FMemory::Memcpy(&Cell.InitialContamination, &FirstBits, sizeof(float));
        FMemory::Memcpy(&Cell.RemainingContamination, &FirstBits, sizeof(float));
        InitializeUniform(Amounts.Num(), Cell);
        Amounts.Empty();
    }
    else BindInitial(FindOrAddInitialAmounts(MoveTemp(Amounts)));
}

void FMD_ScrapePagedState::BindInitial(TSharedPtr<const FInitial, ESPMode::NotThreadSafe> InInitial)
{
    Pages.Empty();
    NonNullPageCount = 0;
    Initial = MoveTemp(InInitial);
    CellCount = Initial ? Initial->Count : 0;
    bUniformInitial = !Initial || Initial->bUniform;
    InitialAmounts = Initial && Initial->bCanonicalAmounts ? Initial->InitialAmounts.GetData() : nullptr;
    InitialCells = Initial ? (bUniformInitial ? &Initial->UniformCell : Initial->Cells.GetData()) : nullptr;
}

void FMD_ScrapePagedState::InitializeUniform(const int32 Count, const FMD_ScrapeSurfaceCell& Cell)
{
    check(IsInGameThread());
    check(Count >= 0);
    if (Count == 0)
    {
        Reset();
        return;
    }
    BindInitial(FindOrAddInitial({}, Cell, Count, true, false));
}

void FMD_ScrapePagedState::InitializeFromCells(TArray<FMD_ScrapeSurfaceCell>&& Cells)
{
    check(IsInGameThread());
    if (Cells.IsEmpty())
    {
        Reset();
        return;
    }
    const int32 Count = Cells.Num();
    FMD_ScrapeSurfaceCell First;
    FMemory::Memcpy(&First, &Cells[0], sizeof(First));
    bool bUniform = true;
    bool bCanonicalAmounts = true;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        bUniform &= AreCellsBitIdentical(First, Cells[Index]);
        bCanonicalAmounts &= MD_ScrapePagedStatePrivate::IsCanonicalInitial(Cells[Index]);
        if (!bUniform && !bCanonicalAmounts) break;
    }
    if (bUniform)
    {
        InitializeUniform(Count, First);
        Cells.Empty();
    }
    else
    {
        BindInitial(FindOrAddInitial(MoveTemp(Cells), First, Count, false, bCanonicalAmounts));
        // A cache hit did not move the supplied array; consume it in both cases.
        Cells.Empty();
    }
}

void FMD_ScrapePagedState::ForEachSaveCandidate(TFunctionRef<void(int32)> Visit) const
{
    if (!Initial) return;
    const bool bCanonical = Initial->bCanonicalAmounts || (Initial->bUniform
        && MD_ScrapePagedStatePrivate::IsCanonicalInitial(Initial->UniformCell));
    if (!bCanonical)
    {
        for (int32 I = 0; I < CellCount; ++I) Visit(I);
        return;
    }
    for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
        if (Pages[PageIndex])
            for (int32 I = PageIndex * PageSize; I < FMath::Min((PageIndex + 1) * PageSize, CellCount); ++I)
                Visit(I);
}

bool FMD_ScrapePagedState::IsPristineCanonicalInitial() const
{
    return Initial && NonNullPageCount == 0
        && (Initial->bCanonicalAmounts || (Initial->bUniform
            && MD_ScrapePagedStatePrivate::IsCanonicalInitial(Initial->UniformCell)));
}

FMD_ScrapePagedState& FMD_ScrapePagedState::operator=(const FMD_ScrapePagedState& Other)
{
    check(IsInGameThread());
    if (this != &Other)
    {
        Initial = Other.Initial;
        // TArray copy assignment may retain minimum capacity for an empty source.
        // Reconciliation to a pristine snapshot must release the old page table too.
        if (Other.Pages.IsEmpty()) { Pages.Empty(); }
        else { Pages = Other.Pages; }
        InitialCells = Other.InitialCells;
        InitialAmounts = Other.InitialAmounts;
        CellCount = Other.CellCount;
        NonNullPageCount = Other.NonNullPageCount;
        bUniformInitial = Other.bUniformInitial;
    }
    return *this;
}

FMD_ScrapePagedState::FMD_ScrapePagedState(FMD_ScrapePagedState&& Other) noexcept
{
    *this = MoveTemp(Other);
}

FMD_ScrapePagedState& FMD_ScrapePagedState::operator=(FMD_ScrapePagedState&& Other) noexcept
{
    if (this != &Other)
    {
        Initial = MoveTemp(Other.Initial);
        Pages = MoveTemp(Other.Pages);
        InitialCells = Other.InitialCells;
        InitialAmounts = Other.InitialAmounts;
        CellCount = Other.CellCount;
        NonNullPageCount = Other.NonNullPageCount;
        bUniformInitial = Other.bUniformInitial;
        Other.InitialCells = nullptr;
        Other.InitialAmounts = nullptr;
        Other.CellCount = 0;
        Other.NonNullPageCount = 0;
        Other.bUniformInitial = true;
    }
    return *this;
}

void FMD_ScrapePagedState::Reset()
{
    check(IsInGameThread());
    BindInitial(nullptr);
}

FMD_ScrapePagedState::FPage& FMD_ScrapePagedState::EditPage(const int32 PageIndex)
{
    if (Pages.IsEmpty()) Pages.SetNum((CellCount - 1) / PageSize + 1);
    TSharedPtr<FPage, ESPMode::NotThreadSafe>& Page = Pages[PageIndex];
    if (!Page)
    {
        Page = MakeShared<FPage, ESPMode::NotThreadSafe>(FMath::Min(PageSize, CellCount - PageIndex * PageSize));
        ++NonNullPageCount;
    }
    else if (!Page.IsUnique())
    {
        Page = MakeShared<FPage, ESPMode::NotThreadSafe>(*Page);
    }
    return *Page;
}

void FMD_ScrapePagedState::ReadInitialChannel(
    const int32 PageIndex, const int32 ChannelIndex, uint32* OutBits) const
{
    const int32 Begin = PageIndex * PageSize;
    const int32 Count = FMath::Min(PageSize, CellCount - Begin);
    for (int32 Offset = 0; Offset < Count; ++Offset)
    {
        const FMD_ScrapeSurfaceCell Cell = GetInitialCell(Begin + Offset);
        FMemory::Memcpy(OutBits + Offset, reinterpret_cast<const uint8*>(&Cell) + ChannelIndex * sizeof(uint32), sizeof(uint32));
    }
}

FMD_ScrapeSurfaceCell& FMD_ScrapePagedState::Edit(const int32 Index)
{
    check(IsInGameThread());
    check(IsValidIndex(Index));
    const int32 PageIndex = Index / PageSize;
    FPage& Page = EditPage(PageIndex);
    if (Page.ExplicitCells.IsEmpty())
    {
        const SIZE_T Before = Page.GetAllocatedSize();
        // Read before publishing the dense fallback: Read selects ExplicitCells when nonempty.
        FMD_ScrapeSurfaceCell Current[PageSize];
        for (int32 Offset = 0; Offset < Page.Count; ++Offset)
        {
            const FMD_ScrapeSurfaceCell Cell = Page.Read(Offset, GetInitialCell(PageIndex * PageSize + Offset));
            FMemory::Memcpy(Current + Offset, &Cell, sizeof(Cell));
        }
        Page.ExplicitCells.SetNumUninitialized(Page.Count);
        FMemory::Memcpy(Page.ExplicitCells.GetData(), Current, Page.Count * sizeof(FMD_ScrapeSurfaceCell));
        for (FChannel& Channel : Page.Channels) Channel.Reset(Page.Count);
        Page.AccountResize(Before);
    }
    return Page.ExplicitCells[Index % PageSize];
}

void FMD_ScrapePagedState::Set(const int32 Index, const FMD_ScrapeSurfaceCell& Cell)
{
    check(IsInGameThread());
    if (AreCellsBitIdentical(Read(Index), Cell)) return;
    // Copy before detachment/recompression, including when Cell aliases an Edit result.
    uint32 Bits[5];
    FMemory::Memcpy(Bits, &Cell, sizeof(Bits));
    const int32 PageIndex = Index / PageSize;
    FPage& Page = EditPage(PageIndex);
    if (!Page.ExplicitCells.IsEmpty()) CompactExplicitPage(PageIndex);
    const SIZE_T Before = Page.GetAllocatedSize();
    const FMD_ScrapeSurfaceCell Original = GetInitialCell(Index);
    uint32 InitialBits[5];
    FMemory::Memcpy(InitialBits, &Original, sizeof(InitialBits));
    for (int32 ChannelIndex = 0; ChannelIndex < 5; ++ChannelIndex)
        Page.Channels[ChannelIndex].SetBits(Index % PageSize, Bits[ChannelIndex], InitialBits[ChannelIndex],
            *this, PageIndex, ChannelIndex, Page.Count);
    Page.AccountResize(Before);
    ReleaseInitialPage(PageIndex);
}

void FMD_ScrapePagedState::CompactExplicitPage(const int32 PageIndex)
{
    FPage& Page = *Pages[PageIndex];
    if (Page.ExplicitCells.IsEmpty()) return;
    check(Pages[PageIndex].IsUnique());
    const SIZE_T Before = Page.GetAllocatedSize();
    uint32 Current[PageSize];
    uint32 Original[PageSize];
    for (int32 ChannelIndex = 0; ChannelIndex < 5; ++ChannelIndex)
    {
        ReadInitialChannel(PageIndex, ChannelIndex, Original);
        for (int32 Offset = 0; Offset < Page.Count; ++Offset)
            FMemory::Memcpy(Current + Offset,
                reinterpret_cast<const uint8*>(&Page.ExplicitCells[Offset]) + ChannelIndex * sizeof(uint32), sizeof(uint32));
        Page.Channels[ChannelIndex].Initialize(Current, Original, Page.Count);
    }
    Page.ExplicitCells.Empty();
    Page.AccountResize(Before);
}

void FMD_ScrapePagedState::ReleaseInitialPage(const int32 PageIndex)
{
    if (!Pages[PageIndex]->IsInitial()) return;
    Pages[PageIndex].Reset();
    --NonNullPageCount;
    if (NonNullPageCount == 0) Pages.Empty();
}

void FMD_ScrapePagedState::ResetToInitial()
{
    check(IsInGameThread());
    Pages.Empty();
    NonNullPageCount = 0;
}

void FMD_ScrapePagedState::CompactUnchangedPages()
{
    check(IsInGameThread());
    for (int32 PageIndex = Pages.Num() - 1; PageIndex >= 0 && !Pages.IsEmpty(); --PageIndex)
    {
        CompactUnchangedPageForIndex(PageIndex * PageSize);
    }
}

void FMD_ScrapePagedState::CompactUnchangedPageForIndex(const int32 Index)
{
    check(IsInGameThread());
    check(IsValidIndex(Index));
    if (Pages.IsEmpty())
    {
        return;
    }
    const int32 PageIndex = Index / PageSize;
    TSharedPtr<FPage, ESPMode::NotThreadSafe>& Page = Pages[PageIndex];
    if (!Page)
    {
        return;
    }
    if (!Page->ExplicitCells.IsEmpty())
    {
        EditPage(PageIndex);
        CompactExplicitPage(PageIndex);
    }
    ReleaseInitialPage(PageIndex);
}

SIZE_T FMD_ScrapePagedState::GetAllocatedSize() const
{
    SIZE_T Bytes = Pages.GetAllocatedSize();
    for (const TSharedPtr<FPage, ESPMode::NotThreadSafe>& Page : Pages)
    {
        if (Page && Page.IsUnique())
        {
            Bytes += Page->GetAllocatedSize();
        }
    }
    return Bytes;
}

SIZE_T FMD_ScrapePagedState::GetSharedPageAllocatedSize() const
{
    SIZE_T Bytes = 0;
    for (const TSharedPtr<FPage, ESPMode::NotThreadSafe>& Page : Pages)
    {
        if (Page && !Page.IsUnique())
        {
            Bytes += Page->GetAllocatedSize();
        }
    }
    return Bytes;
}

SIZE_T FMD_ScrapePagedState::GetSharedInitialAllocatedSize() const
{
    return Initial ? Initial->GetAllocatedSize() : 0;
}

FMD_ScrapePagedStateMemoryStats FMD_ScrapePagedState::GetGlobalMemoryStats()
{
    check(IsInGameThread());
    return MD_ScrapePagedStatePrivate::LiveStats;
}
