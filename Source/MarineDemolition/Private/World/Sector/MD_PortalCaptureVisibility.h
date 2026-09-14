#pragma once

#include "CoreMinimal.h"
#include "ConvexVolume.h"

namespace MD_PortalCaptureVisibility
{
    // Conservative bounds keep partial/near-plane visibility. Subpixel portals retain their
    // last image instead of rendering a full scene that cannot be resolved by the current view.
    inline bool Intersects(const FMatrix& ViewProjection, const FBoxSphereBounds& SurfaceBounds,
        FIntPoint ViewSize=FIntPoint::ZeroValue)
    {
        FConvexVolume Frustum;
        GetViewFrustumBounds(Frustum, ViewProjection, true);
        if (!Frustum.IntersectBox(SurfaceBounds.Origin, SurfaceBounds.BoxExtent)) return false;
        if (ViewSize.X<=0 || ViewSize.Y<=0) return true;
        FVector2D Minimum(DBL_MAX,DBL_MAX), Maximum(-DBL_MAX,-DBL_MAX);
        for (int32 Corner=0;Corner<8;++Corner)
        {
            const FVector P=SurfaceBounds.Origin+SurfaceBounds.BoxExtent*
                FVector(Corner&1?1:-1,Corner&2?1:-1,Corner&4?1:-1);
            const FVector4 Clip=ViewProjection.TransformFVector4(FVector4(P,1));
            if (Clip.W<=UE_SMALL_NUMBER) return true;
            const FVector2D NDC(Clip.X/Clip.W,Clip.Y/Clip.W);
            Minimum.X=FMath::Min(Minimum.X,NDC.X);Minimum.Y=FMath::Min(Minimum.Y,NDC.Y);
            Maximum.X=FMath::Max(Maximum.X,NDC.X);Maximum.Y=FMath::Max(Maximum.Y,NDC.Y);
        }
        return (Maximum.X-Minimum.X)*0.5*ViewSize.X>=1.0
            || (Maximum.Y-Minimum.Y)*0.5*ViewSize.Y>=1.0;
    }
}
