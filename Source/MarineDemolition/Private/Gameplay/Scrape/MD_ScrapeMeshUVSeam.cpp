#include "Gameplay/Scrape/MD_ScrapeMeshUVSeam.h"
#include "HAL/PlatformTime.h"

namespace
{
    // Only connected, similarly facing surfaces share a reconstruction footprint.
    // Real corners, excluded caps, disconnected parts and opposite faces remain separate.
    constexpr double MinimumNormalDot = 0.95;
    constexpr int32 MaximumTrianglesPerPixel = 128;
    constexpr int32 MaximumCellsPerPixel = 2048;

    struct FSeamChartFrame
    {
        FVector Origin, DualU, DualV;
        FVector2D UVOrigin;
        bool bValid = false;
        FVector2D Project(const FVector& Point) const
        {
            const FVector D = Point - Origin;
            return UVOrigin + FVector2D(FVector::DotProduct(D, DualU), FVector::DotProduct(D, DualV));
        }
    };

    FSeamChartFrame MakeFrame(const FMD_ScrapeMeshUVTriangle& T, const FVector2D& Size)
    {
        FSeamChartFrame F;
        F.Origin = T.A; F.UVOrigin = T.UVA * Size;
        const FVector E0 = T.B - T.A, E1 = T.C - T.A;
        const FVector N = FVector::CrossProduct(E0, E1);
        const double D = N.SizeSquared();
        if (!FMath::IsFinite(D) || D <= 0.0) return F;
        const FVector B = FVector::CrossProduct(E1, N) / D;
        const FVector C = FVector::CrossProduct(N, E0) / D;
        const FVector2D U = (T.UVB - T.UVA) * Size, V = (T.UVC - T.UVA) * Size;
        F.DualU = B * U.X + C * V.X; F.DualV = B * U.Y + C * V.Y;
        F.bValid = !F.DualU.ContainsNaN() && !F.DualV.ContainsNaN();
        return F;
    }

    struct FVertex
    {
        FVector2D Position; // Relative destination texel coordinates avoid large-coordinate cancellation.
        FVector2D UV;       // Source chart texel coordinates.
    };
    using FPolygon = TArray<FVertex, TInlineAllocator<16>>;

    void Clip(FPolygon& Polygon, bool bUV, int32 Axis, double Limit, bool bGreater)
    {
        if (Polygon.IsEmpty()) return;
        FPolygon Result;
        FVertex Previous = Polygon.Last();
        auto Distance = [=](const FVertex& P)
        { return ((bUV ? P.UV : P.Position)[Axis] - Limit) * (bGreater ? 1.0 : -1.0); };
        double PreviousD = Distance(Previous);
        for (const FVertex& Current : Polygon)
        {
            const double D = Distance(Current);
            if ((D >= 0.0) != (PreviousD >= 0.0))
            {
                const double Alpha = PreviousD / (PreviousD - D);
                Result.Add({FMath::Lerp(Previous.Position, Current.Position, Alpha),
                    FMath::Lerp(Previous.UV, Current.UV, Alpha)});
            }
            if (D >= 0.0) Result.Add(Current);
            Previous = Current; PreviousD = D;
        }
        Polygon = MoveTemp(Result);
    }

    double Area(const FPolygon& Polygon)
    {
        double Sum = 0.0;
        for (int32 I = 1; I + 1 < Polygon.Num(); ++I)
            Sum += FVector2D::CrossProduct(Polygon[I].Position - Polygon[0].Position,
                Polygon[I + 1].Position - Polygon[0].Position);
        return FMath::Abs(Sum) * 0.5;
    }

    bool PortalTouchesPixel(FVector2D A, FVector2D B)
    {
        double Minimum = 0.0, Maximum = 1.0;
        const FVector2D Delta = B - A;
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            if (FMath::Abs(Delta[Axis]) < 1.e-15)
            {
                if (A[Axis] < -1.e-6 || A[Axis] > 1.0 + 1.e-6) return false;
                continue;
            }
            const double X = (-1.e-6 - A[Axis]) / Delta[Axis];
            const double Y = (1.0 + 1.e-6 - A[Axis]) / Delta[Axis];
            Minimum = FMath::Max(Minimum, FMath::Min(X, Y));
            Maximum = FMath::Min(Maximum, FMath::Max(X, Y));
            if (Minimum > Maximum) return false;
        }
        return true;
    }

    bool BuildPixel(const UMD_ScrapeMeshUVData& Data, const TArray<FSeamChartFrame>& Frames,
        int32 Pixel, int32 Owner, TArray<FMD_ScrapeMeshUVVisualSample>& Samples)
    {
        Samples.Reset();
        const FSeamChartFrame& Frame = Frames[Owner];
        if (!Frame.bValid) return true;
        const FVector2D Size(Data.AtlasDimensions);
        const FVector2D Origin(Pixel % Data.AtlasDimensions.X, Pixel / Data.AtlasDimensions.X);
        TArray<int32, TInlineAllocator<32>> Queue;
        Queue.Add(Owner);
        int32 VisitedCells = 0;
        double TotalArea = 0.0;
        bool bCrossesSeam = false;
        for (int32 Q = 0; Q < Queue.Num(); ++Q)
        {
            const int32 TriangleIndex = Queue[Q];
            const FMD_ScrapeMeshUVTriangle& T = Data.Triangles[TriangleIndex];
            FPolygon Polygon;
            Polygon.Add({Frame.Project(T.A) - Origin, T.UVA * Size});
            Polygon.Add({Frame.Project(T.B) - Origin, T.UVB * Size});
            Polygon.Add({Frame.Project(T.C) - Origin, T.UVC * Size});
            for (int32 Axis = 0; Axis < 2; ++Axis)
            {
                Clip(Polygon, false, Axis, 0.0, true);
                Clip(Polygon, false, Axis, 1.0, false);
            }
            const double TriangleArea = Area(Polygon);
            // Full interior texels already represent exactly this footprint.
            if (Q == 0 && Data.Cells[Pixel].IsActive() && TriangleArea >= 1.0 - 1.e-7) return true;
            if (TriangleArea > 1.e-12)
            {
                FVector2D Minimum = Polygon[0].UV, Maximum = Minimum;
                for (const FVertex& V : Polygon)
                    for (int32 Axis = 0; Axis < 2; ++Axis)
                    {
                        Minimum[Axis] = FMath::Min(Minimum[Axis], V.UV[Axis]);
                        Maximum[Axis] = FMath::Max(Maximum[Axis], V.UV[Axis]);
                    }
                const FIntPoint Begin(FMath::Clamp(FMath::FloorToInt(Minimum.X), 0, Data.AtlasDimensions.X - 1),
                    FMath::Clamp(FMath::FloorToInt(Minimum.Y), 0, Data.AtlasDimensions.Y - 1));
                const FIntPoint End(FMath::Clamp(FMath::CeilToInt(Maximum.X), 0, Data.AtlasDimensions.X),
                    FMath::Clamp(FMath::CeilToInt(Maximum.Y), 0, Data.AtlasDimensions.Y));
                if ((VisitedCells += (End.X - Begin.X) * (End.Y - Begin.Y)) > MaximumCellsPerPixel) return false;
                for (int32 Y = Begin.Y; Y < End.Y; ++Y)
                    for (int32 X = Begin.X; X < End.X; ++X)
                    {
                        const int32 Source = Y * Data.AtlasDimensions.X + X;
                        if (!Data.IsActiveCell(Source) || Data.Cells[Source].TriangleIndex != TriangleIndex) continue;
                        FPolygon Overlap = Polygon;
                        Clip(Overlap, true, 0, X, true); Clip(Overlap, true, 0, X + 1, false);
                        Clip(Overlap, true, 1, Y, true); Clip(Overlap, true, 1, Y + 1, false);
                        const double Weight = Area(Overlap);
                        if (!FMath::IsFinite(Weight) || Weight <= 1.e-12) continue;
                        Samples.Add({Source, static_cast<float>(Weight)});
                        TotalArea += Weight;
                        bCrossesSeam |= TriangleIndex != Owner;
                    }
            }
            auto Visit = [&](int32 Neighbor, const FVector& A, const FVector& B)
            {
                if (!Data.Triangles.IsValidIndex(Neighbor) || Queue.Contains(Neighbor)) return;
                const FMD_ScrapeMeshUVTriangle& N = Data.Triangles[Neighbor];
                if (!N.bCleanable || !Frames[Neighbor].bValid
                    || FVector::DotProduct(T.NormalLocal, N.NormalLocal) < MinimumNormalDot
                    || FVector::DotProduct(Data.Triangles[Owner].NormalLocal, N.NormalLocal) < MinimumNormalDot
                    || !PortalTouchesPixel(Frame.Project(A) - Origin, Frame.Project(B) - Origin)) return;
                Queue.Add(Neighbor);
            };
            if (!T.Neighbors.IsEmpty())
                for (const FMD_ScrapeMeshUVNeighbor& N : T.Neighbors) Visit(N.TriangleIndex, N.SharedEdgeA, N.SharedEdgeB);
            else
            {
                const FVector Points[] = {T.A, T.B, T.C};
                for (int32 Edge = 0; Edge < 3; ++Edge) Visit(T.AdjacentTriangles[Edge], Points[Edge], Points[(Edge + 1) % 3]);
            }
            if (Queue.Num() > MaximumTrianglesPerPixel) return false;
        }
        if (!bCrossesSeam || TotalArea <= 1.e-12 || !FMath::IsFinite(TotalArea)) { Samples.Reset(); return true; }
        // Normalize display weights only, leaving carrier area and clean amount unchanged.
        Samples.Sort([](const auto& A, const auto& B) { return A.CellIndex < B.CellIndex; });
        for (FMD_ScrapeMeshUVVisualSample& Sample : Samples) Sample.Weight /= TotalArea;
        return true;
    }
}

void MD_ScrapeMeshUVSeam::Build(const UMD_ScrapeMeshUVData& Data,
    TArray<int32>& OutOffsets, TArray<FMD_ScrapeMeshUVVisualSample>& OutSamples,
    TArray<int32>& OutDependentOffsets, TArray<int32>& OutDependents)
{
    OutOffsets.Reset(); OutSamples.Reset(); OutDependentOffsets.Reset(); OutDependents.Reset();
    if (Data.AtlasDimensions.X <= 0 || Data.AtlasDimensions.Y <= 0
        || Data.Cells.Num() != static_cast<int64>(Data.AtlasDimensions.X) * Data.AtlasDimensions.Y) return;
    const double Started = FPlatformTime::Seconds();
    TArray<FSeamChartFrame> Frames;
    Frames.Reserve(Data.Triangles.Num());
    for (const FMD_ScrapeMeshUVTriangle& T : Data.Triangles) Frames.Add(MakeFrame(T, FVector2D(Data.AtlasDimensions)));
    TArray<FMD_ScrapeMeshUVVisualSample> Samples;
    int32 BoundedFallbacks = 0, StencilCount = 0;
    OutOffsets.SetNumZeroed(Data.Cells.Num() + 1);
    OutDependentOffsets.SetNumZeroed(Data.Cells.Num() + 1);
    for (int32 Pixel = 0; Pixel < Data.Cells.Num(); ++Pixel)
    {
        OutOffsets[Pixel] = OutSamples.Num();
        const int32 Source = Data.Cells[Pixel].VisualSourceCellIndex;
        if (!Data.IsActiveCell(Source)) continue;
        const int32 Owner = Data.Cells[Source].TriangleIndex;
        if (!Frames.IsValidIndex(Owner) || !Data.Triangles[Owner].bCleanable) continue;
        if (!BuildPixel(Data, Frames, Pixel, Owner, Samples)) { ++BoundedFallbacks; continue; }
        if (Samples.IsEmpty()) continue;
        ++StencilCount;
        OutSamples.Append(Samples);
        for (const FMD_ScrapeMeshUVVisualSample& Sample : Samples)
            if (Sample.CellIndex != Pixel && Sample.CellIndex != Source) ++OutDependentOffsets[Sample.CellIndex + 1];
    }
    OutOffsets.Last() = OutSamples.Num();
    for (int32 I = 1; I < OutDependentOffsets.Num(); ++I) OutDependentOffsets[I] += OutDependentOffsets[I - 1];
    OutDependents.SetNumUninitialized(OutDependentOffsets.Last());
    // Compact reverse adjacency avoids one hash-map allocation per cross-chart contribution.
    TArray<int32> Cursors = OutDependentOffsets;
    for (int32 Pixel = 0; Pixel < Data.Cells.Num(); ++Pixel)
        for (int32 I = OutOffsets[Pixel]; I < OutOffsets[Pixel + 1]; ++I)
        {
            const int32 Source = OutSamples[I].CellIndex;
            if (Source != Pixel && Source != Data.Cells[Pixel].VisualSourceCellIndex)
                OutDependents[Cursors[Source]++] = Pixel;
        }
    OutSamples.Shrink();
    UE_LOG(LogTemp, Display, TEXT("Scrape seam cache %s: %d pixels, %d samples, %d bounded fallbacks, %.3fs, %.3f MiB shared total."),
        *Data.GetName(), StencilCount, OutSamples.Num(), BoundedFallbacks, FPlatformTime::Seconds() - Started,
        (OutOffsets.GetAllocatedSize() + OutSamples.GetAllocatedSize() + OutDependentOffsets.GetAllocatedSize()
            + OutDependents.GetAllocatedSize()) / (1024.0 * 1024.0));
}
