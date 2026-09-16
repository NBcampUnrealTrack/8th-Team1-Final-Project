#include "Gameplay/Scrape/MD_ScrapeMaskPixels.h"
#include "Tasks/Task.h"
#include "RenderingThread.h"
#include "Misc/ScopeLock.h"
#include <atomic>
#include "Misc/Crc.h"
#include "Algo/BinarySearch.h"
#include "MDScrapeInitialMaskRenderer.h"
#include "RHICommandList.h"

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
        if (!Candidate || Candidate->Layout || Candidate->Hash != Hash || Candidate->Count != Count) continue;
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

uint8 FMD_ScrapeMaskPixels::FInitial::Read(int32 Index) const
{
    if (!Layout) return Values.IsEmpty() ? Uniform : Values[Index];
    const uint32 Source = Layout->Sources[Index];
    if (Source == MAX_uint32) return 0;
    if (Source & 0x80000000u) return SeamBytes[Source & 0x7fffffffu];
    return ((ActiveBits[Source >> 5u] >> (Source & 31u)) & 1u) ? 255 : 0;
}

void FMD_ScrapeMaskPixels::InitializeBinary(
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> Layout,
    TArray<uint32>&& Bits, TArray<uint8>&& Seam)
{
    check(IsInGameThread());
    check(Layout && !Layout->Sources.IsEmpty() && !Bits.IsEmpty());
    check(Seam.Num() == Layout->SeamPixels.Num());
    Empty();
    Count = Layout->Sources.Num();
    const uint32 Hash = HashCombineFast(FCrc::MemCrc32(Bits.GetData(), Bits.Num() * sizeof(uint32)),
        FCrc::MemCrc32(Seam.GetData(), Seam.Num()));
    PruneExpiredMetadata();
    for (const auto& Weak : InitialRegistry())
    {
        const auto Candidate = Weak.Pin();
        if (Candidate && Candidate->Hash == Hash && Candidate->Layout == Layout
            && Candidate->ActiveBits == Bits && Candidate->SeamBytes == Seam)
        {
            Initial = Candidate; Bits.Empty(); Seam.Empty(); return;
        }
    }
    auto NewInitial = MakeShared<FInitial, ESPMode::ThreadSafe>();
    NewInitial->Hash = Hash; NewInitial->Count = Count;
    NewInitial->Layout = MoveTemp(Layout);
    NewInitial->ActiveBits = MoveTemp(Bits); NewInitial->SeamBytes = MoveTemp(Seam);
    Initial = NewInitial;
    InitialRegistry().Add(Initial);
}
bool FMD_ScrapeMaskPixels::HasBinaryInitial() const { return Initial && Initial->Layout; }
TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> FMD_ScrapeMaskPixels::GetBinaryInitialLayout() const
{
    return HasBinaryInitial() ? Initial->Layout : nullptr;
}
uint64 FMD_ScrapeMaskPixels::GetBinaryUploadBound(int32 Width) const
{
    if (!HasBinaryInitial()) return 0;
    check(Width > 0);
    uint64 Bytes = Align(uint64(Initial->ActiveBits.Num()) * sizeof(uint32), uint64(512))
        + Align(FMath::Max(uint64(4), uint64(Initial->SeamBytes.Num())), uint64(512));
    for (int32 Page = 0; Page < Pages.Num(); ++Page)
    {
        if (!Pages[Page]) continue;
        int32 Start = Page * PageSize;
        const int32 End = FMath::Min(Start + PageSize, Count);
        while (Start < End)
        {
            const int32 Run = FMath::Min(End - Start, Width - Start % Width);
            Bytes += Align(Align(uint64(Run), uint64(256)), uint64(512));
            Start += Run;
        }
    }
    return Bytes;
}
bool FMD_ScrapeMaskPixels::TryInitializeGPU(FRHICommandListImmediate& Cmd, FRHITexture* Texture, FIntPoint Size) const
{
    if (!HasBinaryInitial() || !FMDScrapeInitialMaskRenderer::Generate(Cmd, Texture, Size,
        *Initial->Layout, Initial->ActiveBits, Initial->SeamBytes)) return false;
    // Resource recreation reuses the GPU baseline and uploads only actual modified
    // pages. No readback or whole-atlas CPU raster is deferred to the first scrape.
    uint8 Bytes[PageSize];
    for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
    {
        if (!Pages[PageIndex]) continue;
        int32 Start = PageIndex * PageSize;
        const int32 Length = FMath::Min(PageSize, Count - Start);
        CopyRange(Start, Length, Bytes);
        for (int32 Offset = 0; Offset < Length;)
        {
            const int32 X = (Start + Offset) % Size.X;
            const int32 Y = (Start + Offset) / Size.X;
            const int32 Run = FMath::Min(Length - Offset, Size.X - X);
            Cmd.UpdateTexture2D(Texture, 0, FUpdateTextureRegion2D(X, Y, 0, 0, Run, 1), Run, Bytes + Offset);
            Offset += Run;
        }
    }
    return true;
}

void FMD_ScrapeMaskPixels::InitializeUniform(int32 InCount, uint8 Value)
{
    check(IsInGameThread());
    check(InCount >= 0);
    Empty();
    if (!InCount) return;
    PruneExpiredMetadata();
    for (const auto& Weak : InitialRegistry())
    {
        const auto Candidate = Weak.Pin();
        if (Candidate && Candidate->Count == InCount && Candidate->IsUniform()
            && Candidate->Uniform == Value)
        {
            Initial = Candidate;
            Count = InCount;
            return;
        }
    }
    TArray<uint8> Values;
    Values.Init(Value, InCount);
    *this = MoveTemp(Values);
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
    return Initial->Read(Index);
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
        if (Initial->IsUniform())
        {
            NewPage->Sum = uint32(Initial->Uniform) * Length;
            NewPage->SumSquares = uint32(Initial->Uniform) * Initial->Uniform * Length;
        }
        else for (int32 I = Start; I < Start + Length; ++I)
        {
            const uint8 V = Initial->Read(I);
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
    const uint8 OriginalValue = Initial->Read(Index);
    Page->OriginalDeviations += int32(Value != OriginalValue) - int32(OldValue != OriginalValue);
    if (Page->OriginalDeviations == 0)
    {
        Page.Reset();
        if (--LivePageCount == 0) Pages.Empty();
        return true; // Dirty flags still restore these pixels on the GPU.
    }
    const auto BaseAt = [&](int32 LocalIndex)
    {
        return Page->bUniformBase ? Page->Uniform : (Initial->Read(Start + LocalIndex));
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

namespace
{
FCriticalSection PageReleaseMutex;
UE::Tasks::FTask PageReleaseTasks[FMD_ScrapeMaskPixels::MaximumDeferredPageBatches];
UE::Tasks::FTask LastPageReleaseTask;
bool bPageReleasesEnabled = true; // PageReleaseMutex; disabled before module unload.
std::atomic<uint64> RetiringSlots{0};
}

void FMD_ScrapeMaskPixels::FlushDeferredPageReleases()
{
    TArray<UE::Tasks::FTask, TInlineAllocator<MaximumDeferredPageBatches>> Tasks;
    { FScopeLock Lock(&PageReleaseMutex); for (const auto& Task : PageReleaseTasks) Tasks.Add(Task); }
    for (const auto& Task : Tasks) Task.Wait();
}
void FMD_ScrapeMaskPixels::ShutdownDeferredPageReleases()
{
    { FScopeLock Lock(&PageReleaseMutex); bPageReleasesEnabled = false; }
    FlushDeferredPageReleases();
}
void FMD_ScrapeMaskPixels::EmptyWithDeferredPageRelease()
{
    // UTexture GC releases large arrays of shared page pointers on the render thread.
    // Only CPU-only FPage data escapes; no UObject, RHI reference, or this pointer.
    const uint64 Slots = Pages.Num();
    if (IsInRenderingThread() && Slots > 0 && Slots <= MaximumDeferredPageSlots)
    {
        FScopeLock Lock(&PageReleaseMutex);
        if (bPageReleasesEnabled && RetiringSlots.load() <= MaximumDeferredPageSlots - Slots)
        {
            for (auto& Task : PageReleaseTasks)
            {
                if (!Task.IsCompleted()) continue;
                RetiringSlots.fetch_add(Slots);
                Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
                    [RetiredPages = MoveTemp(Pages), Slots]() mutable
                    {
                        RetiredPages.Empty();
                        RetiringSlots.fetch_sub(Slots);
                    }, UE::Tasks::Prerequisites(LastPageReleaseTask), UE::Tasks::ETaskPriority::BackgroundNormal);
                LastPageReleaseTask = Task;
                break;
            }
        }
    }
    // At capacity or during shutdown, synchronous ownership release bounds memory.
    Empty();
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
    return Initial ? Initial->GetPayloadBytes() : 0;
}
bool FMD_ScrapeMaskPixels::operator==(const TArray<uint8>& Other) const
{
    if (Count != Other.Num()) return false;
    for (int32 I = 0; I < Count; ++I) if ((*this)[I] != Other[I]) return false;
    return true;
}
void FMD_ScrapeMaskPixels::CopyRange(int32 Start, int32 Length, uint8* Out) const
{
    check(Start >= 0 && Length >= 0 && Start <= Count && Length <= Count - Start);
    if (!Length) return;
    check(Out);
    if (Initial->Layout)
    {
        for (int32 I = 0; I < Length; ++I) Out[I] = Initial->Read(Start + I);
    }
    else if (Initial->IsUniform()) FMemory::Memset(Out, Initial->Uniform, Length);
    else FMemory::Memcpy(Out, Initial->Values.GetData() + Start, Length);
    if (Pages.IsEmpty()) return;
    const int32 End = Start + Length;
    for (int32 PageIndex = Start / PageSize; PageIndex <= (End - 1) / PageSize; ++PageIndex)
    {
        const auto& Page = Pages[PageIndex];
        if (!Page) continue;
        const int32 PageStart = PageIndex * PageSize;
        const int32 First = FMath::Max(Start, PageStart);
        const int32 Last = FMath::Min(End, PageStart + PageSize);
        uint8* Destination = Out + First - Start;
        if (Page->bDense)
        {
            FMemory::Memcpy(Destination, Page->Values.GetData() + First - PageStart, Last - First);
            continue;
        }
        if (Page->bUniformBase) FMemory::Memset(Destination, Page->Uniform, Last - First);
        for (int32 Slot = Algo::LowerBound(Page->Offsets, uint16(First - PageStart));
            Slot < Page->Offsets.Num() && PageStart + Page->Offsets[Slot] < Last; ++Slot)
        {
            Out[PageStart + Page->Offsets[Slot] - Start] = Page->Values[Slot];
        }
    }
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
    TSet<const FMDScrapeInitialMaskLayout*> Layouts;
    auto& Initials = InitialRegistry();
    auto& PageItems = PageRegistry();
    for (int32 I = Initials.Num() - 1; I >= 0; --I)
    {
        if (auto Item = Initials[I].Pin())
        {
            ++Result.InitialCount;
            Result.InitialBytes += Item->GetPayloadBytes();
            if (Item->Layout && !Layouts.Contains(Item->Layout.Get()))
            {
                Layouts.Add(Item->Layout.Get());
                Result.InitialBytes += Item->Layout->GetCPUBytes();
            }
        }
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
