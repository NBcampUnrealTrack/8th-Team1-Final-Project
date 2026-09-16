#include "Gameplay/Scrape/MD_ScrapeRuntimeMaskTexture.h"
#include "MDScrapeInitialMaskRenderer.h"
#include "TextureResource.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "Misc/ScopeLock.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Containers/Ticker.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogMDScrapeMaskUpload, Log, All);
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_InitialPendingBytes, TEXT("MD/ScrapeMask/InitialPendingBytes"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_DirtyPendingBytes, TEXT("MD/ScrapeMask/DirtyPendingBytes"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_InitialPeakBytes, TEXT("MD/ScrapeMask/InitialPeakBytes"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_DirtyPeakBytes, TEXT("MD/ScrapeMask/DirtyPeakBytes"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_InFlightUploads, TEXT("MD/ScrapeMask/InFlightUploads"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_QueuedDirtyStates, TEXT("MD/ScrapeMask/QueuedDirtyStates"));
TRACE_DECLARE_INT_COUNTER(MD_ScrapeMask_InitialAdmissionFailures, TEXT("MD/ScrapeMask/InitialAdmissionFailures"));

namespace
{
std::atomic<uint64> QueuedDirtyStates{0};
struct FMaskUploadState
{
    FCriticalSection Mutex;
    bool bHasDirtyWork = false; // Protected by Mutex; also read after final owner release.
    ~FMaskUploadState()
    {
        if (bHasDirtyWork) QueuedDirtyStates.fetch_sub(1);
        CurrentPixels.EmptyWithDeferredPageRelease();
        PendingPixels.EmptyWithDeferredPageRelease();
    }
    FMD_ScrapeMaskPixels CurrentPixels; // Exact latest data, including resource recreation.
    FMD_ScrapeMaskPixels PendingPixels;
    TBitArray<> Dirty;
    FTextureResource* ResourceIdentity = nullptr; // Compared on GT, never dereferenced here.
    FTextureRHIRef Texture; // RT only.
    bool bSharedInitial = false;
    FIntPoint Size = FIntPoint::ZeroValue;
    FGPUFenceRHIRef Fence; // RT only.
};
using FStatePtr = TSharedPtr<FMaskUploadState, ESPMode::ThreadSafe>;
TMap<TWeakObjectPtr<const UTexture2D>, TWeakPtr<FMaskUploadState, ESPMode::ThreadSafe>> TextureStates;

using FUploadStatePtr = FStatePtr;
std::atomic<uint64> InitialPending{0}, DirtyPending{0}, InitialPeak{0}, DirtyPeak{0}, FenceCount{0}, AdmissionFailures{0};
std::atomic<bool> PumpQueued{false};
FTSTicker::FDelegateHandle UploadTicker;
struct FLayoutUploadReservation
{
    TSharedPtr<const FMDScrapeInitialMaskLayout, ESPMode::ThreadSafe> Layout;
    uint64 Bytes = 0;
    ~FLayoutUploadReservation() { InitialPending.fetch_sub(Bytes); }
};
using FLayoutReservationPtr = TSharedPtr<FLayoutUploadReservation, ESPMode::ThreadSafe>;
FCriticalSection LayoutReservationsMutex;
TMap<const FMDScrapeInitialMaskLayout*, TWeakPtr<FLayoutUploadReservation, ESPMode::ThreadSafe>> LayoutReservations;
struct FPendingFence
{
    FGPUFenceRHIRef Fence;
    uint64 Bytes = 0;
    bool bInitial = false;
    FLayoutReservationPtr LayoutReservation;
};
TArray<FPendingFence> PendingFences; // RT only; survives texture destruction.
int32 NextUploadIndex = 0; // RT round-robin admission avoids starving older walls.

void TraceUploadStats()
{
    // Trace-counter objects are written only on GT; render/RHI work updates atomics.
    // Always emit samples so a trace begun after startup still has explicit zeroes.
    check(IsInGameThread());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_InitialPendingBytes, InitialPending.load());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_DirtyPendingBytes, DirtyPending.load());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_InitialPeakBytes, InitialPeak.load());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_DirtyPeakBytes, DirtyPeak.load());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_InFlightUploads, FenceCount.load());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_QueuedDirtyStates, QueuedDirtyStates.load());
    TRACE_COUNTER_SET_ALWAYS(MD_ScrapeMask_InitialAdmissionFailures, AdmissionFailures.load());
}

FStatePtr FindState(const UTexture2D* Texture)
{
    check(IsInGameThread());
    if (!IsValid(Texture)) return nullptr;
    const auto* Weak = TextureStates.Find(Texture);
    auto State = Weak ? Weak->Pin() : nullptr;
    return State && Texture->GetResource() == State->ResourceIdentity ? State : nullptr;
}
void PruneStates()
{
    check(IsInGameThread());
    for (auto It = TextureStates.CreateIterator(); It; ++It)
        if (!It.Key().IsValid() || !It.Value().IsValid()) It.RemoveCurrent();
    if (TextureStates.IsEmpty()) TextureStates.Empty();
}
void RecordPeak(std::atomic<uint64>& Peak, uint64 Value)
{
    uint64 Previous = Peak.load();
    while (Value > Previous && !Peak.compare_exchange_weak(Previous, Value)) {}
}
uint64 AlignedUploadBytes(uint64 Width, uint64 Height)
{
    // D3D12 source pitch alignment is 256; placement alignment is 512.
    return Align(Align(Width, uint64(256)) * Height, uint64(512));
}
bool ReserveInitial(uint64 Bytes)
{
    if (Bytes > FMD_ScrapeRuntimeMaskTexture::MaximumInitialUploadBytes) return false;
    const uint64 Before = InitialPending.fetch_add(Bytes);
    if (Before > FMD_ScrapeRuntimeMaskTexture::MaximumInitialUploadBytes - Bytes)
    { InitialPending.fetch_sub(Bytes); return false; }
    RecordPeak(InitialPeak, Before + Bytes);
    return true;
}
bool AcquireLayoutReservation(const FMD_ScrapeMaskPixels& Pixels, FLayoutReservationPtr& Out)
{
    const auto Layout = Pixels.GetBinaryInitialLayout();
    if (!Layout) return true;
    FScopeLock Lock(&LayoutReservationsMutex);
    for (auto It = LayoutReservations.CreateIterator(); It; ++It)
        if (!It.Value().IsValid()) It.RemoveCurrent();
    if (auto* Existing = LayoutReservations.Find(Layout.Get()))
        if (auto Shared = Existing->Pin()) { Out = MoveTemp(Shared); return true; }
    const uint64 Bytes = Align(uint64(Layout->Sources.Num()) * sizeof(uint32), uint64(512));
    if (!ReserveInitial(Bytes)) return false;
    Out = MakeShared<FLayoutUploadReservation, ESPMode::ThreadSafe>();
    Out->Layout = Layout; Out->Bytes = Bytes;
    LayoutReservations.Add(Layout.Get(), Out);
    return true;
}
void ReapFences()
{
    check(IsInRenderingThread());
    for (int32 I = PendingFences.Num() - 1; I >= 0; --I)
    {
        if (!PendingFences[I].Fence->Poll()) continue;
        const auto& Pending = PendingFences[I];
        (Pending.bInitial ? InitialPending : DirtyPending).fetch_sub(Pending.Bytes);
        FenceCount.fetch_sub(1);
        PendingFences.RemoveAtSwap(I);
    }
    if (PendingFences.IsEmpty())
    {
        PendingFences.Empty();
        FScopeLock Lock(&LayoutReservationsMutex);
        for (auto It = LayoutReservations.CreateIterator(); It; ++It)
            if (!It.Value().IsValid()) It.RemoveCurrent();
        if (LayoutReservations.IsEmpty()) LayoutReservations.Empty();
    }
}
void TrackFence(FRHICommandListImmediate& Cmd, const FUploadStatePtr& State, uint64 Bytes, bool bInitial,
    FLayoutReservationPtr LayoutReservation = nullptr)
{
    State->Fence = RHICreateGPUFence(TEXT("MD_ScrapeMaskUpload"));
    Cmd.WriteGPUFence(State->Fence);
    PendingFences.Add({State->Fence, Bytes, bInitial, MoveTemp(LayoutReservation)});
    FenceCount.fetch_add(1);
}
uint64 PageUploadBytes(int32 Page, int32 PixelCount, int32 Width)
{
    int32 Start = Page * FMD_ScrapeMaskPixels::PageSize;
    const int32 End = FMath::Min(Start + FMD_ScrapeMaskPixels::PageSize, PixelCount);
    uint64 Bytes = 0;
    while (Start < End)
    {
        const int32 Length = FMath::Min(End - Start, Width - Start % Width);
        Bytes += AlignedUploadBytes(Length, 1);
        Start += Length;
    }
    return Bytes;
}
void PumpUploads(FRHICommandListImmediate& Cmd, const TArray<FUploadStatePtr>& States)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_PumpFencedMaskUploads);
    ReapFences();
    const int32 Count = States.Num();
    const int32 StartIndex = Count ? NextUploadIndex % Count : 0;
    for (int32 Offset = 0; Offset < Count; ++Offset)
    {
        const int32 StateIndex = (StartIndex + Offset) % Count;
        const auto& State = States[StateIndex];
        if (!State->Texture || (State->Fence && !State->Fence->Poll())) continue;
        FMD_ScrapeMaskPixels Latest;
        TArray<int32> Pages;
        uint64 Bytes = 0;
        const uint64 Available = FMD_ScrapeRuntimeMaskTexture::MaximumDirtyUploadBytes - DirtyPending.load();
        {
            FScopeLock Lock(&State->Mutex);
            for (TConstSetBitIterator<> It(State->Dirty); It; ++It)
            {
                const uint64 PageBytes = PageUploadBytes(It.GetIndex(), State->PendingPixels.Num(), State->Size.X);
                if (PageBytes > Available - Bytes) break;
                Pages.Add(It.GetIndex()); Bytes += PageBytes;
            }
            if (Pages.IsEmpty()) continue;
            Latest = State->PendingPixels;
            for (int32 Page : Pages) State->Dirty[Page] = false;
            if (State->Dirty.Find(true) == INDEX_NONE)
            {
                State->Dirty.Empty(); State->PendingPixels.Empty();
                if (State->bHasDirtyWork)
                { State->bHasDirtyWork = false; QueuedDirtyStates.fetch_sub(1); }
            }
        }
        const uint64 Before = DirtyPending.fetch_add(Bytes);
        RecordPeak(DirtyPeak, Before + Bytes);
        uint8 Row[FMD_ScrapeMaskPixels::PageSize];
        for (int32 Page : Pages)
        {
            int32 Start = Page * FMD_ScrapeMaskPixels::PageSize;
            const int32 End = FMath::Min(Start + FMD_ScrapeMaskPixels::PageSize, Latest.Num());
            while (Start < End)
            {
                const int32 X = Start % State->Size.X, Y = Start / State->Size.X;
                const int32 Length = FMath::Min(End - Start, State->Size.X - X);
                Latest.CopyRange(Start, Length, Row);
                Cmd.UpdateTexture2D(State->Texture, 0, FUpdateTextureRegion2D(X, Y, 0, 0, Length, 1), Length, Row);
                Start += Length;
            }
        }
        TrackFence(Cmd, State, Bytes, false);
        NextUploadIndex = (StateIndex + 1) % Count;
    }
}
TArray<FUploadStatePtr> CollectStates()
{
    check(IsInGameThread());
    PruneStates();
    TArray<FUploadStatePtr> Active;
    for (const auto& Pair : TextureStates)
    {
        const auto State = Pair.Value.Pin();
        if (State) Active.Add(State);
    }
    return Active;
}
void EnsureUploadTicker()
{
    check(IsInGameThread());
    if (UploadTicker.IsValid()) return;
    UploadTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
    {
        const bool bHasMetadata = FMD_ScrapeMaskPixels::PruneExpiredMetadata(256);
        TraceUploadStats();
        if (PumpQueued.exchange(true)) return true;
        auto States = CollectStates();
        if (QueuedDirtyStates.load() == 0 && InitialPending.load() == 0
            && DirtyPending.load() == 0 && FenceCount.load() == 0)
        {
            PumpQueued.store(false);
            if (States.IsEmpty() && !bHasMetadata) { UploadTicker.Reset(); return false; }
            return true; // Idle masks and metadata cleanup require no RT pump or readiness wait.
        }
        ENQUEUE_RENDER_COMMAND(MD_ScrapeMaskUploadPump)([States = MoveTemp(States)](FRHICommandListImmediate& Cmd)
        {
            PumpUploads(Cmd, States); PumpQueued.store(false);
        });
        return true;
    }));
}

class FRuntimeMaskResource final : public FTextureResource
{
public:
    FRuntimeMaskResource(FIntPoint InSize, TextureFilter InFilter, FStatePtr InState, FStatePtr InSource,
        uint64 InReservation, FLayoutReservationPtr InLayoutReservation)
        : Size(InSize), Filter(InFilter), State(MoveTemp(InState)), Source(MoveTemp(InSource)),
          Reservation(InReservation), LayoutReservation(MoveTemp(InLayoutReservation)) {}
    virtual ~FRuntimeMaskResource() override
    { if (Reservation) InitialPending.fetch_sub(Reservation); }
    virtual uint32 GetSizeX() const override { return Size.X; }
    virtual uint32 GetSizeY() const override { return Size.Y; }
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_MaskResourceInit);
        FMD_ScrapeMaskPixels Latest;
        {
            FScopeLock Lock(&State->Mutex);
            Latest = State->CurrentPixels;
        }
        // RHI recreation also reconstructs exact latest bytes and requires staging admission.
        if (!Reservation)
        {
            const uint64 Bytes = AlignedUploadBytes(Size.X, Size.Y) + Latest.GetBinaryUploadBound(Size.X);
            if (!ReserveInitial(Bytes))
            {
                AdmissionFailures.fetch_add(1);
                UE_LOG(LogMDScrapeMaskUpload, Error, TEXT("Mask RHI recreation exceeded the fixed startup staging budget."));
                return;
            }
            Reservation = Bytes;
            if (!AcquireLayoutReservation(Latest, LayoutReservation))
            {
                InitialPending.fetch_sub(Reservation); Reservation = 0;
                AdmissionFailures.fetch_add(1);
                UE_LOG(LogMDScrapeMaskUpload, Error, TEXT("Mask layout recreation exceeded the fixed startup staging budget."));
                return;
            }
        }
        TextureRHI = RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(
            TEXT("MD_ScrapeRuntimeMask"), Size.X, Size.Y, PF_G8)
            .SetFlags(ETextureCreateFlags::ShaderResource
                | (Latest.HasBinaryInitial() ? ETextureCreateFlags::UAV : ETextureCreateFlags::None))
            .SetInitialState(ERHIAccess::SRVMask));
        SamplerStateRHI = GetOrCreateSamplerState(FSamplerStateInitializerRHI(
            Filter == TF_Nearest ? SF_Point : SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp));
        const bool bCopyFromSource = Source && Source->Texture;
        if (bCopyFromSource)
        {
            auto& Cmd = FRHICommandListImmediate::Get();
            Cmd.Transition(FRHITransitionInfo(Source->Texture, ERHIAccess::SRVMask, ERHIAccess::CopySrc));
            Cmd.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::SRVMask, ERHIAccess::CopyDest));
            Cmd.CopyTexture(Source->Texture, TextureRHI, FRHICopyTextureInfo());
            Cmd.Transition(FRHITransitionInfo(Source->Texture, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
            Cmd.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
        }
        else
        {
            if (!Latest.TryInitializeGPU(FRHICommandListImmediate::Get(), TextureRHI, Size))
            {
                TArray<uint8> InitialBytes;
                InitialBytes.SetNumUninitialized(Size.X * Size.Y);
                Latest.CopyRange(0, InitialBytes.Num(), InitialBytes.GetData());
                RHICmdList.UpdateTexture2D(TextureRHI, 0,
                    FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y), Size.X, InitialBytes.GetData());
            }
        }
        State->Texture = TextureRHI;
        TrackFence(FRHICommandListImmediate::Get(), State, bCopyFromSource ? 0 : Reservation, true, MoveTemp(LayoutReservation));
        // A source copy consumes no upload staging, but keep its initialization
        // reservation visible until the completion fence is tracked.
        if (bCopyFromSource) InitialPending.fetch_sub(Reservation);
        Reservation = 0;
        RHICmdList.UpdateTextureReference(GetTextureReference(), TextureRHI);
        Source.Reset();
    }
    virtual void ReleaseRHI() override
    {
        RHIClearTextureReference(GetTextureReference());
        State->Texture.SafeRelease();
        FTextureResource::ReleaseRHI();
    }
private:
    FIntPoint Size;
    TextureFilter Filter;
    FStatePtr State, Source;
    uint64 Reservation = 0;
    FLayoutReservationPtr LayoutReservation;
};

UTexture2D* CreateMask(FIntPoint Dimensions, const FMD_ScrapeMaskPixels& Pixels,
    TextureFilter Filter, bool bSharedInitial, FStatePtr Source = nullptr)
{
    check(IsInGameThread());
    if (!FApp::CanEverRender() || Dimensions.X <= 0 || Dimensions.Y <= 0
        || Dimensions.X > MAX_uint16 || Dimensions.Y > MAX_uint16
        || static_cast<int64>(Dimensions.X) * Dimensions.Y != Pixels.Num()) return nullptr;
    const uint64 Reservation = AlignedUploadBytes(Dimensions.X, Dimensions.Y) + Pixels.GetBinaryUploadBound(Dimensions.X);
    if (!ReserveInitial(Reservation))
    {
        AdmissionFailures.fetch_add(1);
        UE_LOG(LogMDScrapeMaskUpload, Error,
            TEXT("Mask initialization exceeded the fixed %llu-byte startup staging budget; no uninitialized texture was published."),
            FMD_ScrapeRuntimeMaskTexture::MaximumInitialUploadBytes);
        TraceUploadStats();
        return nullptr;
    }
    // Every wall keeps its own completion fence, while immutable layout input is
    // charged once until all associated uploads finish. The 256 MiB limit is unchanged.
    FLayoutReservationPtr LayoutReservation;
    if (!AcquireLayoutReservation(Pixels, LayoutReservation))
    {
        InitialPending.fetch_sub(Reservation);
        AdmissionFailures.fetch_add(1);
        UE_LOG(LogMDScrapeMaskUpload, Error, TEXT("Shared mask layout exceeded the fixed startup staging budget."));
        TraceUploadStats();
        return nullptr;
    }
    auto* Texture = NewObject<UTexture2D>();
    Texture->SRGB = false; Texture->NeverStream = true; Texture->Filter = Filter;
    Texture->AddressX = TA_Clamp; Texture->AddressY = TA_Clamp;
    auto* Data = new FTexturePlatformData();
    Data->SizeX = Dimensions.X; Data->SizeY = Dimensions.Y;
    Data->PixelFormat = PF_G8; Data->SetNumSlices(1);
    Data->Mips.Add(new FTexture2DMipMap(Dimensions.X, Dimensions.Y, 1));
    Texture->SetPlatformData(Data);
    auto State = MakeShared<FMaskUploadState, ESPMode::ThreadSafe>();
    State->CurrentPixels = Pixels; State->CurrentPixels.ClearDirty();
    State->bSharedInitial = bSharedInitial;
    State->Size = Dimensions;
    auto* Resource = new FRuntimeMaskResource(Dimensions, Filter, State, MoveTemp(Source), Reservation, MoveTemp(LayoutReservation));
    State->ResourceIdentity = Resource;
    // SetResource publishes the matching RT pointer first. Initialization mirrors the
    // exported UTexture::UpdateResourceWithParams lifecycle without invoking UTexture2D's
    // cooked/source-data resource builder. UTexture retains final release ownership.
    Texture->SetResource(Resource);
    ENQUEUE_RENDER_COMMAND(MD_ScrapeInitializeRuntimeMask)(
        [Texture, Resource](FRHICommandListImmediate& Cmd)
    {
        Resource->SetTextureReference(Texture->TextureReference.TextureReferenceRHI);
        Resource->InitResource(Cmd);
    });
    TextureStates.Add(Texture, State);
    EnsureUploadTicker();
    TraceUploadStats();
    return Texture;
}

void QueuePixels(const FStatePtr& State, int32 Width, FMD_ScrapeMaskPixels& Pixels)
{
    FScopeLock Lock(&State->Mutex);
    State->CurrentPixels = Pixels; State->CurrentPixels.ClearDirty();
    State->PendingPixels = State->CurrentPixels;
    if (State->Dirty.IsEmpty())
        State->Dirty.Init(false, FMath::DivideAndRoundUp(Pixels.Num(), FMD_ScrapeMaskPixels::PageSize));
    for (TConstSetBitIterator<FDefaultAllocator> It(Pixels.GetDirtyPages()); It; ++It)
    {
        if (!State->bHasDirtyWork)
        { State->bHasDirtyWork = true; QueuedDirtyStates.fetch_add(1); }
        State->Dirty[It.GetIndex()] = true;
    }
    Pixels.ClearDirty();
}

}

const FMD_ScrapeMaskPixels* FMD_ScrapeRuntimeMaskTexture::GetPixels(const UTexture2D* Texture)
{
    const auto State = FindState(Texture);
    return State ? &State->CurrentPixels : nullptr;
}
UTexture2D* FMD_ScrapeRuntimeMaskTexture::AcquireInitial(FIntPoint Dimensions,
    const FMD_ScrapeMaskPixels& Pixels, TextureFilter Filter)
{
    PruneStates();
    for (const auto& Pair : TextureStates)
    {
        auto* Texture = const_cast<UTexture2D*>(Pair.Key.Get());
        const auto State = FindState(Texture);
        if (State && State->bSharedInitial && Pixels.IsPristine() && State->CurrentPixels.IsPristine()
            && Texture->GetSizeX() == Dimensions.X && Texture->GetSizeY() == Dimensions.Y && Texture->Filter == Filter
            && State->CurrentPixels.GetInitialIdentity() == Pixels.GetInitialIdentity()) return Texture;
    }
    return CreateMask(Dimensions, Pixels, Filter, Pixels.IsPristine());
}
UTexture2D* FMD_ScrapeRuntimeMaskTexture::UpdateMask(UTexture2D* Texture, FMD_ScrapeMaskPixels& Pixels)
{
    check(IsInGameThread());
    if (!IsValid(Texture)) return nullptr;
    PruneStates();
    if (Pixels.IsPristine())
    {
        // Exact reset returns to the immutable shared template. Pending work on the
        // former private texture keeps its normal resource/fence ownership until release.
        auto* InitialTexture = AcquireInitial(FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()), Pixels, Texture->Filter);
        if (InitialTexture) Pixels.ClearDirty();
        return InitialTexture;
    }
    auto State = FindState(Texture);
    if (!State)
    {
        // An external UpdateResource replaced the resource. Reconstruct from the
        // surface's complete exact CPU state, never from empty platform BulkData.
        Texture = CreateMask(FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()), Pixels, Texture->Filter, false);
        if (!Texture) return nullptr;
        State = FindState(Texture);
    }
    else if (State->bSharedInitial)
    {
        Texture = CreateMask(FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()),
            State->CurrentPixels, Texture->Filter, false, State);
        if (!Texture) return nullptr;
        State = FindState(Texture);
    }
    if (!State) return nullptr;
    QueuePixels(State, Texture->GetSizeX(), Pixels);
    return Texture;
}
