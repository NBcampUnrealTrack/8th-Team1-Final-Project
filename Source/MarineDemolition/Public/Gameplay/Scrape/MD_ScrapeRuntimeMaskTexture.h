#pragma once
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Gameplay/Scrape/MD_ScrapeMaskPixels.h"


/** Ordinary UTexture2D objects with an explicitly owned runtime G8 render resource.
 * No subclass of the engine's non-exported/final UTexture2D implementation is needed.
 */
class MARINEDEMOLITION_API FMD_ScrapeRuntimeMaskTexture final
{
public:
    static constexpr uint64 MaximumInitialUploadBytes = 256ull * 1024 * 1024;
    static constexpr uint64 MaximumDirtyUploadBytes = 32ull * 1024 * 1024;

    static UTexture2D* AcquireInitial(FIntPoint Dimensions,
        const FMD_ScrapeMaskPixels& Pixels, TextureFilter InFilter);
    static UTexture2D* UpdateMask(UTexture2D* Texture, FMD_ScrapeMaskPixels& Pixels);
    /** GT-only borrowed pixels; nullptr means another path replaced the resource. */
    static const FMD_ScrapeMaskPixels* GetPixels(const UTexture2D* Texture);
};
