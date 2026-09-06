#pragma once

#include "CoreMinimal.h"

struct FMeshDescription;

/** Geometry is retained; only exposed fragments acquire contamination. */
enum class EMD_ScrapeExposureReason : uint8
{
    Exposed = 0,
    InsideOtherSurface = 1,
    NearOpposingContact = 2,
    CoincidentDuplicate = 3
};

/** A local-cm source triangle or a split fragment. SourceTriangleId is an original MeshDescription ID. */
struct MARINEDEMOLITION_API FMD_ScrapeSurfaceFragment
{
    int32 SourceTriangleId = INDEX_NONE;
    FVector A = FVector::ZeroVector;
    FVector B = FVector::ZeroVector;
    FVector C = FVector::ZeroVector;
    EMD_ScrapeExposureReason ExclusionReason = EMD_ScrapeExposureReason::Exposed;
    bool IsCleanable() const { return ExclusionReason == EMD_ScrapeExposureReason::Exposed; }
    FVector OutwardNormal() const
    {
        const FVector Cross = FVector::CrossProduct(B - A, C - A);
        return -Cross.GetSafeNormal(1e-24);
    }
};

/** Output triangle IDs can be sparse; original source IDs can appear in many output triangles. */
struct MARINEDEMOLITION_API FMD_ScrapeSourceTriangleRef
{
    int32 SourceTriangleId = INDEX_NONE;
    FVector BarycentricA = FVector::ZeroVector;
    FVector BarycentricB = FVector::ZeroVector;
    FVector BarycentricC = FVector::ZeroVector;
    EMD_ScrapeExposureReason ExclusionReason = EMD_ScrapeExposureReason::Exposed;
    bool IsCleanable() const { return ExclusionReason == EMD_ScrapeExposureReason::Exposed; }
};

struct MARINEDEMOLITION_API FMD_ScrapeExposedSurfaceSettings
{
    double IntersectionToleranceCm = 0.0001;
    double ClassificationOffsetCm = 0.001;
    double ContactGapToleranceCm = 0.1;
    double ContactNormalDotThreshold = 0.999;
    double CoplanarToleranceCm = 0.0001;
    int32 MaximumOutputTriangles = 100000;
};

struct MARINEDEMOLITION_API FMD_ScrapeExposedSurfaceResult
{
    TMap<int32, FMD_ScrapeSourceTriangleRef> TriangleSourceRefs;
    int32 OriginalTriangleCount = 0;
    int32 IntersectionSegmentCount = 0;
    int32 OutputTriangleCount = 0;
    int32 ExposedTriangleCount = 0;
    double OriginalAreaCm2 = 0;
    double ExposedAreaCm2 = 0;
    double InteriorAreaCm2 = 0;
    double NearContactAreaCm2 = 0;
    double DuplicateAreaCm2 = 0;
    FString Diagnostics;
};

/** Editor-only source splitting/classification. It never changes the input description or an asset. */
class MARINEDEMOLITION_API FMD_ScrapeExposedSurface final
{
public:
#if WITH_EDITOR
    static bool Build(const FMeshDescription& Source,
        const FMD_ScrapeExposedSurfaceSettings& Settings,
        FMeshDescription& OutDescription, FMD_ScrapeExposedSurfaceResult& OutResult,
        FString& OutError);
#endif
};
