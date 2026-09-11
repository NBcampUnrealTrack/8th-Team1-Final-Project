#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "Gameplay/Scrape/MD_ScrapePagedState.h"
#include <type_traits>

/** Immutable addressing shared by walls; persistence and networking retain atlas IDs. */
struct FMD_ScrapeCellLayout
{
    TArray<int32> AtlasToActive;
    TArray<int32> ActiveToAtlas;
    SIZE_T GetAllocatedSize() const
    {
        return AtlasToActive.GetAllocatedSize() + ActiveToAtlas.GetAllocatedSize();
    }
};

/** Borrowed logical atlas view over either dense cells or active-only storage. No expansion. */
template <typename ElementType>
class TMD_ScrapeCellView
{
    template <typename> friend class TMD_ScrapeCellView;
    TArrayView<ElementType> Storage;
    using FPagedStateType = std::conditional_t<std::is_const_v<ElementType>, const FMD_ScrapePagedState, FMD_ScrapePagedState>;
    FPagedStateType* Paged = nullptr;
    const FMD_ScrapeCellLayout* Layout = nullptr;
    int32 Offset = 0;
    int32 Count = 0;

public:
    TMD_ScrapeCellView() = default;
    TMD_ScrapeCellView(ElementType* Data, const int32 Num) : Storage(Data, Num), Count(Num) {}
    template <typename OtherType, std::enable_if_t<std::is_convertible_v<OtherType*, ElementType*>, int> = 0>
    TMD_ScrapeCellView(TArrayView<OtherType> Data, const FMD_ScrapeCellLayout* InLayout = nullptr)
        : Storage(Data), Layout(InLayout), Count(InLayout ? InLayout->AtlasToActive.Num() : Data.Num())
    {
        check(!Layout || Storage.Num() == Layout->ActiveToAtlas.Num());
    }
    TMD_ScrapeCellView(FPagedStateType* Data, const FMD_ScrapeCellLayout* InLayout = nullptr)
        : Paged(Data), Layout(InLayout), Count(InLayout ? InLayout->AtlasToActive.Num() : (Data ? Data->Num() : 0))
    {
        check(Data || !Layout);
        check(!Layout || Data->Num() == Layout->ActiveToAtlas.Num());
    }
    template <typename OtherType, std::enable_if_t<std::is_convertible_v<OtherType*, ElementType*>, int> = 0>
    TMD_ScrapeCellView(TArray<OtherType>& Data) : TMD_ScrapeCellView(MakeArrayView(Data)) {}
    template <typename OtherType, std::enable_if_t<std::is_convertible_v<const OtherType*, ElementType*>, int> = 0>
    TMD_ScrapeCellView(const TArray<OtherType>& Data) : TMD_ScrapeCellView(MakeArrayView(Data)) {}
    template <typename OtherType, std::enable_if_t<std::is_convertible_v<OtherType*, ElementType*>, int> = 0>
    TMD_ScrapeCellView(const TMD_ScrapeCellView<OtherType>& Other)
        : Storage(Other.Storage), Paged(Other.Paged), Layout(Other.Layout), Offset(Other.Offset), Count(Other.Count) {}

    int32 Num() const { return Count; }
    bool IsEmpty() const { return Count == 0; }
    bool IsValidIndex(const int32 Index) const { return Index >= 0 && Index < Count; }
    bool IsStoredIndex(const int32 Index) const
    {
        return IsValidIndex(Index) && (!Layout || Layout->AtlasToActive[Index + Offset] != INDEX_NONE);
    }
    /** Read never detaches a COW page, including through a mutable view. */
    FMD_ScrapeSurfaceCell Read(const int32 Index) const
    {
        check(IsValidIndex(Index));
        const int32 Slot = Layout ? Layout->AtlasToActive[Index + Offset] : Index + Offset;
        if (Slot == INDEX_NONE)
        {
            static const FMD_ScrapeSurfaceCell EmptyCell = []
            {
                FMD_ScrapeSurfaceCell Cell;
                Cell.InitialContamination = Cell.RemainingContamination = 0.0f;
                return Cell;
            }();
            return EmptyCell;
        }
        if (Paged) return Paged->Read(Slot);
        FMD_ScrapeSurfaceCell Cell;
        FMemory::Memcpy(&Cell, &Storage[Slot], sizeof(Cell));
        return Cell;
    }
    template <typename T = ElementType, std::enable_if_t<!std::is_const_v<T>, int> = 0>
    FMD_ScrapeSurfaceCell& Edit(const int32 Index) const
    {
        check(IsValidIndex(Index));
        const int32 Slot = Layout ? Layout->AtlasToActive[Index + Offset] : Index + Offset;
        // Inactive pixels are display gutters, never authoritative mutable cells.
        check(Slot != INDEX_NONE);
        return Paged ? Paged->Edit(Slot) : Storage[Slot];
    }
    template <typename T = ElementType, std::enable_if_t<!std::is_const_v<T>, int> = 0>
    void Set(const int32 Index, const FMD_ScrapeSurfaceCell& Cell) const
    {
        check(IsValidIndex(Index));
        const int32 Slot = Layout ? Layout->AtlasToActive[Index + Offset] : Index + Offset;
        check(Slot != INDEX_NONE);
        if (Paged)
        {
            Paged->Set(Slot, Cell);
        }
        else
        {
            FMemory::Memcpy(&Storage[Slot], &Cell, sizeof(Cell));
        }
    }
    // Indexing is always a read. Only explicit Edit/Set may materialize a page.
    const FMD_ScrapeSurfaceCell operator[](const int32 Index) const { return Read(Index); }
    TMD_ScrapeCellView Slice(const int32 Start, const int32 Length) const
    {
        check(Start >= 0 && Length >= 0 && Start + Length <= Count);
        TMD_ScrapeCellView Result = *this;
        Result.Offset += Start;
        Result.Count = Length;
        return Result;
    }
    /** Only dense planar transactions may use contiguous pointer access. */
    ElementType* GetData() const { check(!Paged && !Layout); return Storage.GetData() + Offset; }
    struct FIterator
    {
        const TMD_ScrapeCellView* View;
        int32 Index;
        const FMD_ScrapeSurfaceCell operator*() const { return View->Read(Index); }
        FIterator& operator++() { ++Index; return *this; }
        bool operator!=(const FIterator& Other) const { return Index != Other.Index; }
    };
    FIterator begin() const { return {this, 0}; }
    FIterator end() const { return {this, Count}; }
};

using FMD_ScrapeCellView = TMD_ScrapeCellView<FMD_ScrapeSurfaceCell>;
using FMD_ScrapeConstCellView = TMD_ScrapeCellView<const FMD_ScrapeSurfaceCell>;
