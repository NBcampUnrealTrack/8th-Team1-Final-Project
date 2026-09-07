#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"

/** Unique live allocations, excluding allocator/control-block and weak-cache metadata. */
struct FMD_ScrapePagedStateMemoryStats
{
    uint64 LiveInitialBytes = 0;
    uint64 LiveInitialTemplates = 0;
    uint64 LivePageBytes = 0;
    uint64 LivePages = 0;
};

/**
 * Game-thread-only cell state. Copies share immutable initial values and modified pages;
 * writes detach shared pages. Each field independently uses original, uniform, sparse,
 * or dense exact bits; scattered edits and completed cleaning do not require dense pages.
 * A pristine state allocates no page table.
 *
 * Read/GetInitialCell return values so canonical initial amounts can occupy one float
 * per cell without mutable reconstruction scratch. Other initial content stays exact.
 * An Edit reference lasts until the next write, compaction, reset or state copy.
 * Subsequent writes must go through Edit/Set again to preserve copy-on-write isolation.
 */
class MARINEDEMOLITION_API FMD_ScrapePagedState
{
public:
    static constexpr int32 PageSize = 256;

    FMD_ScrapePagedState() = default;
    FMD_ScrapePagedState(const FMD_ScrapePagedState&) = default;
    FMD_ScrapePagedState& operator=(const FMD_ScrapePagedState& Other);
    FMD_ScrapePagedState(FMD_ScrapePagedState&& Other) noexcept;
    FMD_ScrapePagedState& operator=(FMD_ScrapePagedState&& Other) noexcept;

    void InitializeUniform(int32 Count, const FMD_ScrapeSurfaceCell& Cell);
    void InitializeFromCells(TArray<FMD_ScrapeSurfaceCell>&& Cells);
    void Reset();
    int32 Num() const { return CellCount; }
    bool IsEmpty() const { return CellCount == 0; }
    bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < CellCount; }

    FMD_ScrapeSurfaceCell Read(const int32 Index) const
    {
        check(IsValidIndex(Index));
        if (!Pages.IsEmpty())
        {
            const TSharedPtr<FPage, ESPMode::NotThreadSafe>& Page = Pages[Index / PageSize];
            if (Page)
            {
                return Page->Read(Index % PageSize, GetInitialCell(Index));
            }
        }
        return GetInitialCell(Index);
    }

    /** Explicit dense-reference fallback. Prefer Set; transaction compaction recompresses it. */
    FMD_ScrapeSurfaceCell& Edit(int32 Index);
    /** Exact five-float bit comparison: identical writes do not allocate or detach. */
    void Set(int32 Index, const FMD_ScrapeSurfaceCell& Cell);
    FMD_ScrapeSurfaceCell GetInitialCell(const int32 Index) const
    {
        check(IsValidIndex(Index));
        FMD_ScrapeSurfaceCell Cell;
        if (InitialAmounts)
        {
            FMemory::Memcpy(&Cell.InitialContamination, InitialAmounts + Index, sizeof(float));
            FMemory::Memcpy(&Cell.RemainingContamination, InitialAmounts + Index, sizeof(float));
        }
        else
        {
            FMemory::Memcpy(&Cell, InitialCells + (bUniformInitial ? 0 : Index), sizeof(Cell));
        }
        return Cell;
    }
    void ResetToInitial();
    /** Recompress explicit Edit pages and reclaim restored pages after a transaction. */
    void CompactUnchangedPages();
    /** Bounded rollback cleanup: compare only the page containing this storage index. */
    void CompactUnchangedPageForIndex(int32 Index);

    /** This instance's table and exclusively owned page payload; excludes shared pages. */
    SIZE_T GetAllocatedSize() const;
    /** Referenced shared modified-page payload. Do not sum this over state snapshots. */
    SIZE_T GetSharedPageAllocatedSize() const;
    /** Shared initial object and dense payload. Deduplicate using the identity below. */
    SIZE_T GetSharedInitialAllocatedSize() const;
    const void* GetSharedInitialIdentity() const { return Initial.Get(); }
    /** Number of modified page slots, including pages shared with another snapshot. */
    int32 GetOwnedPageCount() const { return NonNullPageCount; }
    static FMD_ScrapePagedStateMemoryStats GetGlobalMemoryStats();
    static bool AreCellsBitIdentical(const FMD_ScrapeSurfaceCell& A, const FMD_ScrapeSurfaceCell& B);

private:
    struct FInitial;
    struct FChannel
    {
        enum class EMode : uint8 { Initial, Uniform, SparseInitial, SparseUniform, Dense };
        // Sparse entries are sorted, bounded at 64; dense values use no index array.
        TArray<uint32> Values;
        TArray<uint8> Indices;
        uint64 SumBits = 0;
        uint32 UniformBits = 0;
        uint16 InitialMatches = 0;
        EMode Mode = EMode::Initial;

        uint32 ReadBits(int32 Offset, uint32 InitialBits) const;
        void SetBits(int32 Offset, uint32 Bits, uint32 InitialBits, const FMD_ScrapePagedState& Owner, int32 PageIndex, int32 ChannelIndex, int32 Count);
        void Initialize(const uint32* Current, const uint32* Original, int32 Count);
        SIZE_T GetAllocatedSize() const { return Values.GetAllocatedSize() + Indices.GetAllocatedSize(); }
        void Reset(int32 Count);
    };
    struct FPage
    {
        FChannel Channels[5];
        TArray<FMD_ScrapeSurfaceCell> ExplicitCells;
        int32 Count;
        explicit FPage(int32 InCount);
        FPage(const FPage& Other);
        ~FPage();
        FMD_ScrapeSurfaceCell Read(int32 Offset, const FMD_ScrapeSurfaceCell& InitialCell) const;
        SIZE_T GetAllocatedSize() const;
        void AccountResize(SIZE_T PreviousBytes) const;
        bool IsInitial() const;
    };
    static TSharedPtr<const FInitial, ESPMode::NotThreadSafe> FindOrAddInitial(
        TArray<FMD_ScrapeSurfaceCell>&& Cells, const FMD_ScrapeSurfaceCell& UniformCell,
        int32 Count, bool bUniform, bool bCanonicalAmounts);
    void BindInitial(TSharedPtr<const FInitial, ESPMode::NotThreadSafe> InInitial);
    FPage& EditPage(int32 PageIndex);
    void CompactExplicitPage(int32 PageIndex);
    void ReleaseInitialPage(int32 PageIndex);
    void ReadInitialChannel(int32 PageIndex, int32 ChannelIndex, uint32* OutBits) const;

    TSharedPtr<const FInitial, ESPMode::NotThreadSafe> Initial;
    TArray<TSharedPtr<FPage, ESPMode::NotThreadSafe>> Pages;
    const FMD_ScrapeSurfaceCell* InitialCells = nullptr;
    const float* InitialAmounts = nullptr;
    int32 CellCount = 0;
    int32 NonNullPageCount = 0;
    bool bUniformInitial = true;
};
