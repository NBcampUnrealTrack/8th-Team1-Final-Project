#include "Gameplay/Scrape/MD_ContaminationSolidGeometry.h"

#include "Algo/BinarySearch.h"
#include "Algo/Unique.h"
#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "Component/Scrape/MD_ContaminationSolidVisualComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Spatial/SparseDynamicPointOctree3.h"

using namespace UE::Geometry;

TRACE_DECLARE_INT_COUNTER(MD_SolidPrewarm_RetainedGeometryBytes, TEXT("MD/Solid/PrewarmRetainedGeometryBytes"));
TRACE_DECLARE_INT_COUNTER(MD_SolidPrewarm_RetainedGeometries, TEXT("MD/Solid/PrewarmRetainedGeometries"));
TRACE_DECLARE_INT_COUNTER(MD_SolidPrewarm_PeakWorkers, TEXT("MD/Solid/PrewarmPeakWorkers"));

namespace MD_ContaminationSolidGeometryPrivate
{
    constexpr double WeldToleranceCm = 0.001;
    constexpr double NormalWeldDot = 0.95;

    struct FCacheEntry
    {
        TWeakObjectPtr<const UMD_ScrapeMeshUVData> Profile;
        FGuid LayoutId;
        FVector Scale;
        FMD_ContaminationSolidSettings Settings;
        TWeakPtr<const FMD_ContaminationSolidGeometry> Geometry;
    };

    TArray<FCacheEntry> Cache;

    FIntVector Bucket(const FVector& Position, const double Size)
    {
        return FIntVector(FMath::FloorToInt(Position.X / Size),
            FMath::FloorToInt(Position.Y / Size), FMath::FloorToInt(Position.Z / Size));
    }

    bool SameSettings(const FMD_ContaminationSolidSettings& A,
        const FMD_ContaminationSolidSettings& B)
    {
        return A.SampleSpacingCm == B.SampleSpacingCm &&
            A.bAllowAdaptiveSpacing == B.bAllowAdaptiveSpacing &&
            A.MaximumAdaptiveSpacingCm == B.MaximumAdaptiveSpacingCm &&
            A.MinimumThicknessCm == B.MinimumThicknessCm &&
            A.MaximumThicknessCm == B.MaximumThicknessCm &&
            A.NoiseScaleCm == B.NoiseScaleCm && A.Seed == B.Seed &&
            A.MaximumSurfaceTriangles == B.MaximumSurfaceTriangles &&
            A.TrianglesPerChunk == B.TrianglesPerChunk && A.MaximumChunks == B.MaximumChunks;
    }

    float Thickness(const FVector& MetricPosition, const FMD_ContaminationSolidSettings& Settings)
    {
        FRandomStream Random(Settings.Seed);
        const FVector Shift(Random.FRandRange(-1000.0f, 1000.0f),
            Random.FRandRange(-1000.0f, 1000.0f), Random.FRandRange(-1000.0f, 1000.0f));
        const FVector P = MetricPosition / Settings.NoiseScaleCm + Shift;
        // Broad hardened deposits, smaller lobes and subtle ridges; no per-frame randomness.
        const float Broad = FMath::PerlinNoise3D(P);
        const float Lobes = FMath::PerlinNoise3D(P * 2.73 + FVector(11.2, 31.7, 9.1));
        const float Ridge = 1.0f - 2.0f * FMath::Abs(FMath::PerlinNoise3D(P * 6.17));
        const float Height = FMath::Clamp(0.5f + 0.90f * Broad + 0.30f * Lobes +
            0.10f * (Ridge - 0.5f), 0.0f, 1.0f);
        return FMath::Lerp(Settings.MinimumThicknessCm, Settings.MaximumThicknessCm, Height);
    }

    void MakeOffsets(const TArray<int32>& Counts, TArray<int32>& Offsets)
    {
        Offsets.SetNumUninitialized(Counts.Num() + 1);
        Offsets[0] = 0;
        for (int32 I = 0; I < Counts.Num(); ++I)
        {
            Offsets[I + 1] = Offsets[I] + Counts[I];
        }
    }

    struct FSourceView
    {
        TConstArrayView<FMD_ScrapeMeshUVTriangle> Triangles;
        TConstArrayView<FMD_ScrapeMeshUVCell> Cells;
        int32 ValidCellCount = 0;
    };

    TUniquePtr<FMD_ContaminationSolidGeometry> Build(const FSourceView& Data,
        const FVector& Scale, const FMD_ContaminationSolidSettings& Settings, FString& Error)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Build);
        // Coarse phases retain balanced events on every early-return path without per-cell tracing.
#if CPUPROFILERTRACE_ENABLED
        TOptional<FCpuProfilerTrace::FDynamicEventScope> BuildPhase;
        auto SetBuildPhase = [&](const TCHAR* Name) { BuildPhase.Emplace(Name, true, __FILE__, __LINE__); };
#else
        auto SetBuildPhase = [](const TCHAR*) {};
#endif
        SetBuildPhase(TEXT("MD_Solid_Build_SourceTopology"));
        const FVector AbsoluteScale = Scale.GetAbs();
        FDynamicMesh3 Mesh;
        Mesh.EnableTriangleGroups();
        Mesh.EnableVertexNormals(FVector3f::UnitZ());
        TMap<FIntVector, TArray<int32>> WeldBuckets;
        TArray<double> VertexWeldRadii;
        TArray<double> SourceWeldRadii;
        SourceWeldRadii.Init(0.0, Data.Triangles.Num());
        TArray<int32> SourceOrder;
        int32 DegenerateSourceCount = 0;
        int32 IsolatedSourceCount = 0;
        int32 RecoveredSourceCount = 0;
        auto FindOrAppend = [&](const FVector& Position, const FVector& Normal, const double RequestedRadius)
        {
            const FVector Metric = Position * AbsoluteScale;
            const FIntVector Key = Bucket(Metric, WeldToleranceCm);
            int32 Nearest = INDEX_NONE;
            double NearestSquared = TNumericLimits<double>::Max();
            for (int32 X = -1; X <= 1; ++X)
            for (int32 Y = -1; Y <= 1; ++Y)
            for (int32 Z = -1; Z <= 1; ++Z)
            {
                if (const TArray<int32>* Candidates = WeldBuckets.Find(Key + FIntVector(X, Y, Z)))
                {
                    for (const int32 Vertex : *Candidates)
                    {
                        const double Distance = ((FVector(Mesh.GetVertex(Vertex)) - Position) * AbsoluteScale).SquaredLength();
                        if (Distance <= FMath::Square(FMath::Min(RequestedRadius, VertexWeldRadii[Vertex])) &&
                            Distance < NearestSquared &&
                            FVector::DotProduct(FVector(Mesh.GetVertexNormal(Vertex)), Normal) >= NormalWeldDot)
                        {
                            Nearest = Vertex;
                            NearestSquared = Distance;
                        }
                    }
                }
            }
            if (Nearest != INDEX_NONE) return Nearest;
            const int32 Vertex = Mesh.AppendVertex(FVector3d(Position));
            Mesh.SetVertexNormal(Vertex, FVector3f(Normal));
            VertexWeldRadii.SetNumZeroed(Vertex + 1, EAllowShrinking::No);
            VertexWeldRadii[Vertex] = RequestedRadius;
            WeldBuckets.FindOrAdd(Key).Add(Vertex);
            return Vertex;
        };

        // Baked clipping can leave microscopic wedges between otherwise broad faces. Register
        // their corners first with a radius smaller than their altitude, so a broad neighbor
        // cannot weld the two distinct sides together before the wedge is visited.
        for (int32 I = 0; I < Data.Triangles.Num(); ++I)
        {
            const auto& Source = Data.Triangles[I];
            if (!Source.bCleanable || Source.CellIndices.IsEmpty()) continue;
            if (Source.A.ContainsNaN() || Source.B.ContainsNaN() || Source.C.ContainsNaN() ||
                Source.NormalLocal.ContainsNaN() || Source.NormalLocal.IsNearlyZero())
            {
                Error = TEXT("Non-finite or degenerate cleanable source geometry.");
                return nullptr;
            }
            const FVector AB = (Source.B - Source.A) * AbsoluteScale;
            const FVector AC = (Source.C - Source.A) * AbsoluteScale;
            const double LongestSquared = FMath::Max3(AB.SquaredLength(), AC.SquaredLength(),
                ((Source.C - Source.B) * AbsoluteScale).SquaredLength());
            const double TwiceArea = FVector::CrossProduct(AB, AC).Length();
            if (!FMath::IsFinite(TwiceArea) || !FMath::IsFinite(LongestSquared))
            {
                Error = TEXT("Solid source metric exceeds finite geometry bounds.");
                return nullptr;
            }
            if (TwiceArea <= 1.e-10 || LongestSquared <= UE_DOUBLE_SMALL_NUMBER)
            {
                ++DegenerateSourceCount;
                continue;
            }
            SourceWeldRadii[I] = FMath::Clamp(0.2 * TwiceArea / FMath::Sqrt(LongestSquared), 0.0, WeldToleranceCm);
            SourceOrder.Add(I);
        }
        SourceOrder.Sort([&](const int32 A, const int32 B)
        {
            return SourceWeldRadii[A] == SourceWeldRadii[B] ? A < B : SourceWeldRadii[A] < SourceWeldRadii[B];
        });
        for (const int32 I : SourceOrder)
        {
            const auto& Source = Data.Triangles[I];
            const FVector Normal = Source.NormalLocal.GetSafeNormal();
            FindOrAppend(Source.A, Normal, SourceWeldRadii[I]);
            FindOrAppend(Source.B, Normal, SourceWeldRadii[I]);
            FindOrAppend(Source.C, Normal, SourceWeldRadii[I]);
        }

        // Include every baked partial-edge portal before subdivision. A shared short edge and
        // its longer neighbor therefore start with identical endpoints instead of a T-junction.
        for (int32 SourceIndex = 0; SourceIndex < Data.Triangles.Num(); ++SourceIndex)
        {
            const FMD_ScrapeMeshUVTriangle& Source = Data.Triangles[SourceIndex];
            if (SourceWeldRadii[SourceIndex] <= 0.0) continue;
            const double SourceWeldRadius = SourceWeldRadii[SourceIndex];
            const FVector Corners[3] = {Source.A, Source.B, Source.C};
            TArray<double, TInlineAllocator<16>> EdgeParameters[3];
            for (int32 Edge = 0; Edge < 3; ++Edge) EdgeParameters[Edge].Add(0.0);
            for (const FMD_ScrapeMeshUVNeighbor& Neighbor : Source.Neighbors)
            {
                if (!Data.Triangles.IsValidIndex(Neighbor.TriangleIndex) ||
                    !Data.Triangles[Neighbor.TriangleIndex].bCleanable) continue;
                const FVector Endpoints[2] = {Neighbor.SharedEdgeA, Neighbor.SharedEdgeB};
                const FVector PortalDelta = (Endpoints[1] - Endpoints[0]) * AbsoluteScale;
                const double PortalSquared = PortalDelta.SquaredLength();
                int32 BestEdge = INDEX_NONE;
                double BestScore = TNumericLimits<double>::Max();
                double BestParameters[2] = {};
                for (int32 Edge = 0; Edge < 3; ++Edge)
                {
                    const FVector Delta = (Corners[(Edge + 1) % 3] - Corners[Edge]) * AbsoluteScale;
                    const double LengthSquared = Delta.SquaredLength();
                    if (LengthSquared <= 0.0) continue;
                    double Parameters[2], DistanceError = 0.0;
                    for (int32 I = 0; I < 2; ++I)
                    {
                        const FVector Relative = (Endpoints[I] - Corners[Edge]) * AbsoluteScale;
                        Parameters[I] = FVector::DotProduct(Relative, Delta) / LengthSquared;
                        DistanceError += (Relative - Delta * FMath::Clamp(Parameters[I], 0.0, 1.0)).SquaredLength();
                    }
                    if (DistanceError > 8.0 * FMath::Square(WeldToleranceCm)) continue;
                    const double Dot = FVector::DotProduct(Delta, PortalDelta);
                    const double AngularError = PortalSquared > 1.e-16 ?
                        FMath::Max(0.0, 1.0 - Dot * Dot / (LengthSquared * PortalSquared)) : 0.0;
                    const double Score = DistanceError + LengthSquared * AngularError;
                    if (Score < BestScore)
                    {
                        BestScore = Score;
                        BestEdge = Edge;
                        BestParameters[0] = Parameters[0];
                        BestParameters[1] = Parameters[1];
                    }
                }
                // A portal is a segment belonging to one edge. Testing its endpoints
                // independently incorrectly folds an acute triangle's adjacent edge inward.
                if (BestEdge != INDEX_NONE)
                    for (const double T : BestParameters)
                        if (T > 1.e-7 && T < 1.0 - 1.e-7) EdgeParameters[BestEdge].Add(T);
            }
            TArray<int32, TInlineAllocator<16>> Boundary;
            for (int32 Edge = 0; Edge < 3; ++Edge)
            {
                const FVector A = Corners[Edge];
                const FVector Delta = Corners[(Edge + 1) % 3] - A;
                const double LengthSquared = (Delta * AbsoluteScale).SquaredLength();
                if (LengthSquared <= 0.0) continue;
                EdgeParameters[Edge].Sort();
                double Previous = -1.0;
                for (const double T : EdgeParameters[Edge])
                {
                    if (Previous >= 0.0 && FMath::Square(T - Previous) * LengthSquared <=
                        FMath::Square(SourceWeldRadius)) continue;
                    const FVector Position = A + Delta * T;
                    double PointRadius = SourceWeldRadius;
                    if (T > 1.e-7)
                    {
                        double ClearanceSquared = TNumericLimits<double>::Max();
                        for (int32 Other = 0; Other < 3; ++Other)
                        {
                            if (Other == Edge) continue;
                            const FVector OtherDelta = (Corners[(Other + 1) % 3] - Corners[Other]) * AbsoluteScale;
                            const FVector Relative = (Position - Corners[Other]) * AbsoluteScale;
                            const double OtherSquared = OtherDelta.SquaredLength();
                            const double OtherT = OtherSquared > 0.0 ?
                                FMath::Clamp(FVector::DotProduct(Relative, OtherDelta) / OtherSquared, 0.0, 1.0) : 0.0;
                            ClearanceSquared = FMath::Min(ClearanceSquared,
                                (Relative - OtherDelta * OtherT).SquaredLength());
                        }
                        PointRadius = FMath::Clamp(0.2 * FMath::Sqrt(ClearanceSquared), 0.0, SourceWeldRadius);
                    }
                    const int32 Vertex = FindOrAppend(Position, Source.NormalLocal.GetSafeNormal(), PointRadius);
                    if (Boundary.IsEmpty() || Boundary.Last() != Vertex) Boundary.Add(Vertex);
                    Previous = T;
                }
            }
            if (Boundary.Num() > 2 && Boundary.Last() == Boundary[0]) Boundary.Pop();
            if (Boundary.Num() < 3) continue;
            bool bNonManifoldJoin = false;
            for (int32 I = 0; I < Boundary.Num(); ++I)
            {
                const int32 Existing = Mesh.FindEdge(Boundary[I], Boundary[(I + 1) % Boundary.Num()]);
                if (Existing < 0) continue;
                bNonManifoldJoin |= !Mesh.IsBoundaryEdge(Existing);
                const FIndex3i Other = Mesh.GetTriangle(Mesh.GetEdgeT(Existing).A);
                for (int32 Corner = 0; Corner < 3; ++Corner)
                    bNonManifoldJoin |= Other[Corner] == Boundary[I] &&
                        Other[(Corner + 1) % 3] == Boundary[(I + 1) % Boundary.Num()];
            }
            if (bNonManifoldJoin)
            {
                // Some exposed fragments overlap an already full or identically directed
                // edge. SplitEdge requires opposite directions on its two incident faces.
                // Preserve these small fragments as separate closed-to-wall patches.
                ++IsolatedSourceCount;
                for (int32& Vertex : Boundary)
                {
                    const int32 Separate = Mesh.AppendVertex(Mesh.GetVertex(Vertex));
                    Mesh.SetVertexNormal(Separate, FVector3f(Source.NormalLocal));
                    Vertex = Separate;
                }
            }
            auto Append = [&](const int32 A, const int32 B, const int32 C)
            {
                if (Mesh.TriangleCount() >= Settings.MaximumSurfaceTriangles)
                {
                    Error = FString::Printf(TEXT("Solid initial topology budget: source=%d original=%d boundary=%d triangles=%d limit=%d."),
                        SourceIndex, Source.SourceTriangleId, Boundary.Num(), Mesh.TriangleCount(), Settings.MaximumSurfaceTriangles);
                    return false;
                }
                const int32 Added = Mesh.AppendTriangle(A, B, C, SourceIndex);
                if (Added >= 0) return true;
                FString EdgeSources;
                const int32 Vertices[3] = {A, B, C};
                for (int32 Edge = 0; Edge < 3; ++Edge)
                {
                    const int32 EdgeId = Mesh.FindEdge(Vertices[Edge], Vertices[(Edge + 1) % 3]);
                    if (EdgeId < 0) continue;
                    const FIndex2i Existing = Mesh.GetEdgeT(EdgeId);
                    EdgeSources += FString::Printf(TEXT(" e%d=(%d,%d)"), Edge,
                        Mesh.IsTriangle(Existing.A) ? Mesh.GetTriangleGroup(Existing.A) : INDEX_NONE,
                        Mesh.IsTriangle(Existing.B) ? Mesh.GetTriangleGroup(Existing.B) : INDEX_NONE);
                }
                Error = FString::Printf(TEXT("Solid initial topology rejected: source=%d original=%d boundary=%d result=%d triangles=%d vertices=(%d,%d,%d)%s."),
                    SourceIndex, Source.SourceTriangleId, Boundary.Num(), Added, Mesh.TriangleCount(), A, B, C, *EdgeSources);
                return false;
            };
            if (Boundary.Num() == 3)
            {
                if (!Append(Boundary[0], Boundary[1], Boundary[2]))
                {
                    return nullptr;
                }
            }
            else
            {
                const int32 Center = Mesh.AppendVertex((Source.A + Source.B + Source.C) / 3.0);
                Mesh.SetVertexNormal(Center, FVector3f(Source.NormalLocal));
                for (int32 I = 0; I < Boundary.Num(); ++I)
                {
                    if (!Append(Boundary[I], Boundary[(I + 1) % Boundary.Num()], Center))
                    {
                        return nullptr;
                    }
                }
            }
        }
        // Audit all usable authored fragments before subdivision. Extremely short but
        // positive edges must never disappear through a weld or portal simplification.
        // A missing fragment is restored with its exact three original positions and
        // private IDs; this is complete source geometry, not an alias to another face.
        TBitArray<> SourcePresent(false, Data.Triangles.Num());
        for (const int32 Triangle : Mesh.TriangleIndicesItr()) SourcePresent[Mesh.GetTriangleGroup(Triangle)] = true;
        for (int32 SourceIndex = 0; SourceIndex < Data.Triangles.Num(); ++SourceIndex)
        {
            if (SourceWeldRadii[SourceIndex] <= 0.0 || SourcePresent[SourceIndex]) continue;
            if (Mesh.TriangleCount() >= Settings.MaximumSurfaceTriangles)
            {
                Error = FString::Printf(TEXT("Solid source recovery exceeds initial topology budget: source=%d triangles=%d limit=%d."),
                    SourceIndex, Mesh.TriangleCount(), Settings.MaximumSurfaceTriangles);
                return nullptr;
            }
            const FMD_ScrapeMeshUVTriangle& Source = Data.Triangles[SourceIndex];
            const int32 A = Mesh.AppendVertex(Source.A), B = Mesh.AppendVertex(Source.B), C = Mesh.AppendVertex(Source.C);
            Mesh.SetVertexNormal(A, FVector3f(Source.NormalLocal));
            Mesh.SetVertexNormal(B, FVector3f(Source.NormalLocal));
            Mesh.SetVertexNormal(C, FVector3f(Source.NormalLocal));
            const int32 Added = Mesh.AppendTriangle(A, B, C, SourceIndex);
            if (Added < 0)
            {
                Error = FString::Printf(TEXT("Solid exact source recovery rejected: source=%d result=%d."), SourceIndex, Added);
                return nullptr;
            }
            ++RecoveredSourceCount;
            ++IsolatedSourceCount;
        }
        WeldBuckets.Empty();
        VertexWeldRadii.Empty();
        SourceOrder.Empty();
        for (int32 Vertex = 0; Vertex < Mesh.MaxVertexID(); ++Vertex)
            if (Mesh.IsVertex(Vertex) && !Mesh.IsReferencedVertex(Vertex)) Mesh.RemoveVertex(Vertex);
        Mesh.CompactInPlace();
        if (Mesh.TriangleCount() == 0)
        {
            Error = TEXT("Profile has no cleanable triangles with cells.");
            return nullptr;
        }

        // Split the globally longest edge first. FIFO splitting can repeatedly introduce
        // longer diagonals in thin baked fragments and spends the budget unevenly.
        // Every split preserves the whole original surface, so a bounded early stop is a
        // complete coarser shell, never a truncated wall.
        SetBuildPhase(TEXT("MD_Solid_Build_Subdivision"));
        struct FPendingEdge { int32 Edge; double LengthSquared; };
        auto LongerFirst = [](const FPendingEdge& A, const FPendingEdge& B)
        {
            return A.LengthSquared == B.LengthSquared ? A.Edge < B.Edge : A.LengthSquared > B.LengthSquared;
        };
        auto EdgeLengthSquared = [&](const int32 Edge)
        {
            const FIndex2i Ends = Mesh.GetEdgeV(Edge);
            return ((FVector(Mesh.GetVertex(Ends.A)) - FVector(Mesh.GetVertex(Ends.B))) *
                AbsoluteScale).SquaredLength();
        };
        const double RequestedEdgeSquared = FMath::Square(double(Settings.SampleSpacingCm));
        TArray<FPendingEdge> PendingEdges;
        PendingEdges.Reserve(Mesh.EdgeCount());
        for (const int32 Edge : Mesh.EdgeIndicesItr())
        {
            const double LengthSquared = EdgeLengthSquared(Edge);
            if (LengthSquared > RequestedEdgeSquared) PendingEdges.Add({Edge, LengthSquared});
        }
        PendingEdges.Heapify(LongerFirst);
        float ResolvedSpacing = Settings.SampleSpacingCm;
        while (!PendingEdges.IsEmpty())
        {
            FPendingEdge Pending;
            PendingEdges.HeapPop(Pending, LongerFirst, EAllowShrinking::No);
            const int32 AddedTriangles = Mesh.IsBoundaryEdge(Pending.Edge) ? 1 : 2;
            if (Mesh.TriangleCount() + AddedTriangles > Settings.MaximumSurfaceTriangles)
            {
                const double RequiredSpacing = FMath::Sqrt(Pending.LengthSquared);
                if (!Settings.bAllowAdaptiveSpacing || RequiredSpacing > Settings.MaximumAdaptiveSpacingCm)
                {
                    Error = FString::Printf(
                        TEXT("Solid subdivision budget: triangles=%d limit=%d requested=%.2fcm required=%.2fcm adaptive_limit=%.2fcm."),
                        Mesh.TriangleCount(), Settings.MaximumSurfaceTriangles, Settings.SampleSpacingCm,
                        RequiredSpacing, Settings.bAllowAdaptiveSpacing ? Settings.MaximumAdaptiveSpacingCm : Settings.SampleSpacingCm);
                    return nullptr;
                }
                ResolvedSpacing = float(RequiredSpacing + 0.0001);
                break;
            }
            FDynamicMesh3::FEdgeSplitInfo Split;
            const EMeshResult SplitResult = Mesh.SplitEdge(Pending.Edge, Split);
            if (SplitResult != EMeshResult::Ok)
            {
                Error = FString::Printf(TEXT("Solid shared edge subdivision failed: edge=%d result=%d triangles=%d."),
                    Pending.Edge, int32(SplitResult), Mesh.TriangleCount());
                return nullptr;
            }
            auto QueueEdge = [&](const int32 Edge)
            {
                if (Edge < 0) return;
                const double LengthSquared = EdgeLengthSquared(Edge);
                if (LengthSquared > RequestedEdgeSquared) PendingEdges.HeapPush({Edge, LengthSquared}, LongerFirst);
            };
            QueueEdge(Pending.Edge);
            for (int32 I = 0; I < 3; ++I) QueueEdge(Split.NewEdges[I]);
        }
        PendingEdges.Empty();
        SetBuildPhase(TEXT("MD_Solid_Build_Validate"));
        if (!Mesh.CheckValidity(FDynamicMesh3::FValidityOptions(true, false), EValidityCheckFailMode::ReturnOnly))
        {
            Error = TEXT("Solid subdivision produced inconsistent triangle/edge topology.");
            return nullptr;
        }

        SetBuildPhase(TEXT("MD_Solid_Build_TopologyPacking"));
        TUniquePtr<FMD_ContaminationSolidGeometry> Result = MakeUnique<FMD_ContaminationSolidGeometry>();
        Result->DegenerateSourceCount = DegenerateSourceCount;
        Result->IsolatedSourceCount = IsolatedSourceCount;
        Result->RecoveredSourceCount = RecoveredSourceCount;
        Result->SampleSpacingCm = ResolvedSpacing;
        Result->MaximumThicknessCm = Settings.MaximumThicknessCm;
        Result->CarrierOnlySources.Init(false, Data.Triangles.Num());
        for (int32 Source = 0; Source < Data.Triangles.Num(); ++Source)
            Result->CarrierOnlySources[Source] = SourceWeldRadii[Source] <= 0.0 &&
                Data.Triangles[Source].bCleanable && !Data.Triangles[Source].CellIndices.IsEmpty();
        Result->Vertices.SetNum(Mesh.MaxVertexID());
        TArray<TArray<int32>> SourceVertices;
        SourceVertices.SetNum(Data.Triangles.Num());
        TArray<FVector> NormalSums;
        NormalSums.Init(FVector::ZeroVector, Mesh.MaxVertexID());
        struct FSpatialTriangle { uint32 Morton; int32 Triangle; };
        struct FSpatialBucket
        {
            TArray<int32> Batches;
            TArray<FSpatialTriangle> Triangles;
        };
        TMap<FIntVector, FSpatialBucket> SpatialChunks;
        const double ChunkWidth = ResolvedSpacing * FMath::Sqrt(Settings.TrianglesPerChunk * 0.4);
        TArray<int32> MeshToResult;
        MeshToResult.Init(INDEX_NONE, Mesh.MaxTriangleID());
        for (const int32 TriangleIndex : Mesh.TriangleIndicesItr())
        {
            const FIndex3i Indices = Mesh.GetTriangle(TriangleIndex);
            const int32 SourceIndex = Mesh.GetTriangleGroup(TriangleIndex);
            const FMD_ScrapeMeshUVTriangle& Source = Data.Triangles[SourceIndex];
            FMD_ContaminationSolidTriangle Triangle;
            Triangle.Vertices = FIntVector(Indices.A, Indices.B, Indices.C);
            Triangle.SourceTriangle = SourceIndex;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                SourceVertices[SourceIndex].Add(Indices[Corner]);
                FVector Barycentric;
                if (!FMD_ScrapeMeshUVGeometry::TryCalculateBarycentric(Source,
                    FVector(Mesh.GetVertex(Indices[Corner])), Barycentric))
                {
                    Error = TEXT("Cannot recover canonical UV for a solid-shell vertex.");
                    return nullptr;
                }
                Triangle.UV[Corner] = FVector2f(Source.UVA * Barycentric.X +
                    Source.UVB * Barycentric.Y + Source.UVC * Barycentric.Z);
                NormalSums[Indices[Corner]] += Source.NormalLocal;
            }
            // UE uses left-handed triangle normals: Cross(C-A, B-A), as in
            // VectorUtil::NormalDirection. Normalize only derived topology and UVs.
            const FVector WindingNormal = FVector::CrossProduct(
                FVector(Mesh.GetVertex(Indices.C) - Mesh.GetVertex(Indices.A)),
                FVector(Mesh.GetVertex(Indices.B) - Mesh.GetVertex(Indices.A)));
            if (FVector::DotProduct(WindingNormal, Source.NormalLocal) < 0.0)
            {
                Swap(Triangle.Vertices.Y, Triangle.Vertices.Z);
                Swap(Triangle.UV[1], Triangle.UV[2]);
            }
            const FVector Centroid = FVector(Mesh.GetTriCentroid(TriangleIndex)) * AbsoluteScale;
            const FIntVector Key = Bucket(Centroid, ChunkWidth);
            FSpatialBucket& SpatialBucket = SpatialChunks.FindOrAdd(Key);
            TArray<int32>& Batches = SpatialBucket.Batches;
            if (Batches.IsEmpty() || Result->Chunks[Batches.Last()].Triangles.Num() >= Settings.TrianglesPerChunk)
            {
                if (Result->Chunks.Num() >= Settings.MaximumChunks)
                {
                    Error = FString::Printf(TEXT("Solid shell exceeds the %d chunk budget."), Settings.MaximumChunks);
                    return nullptr;
                }
                Batches.Add(Result->Chunks.AddDefaulted());
            }
            Triangle.ChunkIndex = Batches.Last();
            Triangle.LocalTriangleIndex = Result->Chunks[Triangle.ChunkIndex].Triangles.Num();
            const int32 ResultIndex = Result->Triangles.Add(Triangle);
            MeshToResult[TriangleIndex] = ResultIndex;
            Result->Chunks[Triangle.ChunkIndex].Triangles.Add(ResultIndex);
            // Quantization is only a sorting key: no geometry or sampling position changes.
            // Ten bits per axis resolve at least 1/1024 of a bucket. Morton ordering
            // retains locality on arbitrary wall planes, unlike a single-axis stripe sort.
            const FVector Fraction = Centroid / ChunkWidth - FVector(Key);
            const FIntVector Quantized(
                FMath::Clamp(FMath::FloorToInt(Fraction.X * 1024.0), 0, 1023),
                FMath::Clamp(FMath::FloorToInt(Fraction.Y * 1024.0), 0, 1023),
                FMath::Clamp(FMath::FloorToInt(Fraction.Z * 1024.0), 0, 1023));
            const uint32 Morton = FMath::MortonCode3(uint32(Quantized.X)) |
                (FMath::MortonCode3(uint32(Quantized.Y)) << 1) |
                (FMath::MortonCode3(uint32(Quantized.Z)) << 2);
            SpatialBucket.Triangles.Add({Morton, ResultIndex});
        }
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_PackChunks);
            // Preserve original global triangle/vertex/sample order and the exact existing
            // bucket/chunk count. Only assign nearby faces to the same bounded upload buffer.
            for (auto& Entry : SpatialChunks)
            {
                FSpatialBucket& SpatialBucket = Entry.Value;
                SpatialBucket.Triangles.Sort([](const FSpatialTriangle& A, const FSpatialTriangle& B)
                {
                    return A.Morton != B.Morton ? A.Morton < B.Morton : A.Triangle < B.Triangle;
                });
                for (const int32 Chunk : SpatialBucket.Batches) Result->Chunks[Chunk].Triangles.Reset();
                for (int32 I = 0; I < SpatialBucket.Triangles.Num(); ++I)
                {
                    const int32 TriangleIndex = SpatialBucket.Triangles[I].Triangle;
                    FMD_ContaminationSolidTriangle& Triangle = Result->Triangles[TriangleIndex];
                    Triangle.ChunkIndex = SpatialBucket.Batches[I / Settings.TrianglesPerChunk];
                    auto& ChunkTriangles = Result->Chunks[Triangle.ChunkIndex].Triangles;
                    Triangle.LocalTriangleIndex = ChunkTriangles.Num();
                    ChunkTriangles.Add(TriangleIndex);
                }
            }
        }
        SpatialChunks.Empty();
        for (TArray<int32>& SourceList : SourceVertices)
        {
            SourceList.Sort();
            SourceList.SetNum(Algo::Unique(SourceList), EAllowShrinking::No);
        }
        TMap<FIntVector, TArray<int32>> VertexBuckets;
        for (const int32 VertexIndex : Mesh.VertexIndicesItr())
        {
            FMD_ContaminationSolidVertex& Vertex = Result->Vertices[VertexIndex];
            const FVector Position = FVector(Mesh.GetVertex(VertexIndex));
            const FVector Normal = NormalSums[VertexIndex].GetSafeNormal();
            const FVector MetricNormal = (Normal / Scale).GetSafeNormal();
            Vertex.Position = FVector3d(Position);
            Vertex.Normal = FVector3f(Normal);
            Vertex.OffsetPerCm = FVector3f(MetricNormal / Scale);
            Vertex.ThicknessCm = Thickness(Position * AbsoluteScale, Settings);
            VertexBuckets.FindOrAdd(Bucket(Position * AbsoluteScale, ResolvedSpacing)).Add(VertexIndex);
        }
        for (const int32 Edge : Mesh.EdgeIndicesItr())
        {
            if (!Mesh.IsBoundaryEdge(Edge)) continue;
            const FIndex2i Ends = Mesh.GetEdgeV(Edge);
            const int32 TriangleIndex = MeshToResult[Mesh.GetEdgeT(Edge).A];
            const FMD_ContaminationSolidTriangle& Triangle = Result->Triangles[TriangleIndex];
            FIntPoint Oriented(Ends.A, Ends.B);
            for (int32 I = 0; I < 3; ++I)
            {
                if (Triangle.Vertices[I] == Ends.B && Triangle.Vertices[(I + 1) % 3] == Ends.A)
                    Oriented = FIntPoint(Ends.B, Ends.A);
            }
            Result->Chunks[Triangle.ChunkIndex].BoundaryEdges.Add(Oriented);
            ++Result->BoundaryEdgeCount;
        }

        for (FMD_ContaminationSolidChunk& Chunk : Result->Chunks)
        {
            for (const int32 TriangleIndex : Chunk.Triangles)
                for (int32 Corner = 0; Corner < 3; ++Corner)
                    Chunk.TopVertices.Add(Result->Triangles[TriangleIndex].Vertices[Corner]);
            for (const FIntPoint& Edge : Chunk.BoundaryEdges)
            {
                Chunk.BottomVertices.Add(Edge.X);
                Chunk.BottomVertices.Add(Edge.Y);
            }
            Chunk.TopVertices.Sort();
            Chunk.TopVertices.SetNum(Algo::Unique(Chunk.TopVertices), EAllowShrinking::No);
            const float LowHeight = -FMD_ContaminationSolidGeometry::WallInsetCm;
            const float HighHeight = Settings.MaximumThicknessCm * 2.0f - FMD_ContaminationSolidGeometry::WallInsetCm;
            for (const int32 VertexIndex : Chunk.TopVertices)
            {
                const auto& Vertex = Result->Vertices[VertexIndex];
                Chunk.LocalBounds += FVector(Vertex.Position + FVector3d(Vertex.OffsetPerCm) * LowHeight);
                Chunk.LocalBounds += FVector(Vertex.Position + FVector3d(Vertex.OffsetPerCm) * HighHeight);
            }
            // Each skirt quad owns two bottom positions. A bowtie top vertex can have
            // four boundary edges: sharing its vertical edge would become non-manifold.
        }

        SetBuildPhase(TEXT("MD_Solid_Build_CarrierAssignments"));
        struct FAssignment { int32 Vertex; int32 Cell; float Weight; };
        TArray<FAssignment> Assignments;
        Assignments.Reserve(Data.ValidCellCount + Result->Vertices.Num());
        TArray<int32> SampleCounts;
        SampleCounts.Init(0, Result->Vertices.Num());
        TBitArray<> HasPhysicalSample(false, Result->Vertices.Num());
        TMap<FIntVector, TArray<int32>> CellBuckets;
        TUniquePtr<FSparseDynamicPointOctree3> CarrierTree;
        TArray<const FSparsePointOctreeCell*> CarrierQueryScratch;
        auto MetricPosition = [&](const int32 Vertex)
        {
            return Result->Vertices[Vertex].Position * FVector3d(AbsoluteScale);
        };
        auto FindGlobalCarrier = [&](const FVector& Metric)
        {
            // Most machine remnants find a nearby wall vertex. Construct the point tree
            // only for isolated remnants, once per shared build; never during a stroke.
            if (!CarrierTree)
            {
                FBox Bounds(ForceInit);
                for (int32 Vertex = 0; Vertex < Result->Vertices.Num(); ++Vertex)
                    Bounds += FVector(MetricPosition(Vertex));
                CarrierTree = MakeUnique<FSparseDynamicPointOctree3>();
                CarrierTree->RootDimension = FMath::Max(double(ResolvedSpacing), Bounds.GetSize().GetMax() / 4.0);
                CarrierTree->MaxTreeDepth = 8;
                CarrierTree->MaxPointsPerCell = 64;
                for (int32 Vertex = 0; Vertex < Result->Vertices.Num(); ++Vertex)
                    CarrierTree->InsertPoint_DynamicExpand(Vertex, MetricPosition);
            }
            ++Result->GlobalCarrierQueryCount;
            // Any existing vertex is an upper bound on the nearest distance, even if
            // a numerical remnant lies outside the real surface's bounding box.
            const double Radius = (MetricPosition(0) - FVector3d(Metric)).Length() + 1.0;
            return CarrierTree->FindClosestPoint(FVector3d(Metric), Radius,
                [](const int32) { return true; },
                [&](const int32 Vertex) { return (MetricPosition(Vertex) - FVector3d(Metric)).SquaredLength(); },
                &CarrierQueryScratch);
        };
        for (int32 CellIndex = 0; CellIndex < Data.Cells.Num(); ++CellIndex)
        {
            const FMD_ScrapeMeshUVCell& Cell = Data.Cells[CellIndex];
            if (!Cell.IsActive() || !SourceVertices.IsValidIndex(Cell.TriangleIndex)) continue;
            const TArray<int32>& Candidates = SourceVertices[Cell.TriangleIndex];
            const bool bDegenerateAlias = Result->CarrierOnlySources[Cell.TriangleIndex];
            if (Candidates.IsEmpty() && !bDegenerateAlias)
            {
                Error = FString::Printf(TEXT("Non-degenerate cleanable source lost its solid geometry: source=%d cell=%d."),
                    Cell.TriangleIndex, CellIndex);
                return nullptr;
            }
            if (Cell.PositionLocal.ContainsNaN() || !FMath::IsFinite(Cell.AreaCm2))
            {
                Error = FString::Printf(TEXT("Non-finite solid carrier: source=%d cell=%d."), Cell.TriangleIndex, CellIndex);
                return nullptr;
            }
            CellBuckets.FindOrAdd(Bucket(Cell.PositionLocal * AbsoluteScale, ResolvedSpacing)).Add(CellIndex);
            const FVector Metric = Cell.PositionLocal * AbsoluteScale;
            int32 Nearest = INDEX_NONE;
            int32 NearestUnaligned = INDEX_NONE;
            double BestDistance = TNumericLimits<double>::Max();
            double BestUnalignedDistance = TNumericLimits<double>::Max();
            auto Consider = [&](const int32 VertexIndex)
            {
                const double Distance = ((FVector(Result->Vertices[VertexIndex].Position) -
                    Cell.PositionLocal) * AbsoluteScale).SquaredLength();
                if (Distance < BestDistance) { BestDistance = Distance; Nearest = VertexIndex; }
            };
            if (!bDegenerateAlias && Candidates.Num() <= 64)
            {
                for (const int32 VertexIndex : Candidates) Consider(VertexIndex);
            }
            else
            {
                const FIntVector Key = Bucket(Metric, ResolvedSpacing);
                const int32 Radius = bDegenerateAlias ? 2 : 1;
                for (int32 X = -Radius; X <= Radius; ++X)
                for (int32 Y = -Radius; Y <= Radius; ++Y)
                for (int32 Z = -Radius; Z <= Radius; ++Z)
                {
                    if (const TArray<int32>* Nearby = VertexBuckets.Find(Key + FIntVector(X, Y, Z)))
                    {
                        for (const int32 VertexIndex : *Nearby)
                        {
                            const bool bCandidate = bDegenerateAlias ?
                                FVector::DotProduct(FVector(Result->Vertices[VertexIndex].Normal),
                                    Data.Triangles[Cell.TriangleIndex].NormalLocal.GetSafeNormal()) >= NormalWeldDot :
                                Algo::BinarySearch(Candidates, VertexIndex) != INDEX_NONE;
                            if (bCandidate) Consider(VertexIndex);
                            else if (bDegenerateAlias)
                            {
                                const double Distance = (MetricPosition(VertexIndex) - FVector3d(Metric)).SquaredLength();
                                if (Distance < BestUnalignedDistance)
                                {
                                    BestUnalignedDistance = Distance;
                                    NearestUnaligned = VertexIndex;
                                }
                            }
                        }
                    }
                }
            }
            if (bDegenerateAlias && Nearest == INDEX_NONE)
            {
                Nearest = NearestUnaligned != INDEX_NONE ? NearestUnaligned : FindGlobalCarrier(Metric);
                ++Result->UnalignedCarrierCount;
            }
            if (Nearest == INDEX_NONE)
            {
                Error = FString::Printf(TEXT("A cleanable cell could not map to bounded solid geometry: source=%d cell=%d degenerate_alias=%d."),
                    Cell.TriangleIndex, CellIndex, bDegenerateAlias);
                return nullptr;
            }
            if (bDegenerateAlias) ++Result->AliasedCarrierCount;
            // A zero-area fragment has no visible volume. Keep its reverse mapping for
            // lifecycle/dirty-cell completeness, but never let its mass alter a real face.
            Assignments.Add({Nearest, CellIndex, bDegenerateAlias ? 0.0f : float(Cell.AreaCm2 / (0.25 + BestDistance))});
            ++SampleCounts[Nearest];
            if (!bDegenerateAlias) HasPhysicalSample[Nearest] = true;
        }
        // Tiny clipped fragments can contain more geometric vertices than owned cells. Give
        // those vertices a real incident-triangle sample, never an atlas gutter or other face.
        for (const int32 VertexIndex : Mesh.VertexIndicesItr())
        {
            // A machine-epsilon carrier alone must not normalize to 100% of a visible
            // vertex. Always supply a real incident surface cell before normalizing it.
            if (HasPhysicalSample[VertexIndex]) continue;
            int32 Nearest = INDEX_NONE;
            double BestDistance = TNumericLimits<double>::Max();
            TArray<int32, TInlineAllocator<12>> IncidentSources;
            int32 CandidateCount = 0;
            for (const int32 Incident : Mesh.VtxTrianglesItr(VertexIndex))
            {
                const int32 Source = Mesh.GetTriangleGroup(Incident);
                if (!IncidentSources.Contains(Source))
                {
                    IncidentSources.Add(Source);
                    CandidateCount += Data.Triangles[Source].CellIndices.Num();
                }
            }
            auto ConsiderCell = [&](const int32 CellIndex)
            {
                if (!Data.Cells.IsValidIndex(CellIndex) || !Data.Cells[CellIndex].IsActive()) return;
                const double Distance = ((Data.Cells[CellIndex].PositionLocal -
                    FVector(Result->Vertices[VertexIndex].Position)) * AbsoluteScale).SquaredLength();
                if (Distance < BestDistance) { BestDistance = Distance; Nearest = CellIndex; }
            };
            if (CandidateCount <= 64)
            {
                for (const int32 Source : IncidentSources)
                    for (const int32 CellIndex : Data.Triangles[Source].CellIndices) ConsiderCell(CellIndex);
            }
            else
            {
                const FIntVector Key = Bucket(FVector(Result->Vertices[VertexIndex].Position) *
                    AbsoluteScale, ResolvedSpacing);
                for (int32 X = -2; X <= 2; ++X)
                for (int32 Y = -2; Y <= 2; ++Y)
                for (int32 Z = -2; Z <= 2; ++Z)
                {
                    if (const TArray<int32>* Nearby = CellBuckets.Find(Key + FIntVector(X, Y, Z)))
                        for (const int32 CellIndex : *Nearby)
                            if (IncidentSources.Contains(Data.Cells[CellIndex].TriangleIndex)) ConsiderCell(CellIndex);
                }
            }
            if (Nearest != INDEX_NONE)
            {
                Assignments.Add({VertexIndex, Nearest, 1.0f});
                ++SampleCounts[VertexIndex];
            }
        }
        MakeOffsets(SampleCounts, Result->SampleOffsets);
        Result->Samples.SetNumUninitialized(Assignments.Num());
        TArray<int32> WriteOffsets = Result->SampleOffsets;
        TArray<int32> CellCounts;
        CellCounts.Init(0, Data.Cells.Num());
        for (const FAssignment& Assignment : Assignments)
        {
            Result->Samples[WriteOffsets[Assignment.Vertex]++] = {Assignment.Cell, Assignment.Weight};
            ++CellCounts[Assignment.Cell];
        }
        for (int32 VertexIndex = 0; VertexIndex < Result->Vertices.Num(); ++VertexIndex)
        {
            double Sum = 0.0;
            for (int32 I = Result->SampleOffsets[VertexIndex]; I < Result->SampleOffsets[VertexIndex + 1]; ++I)
                Sum += Result->Samples[I].Weight;
            if (Sum <= 0.0) continue;
            for (int32 I = Result->SampleOffsets[VertexIndex]; I < Result->SampleOffsets[VertexIndex + 1]; ++I)
                Result->Samples[I].Weight = float(Result->Samples[I].Weight / Sum);
        }
        MakeOffsets(CellCounts, Result->CellVertexOffsets);
        Result->CellVertices.SetNumUninitialized(Assignments.Num());
        WriteOffsets = Result->CellVertexOffsets;
        for (const FAssignment& Assignment : Assignments)
            Result->CellVertices[WriteOffsets[Assignment.Cell]++] = Assignment.Vertex;

        SetBuildPhase(TEXT("MD_Solid_Build_Adjacency"));
        TArray<TArray<int32>> VertexChunks;
        VertexChunks.SetNum(Result->Vertices.Num());
        TArray<int32> VertexTriangleCounts;
        VertexTriangleCounts.Init(0, Result->Vertices.Num());
        for (const FMD_ContaminationSolidTriangle& Triangle : Result->Triangles)
        {
            for (int32 I = 0; I < 3; ++I)
            {
                VertexChunks[Triangle.Vertices[I]].AddUnique(Triangle.ChunkIndex);
                ++VertexTriangleCounts[Triangle.Vertices[I]];
            }
        }
        TArray<int32> ChunkCounts;
        ChunkCounts.Reserve(VertexChunks.Num());
        for (const TArray<int32>& Indices : VertexChunks) ChunkCounts.Add(Indices.Num());
        MakeOffsets(ChunkCounts, Result->VertexChunkOffsets);
        for (int32 Vertex = 0; Vertex < VertexChunks.Num(); ++Vertex)
            for (const int32 Chunk : VertexChunks[Vertex])
            {
                Result->VertexChunks.Add(Chunk);
                Result->VertexChunkLocalVertices.Add(Algo::BinarySearch(Result->Chunks[Chunk].TopVertices, Vertex));
            }
        TArray<int32> BoundaryCounts;
        BoundaryCounts.Init(0, Result->Vertices.Num());
        for (const auto& Chunk : Result->Chunks)
            for (const FIntPoint& Edge : Chunk.BoundaryEdges)
            {
                ++BoundaryCounts[Edge.X];
                ++BoundaryCounts[Edge.Y];
            }
        MakeOffsets(BoundaryCounts, Result->VertexBoundaryOffsets);
        Result->VertexBoundaries.SetNumUninitialized(Result->BoundaryEdgeCount * 2);
        WriteOffsets = Result->VertexBoundaryOffsets;
        for (int32 ChunkIndex = 0; ChunkIndex < Result->Chunks.Num(); ++ChunkIndex)
        {
            const auto& Edges = Result->Chunks[ChunkIndex].BoundaryEdges;
            for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
            {
                Result->VertexBoundaries[WriteOffsets[Edges[EdgeIndex].X]++] = FIntPoint(ChunkIndex, EdgeIndex);
                Result->VertexBoundaries[WriteOffsets[Edges[EdgeIndex].Y]++] = FIntPoint(ChunkIndex, EdgeIndex);
            }
        }
        MakeOffsets(VertexTriangleCounts, Result->VertexTriangleOffsets);
        Result->VertexTriangles.SetNumUninitialized(Result->Triangles.Num() * 3);
        WriteOffsets = Result->VertexTriangleOffsets;
        for (int32 I = 0; I < Result->Triangles.Num(); ++I)
            for (int32 Corner = 0; Corner < 3; ++Corner)
                Result->VertexTriangles[WriteOffsets[Result->Triangles[I].Vertices[Corner]]++] = I;
        return Result;
    }
}

namespace MD_ContaminationSolidGeometryPrivate
{
    bool NormalizeSettings(const FSourceView& Data, const FVector& WallScale,
        const FMD_ContaminationSolidSettings& InSettings,
        FMD_ContaminationSolidSettings& Settings, FString& OutError)
    {
        OutError.Reset();
        if (WallScale.ContainsNaN() || WallScale.GetAbs().GetMin() < 0.001 ||
            WallScale.GetAbs().GetMax() > 1000.0 || Data.Cells.Num() > 4194304 ||
            Data.Triangles.Num() > 300000 || !FMath::IsFinite(InSettings.SampleSpacingCm) ||
            !FMath::IsFinite(InSettings.MinimumThicknessCm) || !FMath::IsFinite(InSettings.MaximumThicknessCm) ||
            !FMath::IsFinite(InSettings.NoiseScaleCm) ||
            !FMath::IsFinite(InSettings.MaximumAdaptiveSpacingCm))
        {
            OutError = TEXT("Solid shell settings, wall scale or source payload exceed finite safety bounds.");
            return false;
        }
        Settings = InSettings;
        Settings.SampleSpacingCm = FMath::Clamp(Settings.SampleSpacingCm, 2.0f, 12.0f);
        Settings.MaximumAdaptiveSpacingCm = FMath::Clamp(Settings.MaximumAdaptiveSpacingCm, Settings.SampleSpacingCm, 12.0f);
        Settings.MinimumThicknessCm = FMath::Clamp(Settings.MinimumThicknessCm, 0.1f, 20.0f);
        Settings.MaximumThicknessCm = FMath::Clamp(Settings.MaximumThicknessCm, Settings.MinimumThicknessCm, 20.0f);
        Settings.NoiseScaleCm = FMath::Clamp(Settings.NoiseScaleCm, 4.0f, 200.0f);
        Settings.MaximumSurfaceTriangles = FMath::Clamp(Settings.MaximumSurfaceTriangles, 1000, 300000);
        Settings.TrianglesPerChunk = FMath::Clamp(Settings.TrianglesPerChunk, 256, 8192);
        Settings.MaximumChunks = FMath::Clamp(Settings.MaximumChunks, 1, 256);
        return true;
    }

    FSourceView View(const UMD_ScrapeMeshUVData& Data)
    {
        check(IsInGameThread());
        return {MakeArrayView(Data.Triangles), MakeArrayView(Data.Cells), Data.ValidCellCount};
    }

    TSharedPtr<const FMD_ContaminationSolidGeometry> FindCached(const UMD_ScrapeMeshUVData& Data,
        const FVector& Scale, const FMD_ContaminationSolidSettings& Settings)
    {
        check(IsInGameThread());
        for (int32 I = Cache.Num() - 1; I >= 0; --I)
        {
            if (!Cache[I].Profile.IsValid() || !Cache[I].Geometry.IsValid())
            {
                Cache.RemoveAtSwap(I);
                continue;
            }
            const FCacheEntry& Entry = Cache[I];
            if (Entry.Profile.Get() == &Data && Entry.LayoutId == Data.LayoutId &&
                Entry.Scale.Equals(Scale, 1.e-6) && SameSettings(Entry.Settings, Settings))
                return Entry.Geometry.Pin();
        }
        return nullptr;
    }
}

TSharedPtr<const FMD_ContaminationSolidGeometry> FMD_ContaminationSolidGeometry::FindOrBuild(
    const UMD_ScrapeMeshUVData& Data, const FVector& WallScale,
    const FMD_ContaminationSolidSettings& InSettings, FString& OutError)
{
    using namespace MD_ContaminationSolidGeometryPrivate;
    check(IsInGameThread());
    const FSourceView Source = View(Data);
    FMD_ContaminationSolidSettings Settings;
    if (!NormalizeSettings(Source, WallScale, InSettings, Settings, OutError)) return nullptr;
    if (const auto Existing = FindCached(Data, WallScale, Settings)) return Existing;
    TUniquePtr<FMD_ContaminationSolidGeometry> Built = Build(Source, WallScale, Settings, OutError);
    TSharedPtr<const FMD_ContaminationSolidGeometry> Geometry;
    if (Built)
    {
        Geometry = MakeShareable(Built.Release());
        Cache.Add({&Data, Data.LayoutId, WallScale, Settings, Geometry});
    }
    return Geometry;
}

namespace MD_ContaminationSolidGeometryPrivate
{
    // Game-thread-only accounting counts a geometry once across overlapping worlds.
    TMap<const FMD_ContaminationSolidGeometry*, int32> PrewarmPinCounts;
    int64 PrewarmRetainedBytes = 0;
}

struct FMD_ContaminationSolidPrewarmBatch::FImpl
{
    struct FBuildJob
    {
        MD_ContaminationSolidGeometryPrivate::FSourceView Source;
        FVector Scale;
        FMD_ContaminationSolidSettings Settings;
        TUniquePtr<FMD_ContaminationSolidGeometry> Result;
        FString Error;
    };
    struct FPrepared
    {
        const UMD_ScrapeMeshUVData* Profile = nullptr;
        FGuid LayoutId;
        TUniquePtr<FBuildJob> Job;

        bool IsCurrent() const
        {
            check(IsInGameThread());
            const auto& Source = Job->Source;
            return IsValid(Profile) && Profile->LayoutId == LayoutId &&
                Profile->Triangles.GetData() == Source.Triangles.GetData() &&
                Profile->Triangles.Num() == Source.Triangles.Num() &&
                Profile->Cells.GetData() == Source.Cells.GetData() &&
                Profile->Cells.Num() == Source.Cells.Num() &&
                Profile->ValidCellCount == Source.ValidCellCount && Profile->ValidateForRuntime();
        }
    };
    TArray<FPrepared> Prepared;
    TArray<TStrongObjectPtr<UMD_ScrapeMeshUVData>> ProfilePins;
    TArray<TSharedPtr<const FMD_ContaminationSolidGeometry>> GeometryPins;
    FMD_ContaminationSolidPrewarmStats Stats;
    bool bExecuted = false;

    void PinProfile(const UMD_ScrapeMeshUVData& Data)
    {
        check(IsInGameThread());
        for (const auto& Pin : ProfilePins) if (Pin.Get() == &Data) return;
        // GC ownership only; every calculation continues to use a const input view.
        ProfilePins.Emplace(const_cast<UMD_ScrapeMeshUVData*>(&Data));
    }
    void PinGeometry(const TSharedPtr<const FMD_ContaminationSolidGeometry>& Geometry)
    {
        using namespace MD_ContaminationSolidGeometryPrivate;
        check(IsInGameThread());
        if (!GeometryPins.Contains(Geometry))
        {
            GeometryPins.Add(Geometry);
            Stats.RetainedGeometryBytes += Geometry->GetAllocatedBytes();
            int32& PinCount = PrewarmPinCounts.FindOrAdd(Geometry.Get());
            if (PinCount++ == 0) PrewarmRetainedBytes += Geometry->GetAllocatedBytes();
        }
        TRACE_COUNTER_SET(MD_SolidPrewarm_RetainedGeometryBytes, PrewarmRetainedBytes);
        TRACE_COUNTER_SET(MD_SolidPrewarm_RetainedGeometries, PrewarmPinCounts.Num());
    }
};

FMD_ContaminationSolidPrewarmBatch::FMD_ContaminationSolidPrewarmBatch() : Impl(MakeUnique<FImpl>())
{
    check(IsInGameThread());
}
FMD_ContaminationSolidPrewarmBatch::~FMD_ContaminationSolidPrewarmBatch()
{
    Release();
}

bool FMD_ContaminationSolidPrewarmBatch::Add(const UMD_ScrapeMeshUVData& Data,
    const FVector& WallScale, const FMD_ContaminationSolidSettings& InSettings, FString& OutError)
{
    using namespace MD_ContaminationSolidGeometryPrivate;
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Prewarm_Prepare);
    ++Impl->Stats.Candidates;
    OutError.Reset();
    FMD_ContaminationSolidSettings Settings;
    const FSourceView Source = View(Data);
    if (Impl->bExecuted || !NormalizeSettings(Source, WallScale, InSettings, Settings, OutError) ||
        !Data.ValidateForRuntime(&OutError))
    {
        ++Impl->Stats.Rejected;
        if (OutError.IsEmpty()) OutError = TEXT("Prewarm batch already executed or source is invalid.");
        return false;
    }
    Impl->PinProfile(Data);
    if (const auto Existing = FindCached(Data, WallScale, Settings))
    {
        ++Impl->Stats.CacheHits;
        Impl->PinGeometry(Existing);
        return true;
    }
    for (const auto& Prepared : Impl->Prepared)
    {
        if (Prepared.Profile == &Data && Prepared.LayoutId == Data.LayoutId &&
            Prepared.Job->Scale.Equals(WallScale, 1.e-6) && SameSettings(Prepared.Job->Settings, Settings))
        {
            ++Impl->Stats.Duplicates;
            return true;
        }
    }
    auto& Prepared = Impl->Prepared.Emplace_GetRef();
    Prepared.Profile = &Data;
    Prepared.LayoutId = Data.LayoutId;
    Prepared.Job = MakeUnique<FImpl::FBuildJob>();
    Prepared.Job->Source = Source;
    Prepared.Job->Scale = WallScale;
    Prepared.Job->Settings = Settings;
    ++Impl->Stats.Queued;
    return true;
}

void FMD_ContaminationSolidPrewarmBatch::Execute(const int32 MaximumWorkers)
{
    using namespace MD_ContaminationSolidGeometryPrivate;
    check(IsInGameThread());
    if (Impl->bExecuted) return;
    Impl->bExecuted = true;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Prewarm_Builds);
    const double Started = FPlatformTime::Seconds();
    TArray<FImpl::FBuildJob*> Jobs;
    for (auto& Prepared : Impl->Prepared)
    {
        if (!Prepared.IsCurrent())
        {
            ++Impl->Stats.Invalidated;
            continue;
        }
        if (const auto Existing = FindCached(*Prepared.Profile, Prepared.Job->Scale, Prepared.Job->Settings))
        {
            ++Impl->Stats.CacheHits;
            Impl->PinGeometry(Existing);
            continue;
        }
        Jobs.Add(Prepared.Job.Get());
    }
    const int32 WorkerCount = FMath::Min(Jobs.Num(), FMath::Clamp(MaximumWorkers, 1, 4));
    FThreadSafeCounter NextJob;
    FThreadSafeCounter ActiveJobs;
    FCriticalSection PeakLock;
    int32 PeakWorkers = 0;
    TArray<TFuture<void>> Futures;
    for (int32 Worker = 0; Worker < WorkerCount; ++Worker)
    {
        Futures.Add(Async(EAsyncExecution::ThreadPool, [&Jobs, &NextJob, &ActiveJobs, &PeakLock, &PeakWorkers]()
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Prewarm_Worker);
            for (int32 Index = NextJob.Increment() - 1; Index < Jobs.Num(); Index = NextJob.Increment() - 1)
            {
                auto& Job = *Jobs[Index];
                const int32 Active = ActiveJobs.Increment();
                { FScopeLock Lock(&PeakLock); PeakWorkers = FMath::Max(PeakWorkers, Active); }
                // No UObject, weak/shared pointer, cache, world or asset-manager access here.
                Job.Result = Build(Job.Source, Job.Scale, Job.Settings, Job.Error);
                ActiveJobs.Decrement();
            }
        }));
    }
    if (!Jobs.IsEmpty())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Prewarm_VisualSeams);
        TSet<const UMD_ScrapeMeshUVData*> WarmedProfiles;
        for (const auto& Prepared : Impl->Prepared)
        {
            if (!Jobs.Contains(Prepared.Job.Get()) || !Prepared.IsCurrent() ||
                WarmedProfiles.Contains(Prepared.Profile)) continue;
            // GT-only derived caches; Cells/Triangles (including nested arrays) stay immutable.
            // This overlaps up to four pure geometry workers and remains inside ExecuteSeconds.
            Prepared.Profile->GetVisualDependents();
            WarmedProfiles.Add(Prepared.Profile);
        }
    }
    // Never return or publish a partial batch while any worker still owns input views.
    for (auto& Future : Futures) Future.Wait();
    Impl->Stats.PeakWorkers = PeakWorkers;
    TRACE_COUNTER_SET(MD_SolidPrewarm_PeakWorkers, PeakWorkers);
    for (auto& Prepared : Impl->Prepared)
    {
        if (!Jobs.Contains(Prepared.Job.Get())) continue;
        if (!Prepared.IsCurrent())
        {
            ++Impl->Stats.Invalidated;
            Prepared.Job->Result.Reset();
            continue;
        }
        auto Geometry = FindCached(*Prepared.Profile, Prepared.Job->Scale, Prepared.Job->Settings);
        if (Geometry)
        {
            ++Impl->Stats.CacheHits;
            Prepared.Job->Result.Reset();
        }
        else if (Prepared.Job->Result)
        {
            Geometry = MakeShareable(Prepared.Job->Result.Release());
            Cache.Add({Prepared.Profile, Prepared.LayoutId, Prepared.Job->Scale, Prepared.Job->Settings, Geometry});
            ++Impl->Stats.Built;
        }
        else
        {
            ++Impl->Stats.Failed;
        }
        if (Geometry) Impl->PinGeometry(Geometry);
    }
    Impl->Stats.ExecuteSeconds = FPlatformTime::Seconds() - Started;
}

void FMD_ContaminationSolidPrewarmBatch::Release()
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Prewarm_Release);
    Impl->bExecuted = true;
    using namespace MD_ContaminationSolidGeometryPrivate;
    Impl->Prepared.Empty();
    for (const auto& Geometry : Impl->GeometryPins)
    {
        int32& PinCount = PrewarmPinCounts.FindChecked(Geometry.Get());
        if (--PinCount == 0)
        {
            PrewarmRetainedBytes -= Geometry->GetAllocatedBytes();
            PrewarmPinCounts.Remove(Geometry.Get());
        }
    }
    Impl->GeometryPins.Empty();
    Impl->ProfilePins.Empty();
    Impl->Stats.RetainedGeometryBytes = 0;
    TRACE_COUNTER_SET(MD_SolidPrewarm_RetainedGeometryBytes, PrewarmRetainedBytes);
    TRACE_COUNTER_SET(MD_SolidPrewarm_RetainedGeometries, PrewarmPinCounts.Num());
}

const FMD_ContaminationSolidPrewarmStats& FMD_ContaminationSolidPrewarmBatch::GetStats() const
{
    check(IsInGameThread());
    return Impl->Stats;
}

int64 FMD_ContaminationSolidGeometry::GetAllocatedBytes() const
{
    int64 Bytes = Vertices.GetAllocatedSize() + Triangles.GetAllocatedSize() + Chunks.GetAllocatedSize() +
        SampleOffsets.GetAllocatedSize() + Samples.GetAllocatedSize() + CellVertexOffsets.GetAllocatedSize() +
        CellVertices.GetAllocatedSize() + VertexChunkOffsets.GetAllocatedSize() + VertexChunks.GetAllocatedSize() +
        VertexChunkLocalVertices.GetAllocatedSize() + VertexBoundaryOffsets.GetAllocatedSize() + VertexBoundaries.GetAllocatedSize() +
        VertexTriangleOffsets.GetAllocatedSize() + VertexTriangles.GetAllocatedSize() + CarrierOnlySources.GetAllocatedSize();
    for (const FMD_ContaminationSolidChunk& Chunk : Chunks)
        Bytes += Chunk.Triangles.GetAllocatedSize() + Chunk.BoundaryEdges.GetAllocatedSize() +
            Chunk.TopVertices.GetAllocatedSize() + Chunk.BottomVertices.GetAllocatedSize();
    return Bytes;
}
