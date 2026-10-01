#pragma once
#include <cstdint>

namespace MD_ScrapeMaskUploadPrivate
{
struct FPageUploadRegion
{
    int32_t X, Y, Width, Height, SourceOffset;
};

// Start/Length describe one valid contiguous page in a G8 texture of positive width.
// Emit only that page's bytes: a leading fragment, complete rows, and a trailing fragment.
// SourceOffset addresses a packed copy of the page; source pitch is each region's Width.
template <typename Visitor>
void ForEachPageUploadRegion(int32_t Start, int32_t Length, int32_t TextureWidth, Visitor&& Visit)
{
    int32_t SourceOffset = 0;
    while (SourceOffset < Length)
    {
        const int32_t Pixel = Start + SourceOffset;
        const int32_t X = Pixel % TextureWidth;
        const int32_t Remaining = Length - SourceOffset;
        const int32_t RowRemaining = TextureWidth - X;
        const int32_t Width = Remaining < RowRemaining ? Remaining : RowRemaining;
        const int32_t Height = X == 0 && Remaining >= TextureWidth ? Remaining / TextureWidth : 1;
        Visit(FPageUploadRegion{X, Pixel / TextureWidth, Width, Height, SourceOffset});
        SourceOffset += Width * Height;
    }
}
}
