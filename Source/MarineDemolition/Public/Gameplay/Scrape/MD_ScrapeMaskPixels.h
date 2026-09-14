#pragma once
#include "CoreMinimal.h"
class FMDScrapeInitialMaskLayout;
class FRHICommandListImmediate;
class FRHITexture;

struct FMD_ScrapeMaskPixelMemoryStats
{
    uint64 InitialBytes = 0, InitialCount = 0, PageBytes = 0, PageCount = 0, RegistryBytes = 0;
};

/** Exact G8 data: immutable content-interned baseline plus copy-on-write byte pages.
 * The all-touched bound is one byte per pixel plus page metadata, not a hash entry
 * per pixel. Uniform initial and uniform changed pages retain only one byte.
 */
class MARINEDEMOLITION_API FMD_ScrapeMaskPixels
{
public:
    static constexpr int32 PageSize = 1024;
    FMD_ScrapeMaskPixels() = default;
    FMD_ScrapeMaskPixels(const FMD_ScrapeMaskPixels&) = default;
    FMD_ScrapeMaskPixels& operator=(const FMD_ScrapeMaskPixels& Other);
    FMD_ScrapeMaskPixels(FMD_ScrapeMaskPixels&& Other) noexcept;
    FMD_ScrapeMaskPixels& operator=(FMD_ScrapeMaskPixels&& Other) noexcept;
    FMD_ScrapeMaskPixels& operator=(TArray<uint8>&& Values);
    /** Intern a constant baseline without rebuilding a dense array on cache hits. */
    void InitializeUniform(int32 InCount, uint8 Value);
    void InitializeBinary(TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> Layout,
        TArray<uint32>&& ActiveBits, TArray<uint8>&& SeamBytes);
    bool HasBinaryInitial() const;
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> GetBinaryInitialLayout() const;
    /** Per-mask GPU input plus resident changed-page staging; shared layout is reserved separately. */
    uint64 GetBinaryUploadBound(int32 Width) const;
    bool TryInitializeGPU(FRHICommandListImmediate& Cmd, FRHITexture* Texture, FIntPoint Size) const;
    int32 Num() const { return Count; }
    bool IsEmpty() const { return Count == 0; }
    bool IsValidIndex(int32 Index) const { return Index >= 0 && Index < Count; }
    uint8 operator[](int32 Index) const;
    bool Set(int32 Index, uint8 Value);
    void Empty();
    void Reset() { Empty(); }
    SIZE_T GetAllocatedSize() const;
    SIZE_T GetSharedInitialAllocatedSize() const;
    static FMD_ScrapeMaskPixelMemoryStats GetGlobalMemoryStats();
    /** Bounded weak-entry reclamation, called by the runtime mask ticker. */
    static bool PruneExpiredMetadata(int32 WorkBudget = 64);
    const void* GetInitialIdentity() const { return Initial.Get(); }
    bool IsPristine() const { return Pages.IsEmpty(); }
    bool operator==(const TArray<uint8>& Other) const;
    void CopyRange(int32 Start, int32 Length, uint8* Out) const;
    const TBitArray<FDefaultAllocator>& GetDirtyPages() const { return DirtyPages; }
    void ClearDirty() { DirtyPages.Empty(); }
private:
    struct FInitial
    {
        uint32 Hash = 0;
        int32 Count = 0;
        uint8 Uniform = 0;
        TArray<uint8> Values;
        TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> Layout;
        TArray<uint32> ActiveBits;
        TArray<uint8> SeamBytes;
        bool IsUniform() const { return !Layout && Values.IsEmpty(); }
        uint8 Read(int32 Index) const;
        SIZE_T GetPayloadBytes() const
        { return sizeof(*this) + Values.GetAllocatedSize() + ActiveBits.GetAllocatedSize() + SeamBytes.GetAllocatedSize(); }
    };
    struct FPage
    {
        uint8 Uniform = 0;
        bool bUniformBase = false;
        bool bDense = false;
        uint32 Sum = 0, SumSquares = 0;
        int32 Deviations = 0, OriginalDeviations = 0;
        TArray<uint16> Offsets;
        TArray<uint8> Values;
    };
    TSharedPtr<const FInitial, ESPMode::ThreadSafe> Initial;
    TArray<TSharedPtr<FPage, ESPMode::ThreadSafe>> Pages;
    // Heap-only capacity keeps pristine wrappers allocation-free and memory counters exact.
    TBitArray<FDefaultAllocator> DirtyPages;
    int32 Count = 0, LivePageCount = 0;
    static TArray<TWeakPtr<const FInitial, ESPMode::ThreadSafe>>& InitialRegistry();
    static TArray<TWeakPtr<FPage, ESPMode::ThreadSafe>>& PageRegistry();
    static void RegisterPage(const TSharedPtr<FPage, ESPMode::ThreadSafe>& Page);
};
