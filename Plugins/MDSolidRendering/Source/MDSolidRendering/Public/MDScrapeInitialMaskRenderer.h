#pragma once
#include "CoreMinimal.h"
class FRHICommandListImmediate;
class FRHITexture;

/** Immutable atlas addressing, shared by every wall using the same baked layout. */
class MDSOLIDRENDERING_API FMDScrapeInitialMaskLayout
{
public:
    FMDScrapeInitialMaskLayout(TArray<uint32>&& InSources, TArray<int32>&& InSeamPixels);
    ~FMDScrapeInitialMaskLayout();
    const TArray<uint32> Sources;
    const TArray<int32> SeamPixels;
    uint64 GetCPUBytes() const;
private:
    struct FGPUData;
    TUniquePtr<FGPUData> GPU;
    friend class FMDScrapeInitialMaskRenderer;
};

class MDSOLIDRENDERING_API FMDScrapeInitialMaskRenderer
{
public:
    static bool IsSupported();
    /** All atlas pixels are written on GPU. Exact seam G8 bytes come from the legacy CPU sum. */
    static bool Generate(FRHICommandListImmediate& Cmd, FRHITexture* Texture, FIntPoint Size,
        const FMDScrapeInitialMaskLayout& Layout, TConstArrayView<uint32> ActiveBits,
        TConstArrayView<uint8> SeamBytes);
    static uint64 GetLiveLayoutGPUBytes();
};
