#include "Gameplay/Scrape/MD_ScrapeSurfaceExposureClip.h"

namespace
{
    using FExposurePolygon = TArray<FVector2D>;
    constexpr double DepthTieToleranceCm = 1e-9;
    constexpr double DuplicatePointToleranceSquared = 1e-24;

    struct FPlaneVertex { FVector2D Point; double Depth; };
    struct FClipRegion
    {
        FExposurePolygon Points;
        EMD_ScrapeExposureReason Reason;
    };
    struct FSourcePlane
    {
        const FMD_ScrapeSurfaceFragment* Triangle = nullptr;
        FVector Normal, U, V;
        FBox Bounds = FBox(ForceInit);
        TArray<FClipRegion> Regions;
        bool bRegionsBuilt = false;
    };
    struct FClassifiedPolygon
    {
        FExposurePolygon Points;
        EMD_ScrapeExposureReason Reason;
    };

    double Cross(const FVector2D& A, const FVector2D& B)
    { return A.X * B.Y - A.Y * B.X; }

    double SignedDoubleArea(const FExposurePolygon& Points)
    {
        double Result = 0;
        for (int32 I = 1; I + 1 < Points.Num(); ++I)
        { Result += Cross(Points[I] - Points[0], Points[I + 1] - Points[0]); }
        return Result;
    }

    void AddUnique(FExposurePolygon& Polygon, const FVector2D& Point)
    {
        if (Polygon.IsEmpty() || (Polygon.Last() - Point).SizeSquared() > DuplicatePointToleranceSquared)
        { Polygon.Add(Point); }
    }

    void ClosePolygon(FExposurePolygon& Polygon)
    {
        if (Polygon.Num() > 1 && (Polygon[0] - Polygon.Last()).SizeSquared() <= DuplicatePointToleranceSquared)
        { Polygon.Pop(EAllowShrinking::No); }
        if (Polygon.Num() < 3 || SignedDoubleArea(Polygon) == 0.0) { Polygon.Reset(); }
    }

    // Both half planes retain their common edge. Its area is zero; their interiors never overlap.
    void SplitPolygon(const FExposurePolygon& Polygon, const FVector2D& A, const FVector2D& B,
        FExposurePolygon& Inside, FExposurePolygon& Outside)
    {
        Inside.Reset(); Outside.Reset();
        if (Polygon.IsEmpty()) { return; }
        FVector2D Previous = Polygon.Last();
        double PreviousDistance = Cross(B - A, Previous - A);
        for (const FVector2D& Current : Polygon)
        {
            const double Distance = Cross(B - A, Current - A);
            if ((PreviousDistance < 0.0 && Distance > 0.0) || (PreviousDistance > 0.0 && Distance < 0.0))
            {
                const FVector2D Point = FMath::Lerp(Previous, Current,
                    FMath::Clamp(PreviousDistance / (PreviousDistance - Distance), 0.0, 1.0));
                AddUnique(Inside, Point); AddUnique(Outside, Point);
            }
            if (Distance >= 0.0) { AddUnique(Inside, Current); }
            if (Distance <= 0.0) { AddUnique(Outside, Current); }
            Previous = Current; PreviousDistance = Distance;
        }
        ClosePolygon(Inside); ClosePolygon(Outside);
    }

    void ClipDepth(TArray<FPlaneVertex>& Vertices, const double Boundary, const bool bKeepAbove)
    {
        if (Vertices.IsEmpty()) { return; }
        TArray<FPlaneVertex> Result;
        FPlaneVertex Previous = Vertices.Last();
        double PreviousDistance = bKeepAbove ? Previous.Depth - Boundary : Boundary - Previous.Depth;
        for (const FPlaneVertex& Current : Vertices)
        {
            const double Distance = bKeepAbove ? Current.Depth - Boundary : Boundary - Current.Depth;
            if ((PreviousDistance < 0.0 && Distance > 0.0) || (PreviousDistance > 0.0 && Distance < 0.0))
            {
                const double T = FMath::Clamp(PreviousDistance / (PreviousDistance - Distance), 0.0, 1.0);
                Result.Add({FMath::Lerp(Previous.Point, Current.Point, T), Boundary});
            }
            if (Distance >= 0.0) { Result.Add(Current); }
            Previous = Current; PreviousDistance = Distance;
        }
        Vertices = MoveTemp(Result);
    }

    void BuildRegions(FSourcePlane& Plane, const TArray<FSourcePlane>& Planes,
        const FMD_ScrapeExposedSurfaceSettings& Settings)
    {
        Plane.bRegionsBuilt = true;
        const FBox ExpandedBounds = Plane.Bounds.ExpandBy(
            Settings.ContactGapToleranceCm + Settings.CoplanarToleranceCm);
        for (const FSourcePlane& Other : Planes)
        {
            if (Other.Triangle->SourceTriangleId == Plane.Triangle->SourceTriangleId
                || !ExpandedBounds.Intersect(Other.Bounds)) { continue; }
            const double NormalDot = FVector::DotProduct(Plane.Normal, Other.Normal);
            const bool bOpposing = NormalDot <= -Settings.ContactNormalDotThreshold;
            const bool bSameNormal = NormalDot >= Settings.ContactNormalDotThreshold;
            if (!bOpposing && !bSameNormal) { continue; }

            double LowerDepth = -Settings.CoplanarToleranceCm;
            double UpperDepth = Settings.ContactGapToleranceCm;
            EMD_ScrapeExposureReason Reason = EMD_ScrapeExposureReason::NearOpposingContact;
            if (bSameNormal)
            {
                // The outer sheet owns a near-coincident region. Source ID only resolves numerical depth ties.
                const bool bOtherWinsTie = Other.Triangle->SourceTriangleId < Plane.Triangle->SourceTriangleId;
                LowerDepth = bOtherWinsTie ? -DepthTieToleranceCm : DepthTieToleranceCm;
                UpperDepth = Settings.CoplanarToleranceCm;
                Reason = EMD_ScrapeExposureReason::CoincidentDuplicate;
            }
            TArray<FPlaneVertex> Vertices;
            for (const FVector& Point : {Other.Triangle->A, Other.Triangle->B, Other.Triangle->C})
            {
                const FVector Delta = Point - Plane.Triangle->A;
                Vertices.Add({FVector2D(FVector::DotProduct(Delta, Plane.U),
                    FVector::DotProduct(Delta, Plane.V)), FVector::DotProduct(Delta, Plane.Normal)});
            }
            // A near-parallel triangle can cross a depth boundary; classify only that projected portion.
            ClipDepth(Vertices, LowerDepth, true);
            ClipDepth(Vertices, UpperDepth, false);
            FClipRegion Region; Region.Reason = Reason;
            for (const FPlaneVertex& Vertex : Vertices) { AddUnique(Region.Points, Vertex.Point); }
            ClosePolygon(Region.Points);
            if (Region.Points.IsEmpty()) { continue; }
            if (SignedDoubleArea(Region.Points) < 0)
            {
                for (int32 I = 0; I < Region.Points.Num() / 2; ++I)
                { Swap(Region.Points[I], Region.Points[Region.Points.Num() - 1 - I]); }
            }
            Plane.Regions.Add(MoveTemp(Region));
        }
    }

    bool EmitPolygon(const FClassifiedPolygon& Polygon, const FSourcePlane& Plane,
        const FMD_ScrapeSurfaceFragment& Original, const int32 Maximum,
        TArray<FMD_ScrapeSurfaceFragment>& Result, double& OutArea)
    {
        const auto ToWorld = [&Plane](const FVector2D& P)
        { return Plane.Triangle->A + Plane.U * P.X + Plane.V * P.Y; };
        for (int32 I = 1; I + 1 < Polygon.Points.Num(); ++I)
        {
            // Keep every positive-area fragment; never discard a thin exposed strip by an area threshold.
            const double Area = 0.5 * FMath::Abs(Cross(Polygon.Points[I] - Polygon.Points[0],
                Polygon.Points[I + 1] - Polygon.Points[0]));
            if (Area == 0.0) { continue; }
            if (!FMath::IsFinite(Area) || Result.Num() >= Maximum) { return false; }
            FMD_ScrapeSurfaceFragment Fragment;
            Fragment.SourceTriangleId = Original.SourceTriangleId;
            Fragment.A = ToWorld(Polygon.Points[0]);
            Fragment.B = ToWorld(Polygon.Points[I]);
            Fragment.C = ToWorld(Polygon.Points[I + 1]);
            Fragment.ExclusionReason = Polygon.Reason;
            Result.Add(Fragment); OutArea += Area;
        }
        return true;
    }
}

bool FMD_ScrapeSurfaceExposureClip::SplitAndClassify(
    const TConstArrayView<FMD_ScrapeSurfaceFragment> SourceTriangles,
    const TConstArrayView<FMD_ScrapeSurfaceFragment> InputFragments,
    const FMD_ScrapeExposedSurfaceSettings& Settings,
    TArray<FMD_ScrapeSurfaceFragment>& OutFragments, FString& OutError)
{
    OutFragments.Reset(); OutError.Reset();
    if (!FMath::IsFinite(Settings.ContactGapToleranceCm) || Settings.ContactGapToleranceCm < 0
        || !FMath::IsFinite(Settings.CoplanarToleranceCm) || Settings.CoplanarToleranceCm < DepthTieToleranceCm
        || !FMath::IsFinite(Settings.IntersectionToleranceCm) || Settings.IntersectionToleranceCm <= 0
        || !FMath::IsFinite(Settings.ContactNormalDotThreshold) || Settings.ContactNormalDotThreshold <= 0
        || Settings.ContactNormalDotThreshold > 1 || Settings.MaximumOutputTriangles <= 0
        || Settings.MaximumOutputTriangles < InputFragments.Num())
    { OutError = TEXT("Invalid exposure clip settings or insufficient output triangle budget."); return false; }

    TArray<FSourcePlane> Planes;
    TMap<int32, int32> SourceToPlane;
    for (const FMD_ScrapeSurfaceFragment& Source : SourceTriangles)
    {
        if (Source.SourceTriangleId < 0 || SourceToPlane.Contains(Source.SourceTriangleId)
            || Source.A.ContainsNaN() || Source.B.ContainsNaN() || Source.C.ContainsNaN())
        { OutError = TEXT("Invalid or duplicate source triangle in exposure clipping."); return false; }
        FSourcePlane Plane;
        Plane.Triangle = &Source;
        Plane.Normal = Source.OutwardNormal();
        const double EdgeLength = (Source.B - Source.A).Size();
        if (Plane.Normal.IsNearlyZero(1e-12) || !FMath::IsFinite(EdgeLength) || EdgeLength <= 0)
        { OutError = TEXT("Degenerate source triangle in exposure clipping."); return false; }
        Plane.U = (Source.B - Source.A) / EdgeLength;
        Plane.V = FVector::CrossProduct(Plane.Normal, Plane.U);
        Plane.Bounds += Source.A; Plane.Bounds += Source.B; Plane.Bounds += Source.C;
        SourceToPlane.Add(Source.SourceTriangleId, Planes.Num());
        Planes.Add(MoveTemp(Plane));
    }

    TArray<FMD_ScrapeSurfaceFragment> Result;
    for (const FMD_ScrapeSurfaceFragment& Input : InputFragments)
    {
        const int32* PlaneIndex = SourceToPlane.Find(Input.SourceTriangleId);
        if (!PlaneIndex || Input.A.ContainsNaN() || Input.B.ContainsNaN() || Input.C.ContainsNaN())
        { OutError = TEXT("Invalid fragment source or position in exposure clipping."); return false; }
        if (!Input.IsCleanable())
        {
            if (Result.Num() >= Settings.MaximumOutputTriangles)
            { OutError = TEXT("Exposure clipping exceeded its output triangle budget."); return false; }
            Result.Add(Input); continue;
        }
        FSourcePlane& Plane = Planes[*PlaneIndex];
        if (!Plane.bRegionsBuilt) { BuildRegions(Plane, Planes, Settings); }
        if (Plane.Regions.IsEmpty())
        {
            if (Result.Num() >= Settings.MaximumOutputTriangles)
            { OutError = TEXT("Exposure clipping exceeded its output triangle budget."); return false; }
            Result.Add(Input); continue;
        }
        FClassifiedPolygon Initial; Initial.Reason = Input.ExclusionReason;
        for (const FVector& Point : {Input.A, Input.B, Input.C})
        {
            const FVector Delta = Point - Plane.Triangle->A;
            if (FMath::Abs(FVector::DotProduct(Delta, Plane.Normal)) > Settings.IntersectionToleranceCm)
            { OutError = TEXT("Cut fragment no longer lies in its original source triangle plane."); return false; }
            Initial.Points.Add(FVector2D(FVector::DotProduct(Delta, Plane.U), FVector::DotProduct(Delta, Plane.V)));
        }
        const double InputArea = 0.5 * FMath::Abs(SignedDoubleArea(Initial.Points));
        if (!FMath::IsFinite(InputArea) || InputArea <= 0)
        { OutError = TEXT("Degenerate input fragment in exposure clipping."); return false; }
        TArray<FClassifiedPolygon> Polygons;
        Polygons.Add(MoveTemp(Initial));
        for (const FClipRegion& Region : Plane.Regions)
        {
            TArray<FClassifiedPolygon> Next;
            for (FClassifiedPolygon& Polygon : Polygons)
            {
                if (Next.Num() >= Settings.MaximumOutputTriangles)
                { OutError = TEXT("Exposure clipping exceeded its intermediate polygon budget."); return false; }
                if (Polygon.Reason != EMD_ScrapeExposureReason::Exposed)
                { Next.Add(MoveTemp(Polygon)); continue; }
                // Do not subdivide geometry merely because it crosses an infinite clip-edge line.
                // First establish a positive-area intersection with the finite opposing triangle.
                FExposurePolygon Covered = Polygon.Points;
                for (int32 Edge = 0; Edge < Region.Points.Num() && !Covered.IsEmpty(); ++Edge)
                {
                    FExposurePolygon Inside, Outside;
                    SplitPolygon(Covered, Region.Points[Edge],
                        Region.Points[(Edge + 1) % Region.Points.Num()], Inside, Outside);
                    Covered = MoveTemp(Inside);
                }
                if (Covered.IsEmpty()) { Next.Add(MoveTemp(Polygon)); continue; }
                FExposurePolygon Remaining = MoveTemp(Polygon.Points);
                for (int32 Edge = 0; Edge < Region.Points.Num() && !Remaining.IsEmpty(); ++Edge)
                {
                    FExposurePolygon Inside, Outside;
                    SplitPolygon(Remaining, Region.Points[Edge],
                        Region.Points[(Edge + 1) % Region.Points.Num()], Inside, Outside);
                    if (!Outside.IsEmpty()) { Next.Add({MoveTemp(Outside), EMD_ScrapeExposureReason::Exposed}); }
                    Remaining = MoveTemp(Inside);
                }
                if (!Remaining.IsEmpty()) { Next.Add({MoveTemp(Remaining), Region.Reason}); }
            }
            Polygons = MoveTemp(Next);
            int64 TriangleBudget = Result.Num();
            for (const FClassifiedPolygon& Polygon : Polygons) { TriangleBudget += Polygon.Points.Num() - 2; }
            if (TriangleBudget > Settings.MaximumOutputTriangles)
            { OutError = TEXT("Exposure clipping exceeded its output triangle budget; geometry was not committed."); return false; }
        }
        double OutputArea = 0;
        for (const FClassifiedPolygon& Polygon : Polygons)
        {
            if (!EmitPolygon(Polygon, Plane, Input, Settings.MaximumOutputTriangles, Result, OutputArea))
            { OutError = TEXT("Exposure clipping produced invalid geometry or exceeded its triangle budget."); return false; }
        }
        if (!FMath::IsFinite(OutputArea) || FMath::Abs(OutputArea - InputArea) > FMath::Max(1e-8, InputArea * 1e-9))
        { OutError = TEXT("Exposure polygon splitting failed its geometry area conservation check."); return false; }
    }
    OutFragments = MoveTemp(Result);
    return true;
}
