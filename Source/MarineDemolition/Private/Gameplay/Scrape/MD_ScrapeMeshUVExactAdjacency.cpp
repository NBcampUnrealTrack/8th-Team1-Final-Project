#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    bool PositionLess(const FVector& A, const FVector& B)
    {
        if (A.X != B.X) return A.X < B.X;
        if (A.Y != B.Y) return A.Y < B.Y;
        return A.Z < B.Z;
    }

    struct FExactEdge
    {
        FVector Low, High;
        int32 Triangle;
        bool bForward;
    };
}

void UMD_ScrapeMeshUVData::BuildExactAdjacencyCache() const
{
    // Immutable profiles are shared by walls. Callers borrow the result on the game thread.
    check(IsInGameThread());
    if (ExactAdjacencyTriangleData == Triangles.GetData() && ExactAdjacencyTriangleCount == Triangles.Num()
        && ExactAdjacencyLayoutId == LayoutId) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ScrapeMeshUV_BuildExactAdjacency);
    ExactAdjacencyOverrides.Empty();
    TArray<FExactEdge> Edges;
    Edges.Reserve(Triangles.Num() * 3);
    for (int32 Index = 0; Index < Triangles.Num(); ++Index)
    {
        const FMD_ScrapeMeshUVTriangle& T = Triangles[Index];
        if (T.A.ContainsNaN() || T.B.ContainsNaN() || T.C.ContainsNaN()) continue;
        const double AreaSquared = FVector::CrossProduct(T.B - T.A, T.C - T.A).SizeSquared();
        if (!FMath::IsFinite(AreaSquared) || AreaSquared <= 0.0) continue;
        const FVector Points[] = {T.A, T.B, T.C};
        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            const FVector& A = Points[Edge];
            const FVector& B = Points[(Edge + 1) % 3];
            if (A == B) continue;
            const bool bForward = PositionLess(A, B);
            Edges.Add({bForward ? A : B, bForward ? B : A, Index, bForward});
        }
    }
    Edges.Sort([](const FExactEdge& A, const FExactEdge& B)
    {
        if (A.Low != B.Low) return PositionLess(A.Low, B.Low);
        if (A.High != B.High) return PositionLess(A.High, B.High);
        return A.Triangle < B.Triangle;
    });
    const auto AddMissing = [&](int32 From, int32 To, const FVector& A, const FVector& B)
    {
        const FMD_ScrapeMeshUVTriangle& T = Triangles[From];
        TArray<FMD_ScrapeMeshUVNeighbor>* Override = ExactAdjacencyOverrides.Find(From);
        const auto& Existing = Override ? *Override : T.Neighbors;
        for (const FMD_ScrapeMeshUVNeighbor& N : Existing)
            if (N.TriangleIndex == To) return;
        if (!Override)
        {
            Override = &ExactAdjacencyOverrides.Add(From, T.Neighbors);
            // Adding one repaired edge must not hide a legacy profile's other full-edge links.
            if (T.Neighbors.IsEmpty())
            {
                const FVector Points[] = {T.A, T.B, T.C};
                for (int32 Edge = 0; Edge < 3; ++Edge)
                    if (Triangles.IsValidIndex(T.AdjacentTriangles[Edge]))
                    {
                        FMD_ScrapeMeshUVNeighbor N;
                        N.TriangleIndex = T.AdjacentTriangles[Edge];
                        N.SharedEdgeA = Points[Edge]; N.SharedEdgeB = Points[(Edge + 1) % 3];
                        Override->Add(N);
                    }
                for (const FMD_ScrapeMeshUVNeighbor& N : *Override)
                    if (N.TriangleIndex == To) return;
            }
        }
        FMD_ScrapeMeshUVNeighbor N;
        N.TriangleIndex = To; N.SharedEdgeA = A; N.SharedEdgeB = B;
        Override->Add(N);
    };
    for (int32 Begin = 0; Begin < Edges.Num();)
    {
        int32 End = Begin + 1;
        while (End < Edges.Num() && Edges[End].Low == Edges[Begin].Low && Edges[End].High == Edges[Begin].High) ++End;
        // Nearby partial candidates can suppress a correct full portal during baking.
        // Exact reversed endpoints establish ownership without welding across real gaps.
        // Count excluded faces too: never choose arbitrarily on a non-manifold edge.
        if (End - Begin == 2)
        {
            const FExactEdge& A = Edges[Begin];
            const FExactEdge& B = Edges[Begin + 1];
            const FMD_ScrapeMeshUVTriangle& TA = Triangles[A.Triangle];
            const FMD_ScrapeMeshUVTriangle& TB = Triangles[B.Triangle];
            if (A.Triangle != B.Triangle && A.bForward != B.bForward && TA.bCleanable && TB.bCleanable
                && TA.SourceTriangleId != INDEX_NONE && TB.SourceTriangleId != INDEX_NONE
                && FVector::DotProduct(TA.NormalLocal, TB.NormalLocal) > -0.999)
            {
                AddMissing(A.Triangle, B.Triangle, A.Low, A.High);
                AddMissing(B.Triangle, A.Triangle, A.Low, A.High);
            }
        }
        Begin = End;
    }
    Edges.Empty();
    // Fragments cut from one original triangle can share only part of an edge.
    // Restrict numerical recovery to that provenance; never increase the weld
    // tolerance to cross a gap between unrelated original mesh faces.
    struct FFragmentEdge
    {
        FVector A, Direction;
        double Length;
        int32 Triangle, Source;
    };
    struct FFragmentPortal
    {
        int32 First, Second;
        double LowFirst, HighFirst, LowSecond, HighSecond;
        FVector A, B;
        bool bAmbiguous = false;
    };
    TArray<FFragmentEdge> Fragments;
    Fragments.Reserve(Triangles.Num()*3);
    for (int32 Index = 0; Index < Triangles.Num(); ++Index)
    {
        const auto& T = Triangles[Index];
        if (T.SourceTriangleId == INDEX_NONE || T.A.ContainsNaN() || T.B.ContainsNaN() || T.C.ContainsNaN()) continue;
        const double AreaSquared = FVector::CrossProduct(T.B-T.A,T.C-T.A).SizeSquared();
        if (!FMath::IsFinite(AreaSquared) || AreaSquared <= 0.0) continue;
        const FVector Points[] = {T.A,T.B,T.C};
        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            const FVector Delta = Points[(Edge+1)%3]-Points[Edge];
            const double Length = Delta.Size();
            if (FMath::IsFinite(Length) && Length > 0.0)
                Fragments.Add({Points[Edge],Delta/Length,Length,Index,T.SourceTriangleId});
        }
    }
    Fragments.Sort([](const auto& A, const auto& B) { return A.Source < B.Source; });
    const double Tolerance = FMath::Clamp(AdjacencyWeldToleranceCm,0.0,0.001);
    for (int32 Begin = 0; Begin < Fragments.Num();)
    {
        int32 End = Begin+1;
        while (End < Fragments.Num() && Fragments[End].Source == Fragments[Begin].Source) ++End;
        // Bound pathological source fragmentation independently of map instance count.
        if (End-Begin > 768) { Begin = End; continue; }
        TArray<FFragmentPortal> Portals;
        for (int32 I = Begin; I < End && Portals.Num() <= 1024; ++I)
            for (int32 J = I+1; J < End && Portals.Num() <= 1024; ++J)
            {
                const auto& A = Fragments[I]; const auto& B = Fragments[J];
                if (A.Triangle == B.Triangle || FVector::DotProduct(A.Direction,B.Direction) > -1.0+1.e-8) continue;
                const double B0 = FVector::DotProduct(B.A-A.A,A.Direction);
                const double B1 = FVector::DotProduct(B.A+B.Direction*B.Length-A.A,A.Direction);
                const double Low = FMath::Max(0.0,FMath::Min(B0,B1));
                const double High = FMath::Min(A.Length,FMath::Max(B0,B1));
                if (High-Low <= Tolerance) continue; // Ignore numerical endpoint-only contacts.
                const FVector P = A.A+A.Direction*Low, Q = A.A+A.Direction*High;
                const double LowB = FMath::Clamp(FVector::DotProduct(Q-B.A,B.Direction),0.0,B.Length);
                const double HighB = FMath::Clamp(FVector::DotProduct(P-B.A,B.Direction),0.0,B.Length);
                if (HighB-LowB <= Tolerance || FVector::DistSquared(P,B.A+B.Direction*HighB) > Tolerance*Tolerance
                    || FVector::DistSquared(Q,B.A+B.Direction*LowB) > Tolerance*Tolerance) continue;
                Portals.Add({I,J,Low,High,LowB,HighB,P,Q});
            }
        if (Portals.Num() > 1024) { Begin = End; continue; }
        for (int32 I = 0; I < Portals.Num(); ++I)
            for (int32 J = I+1; J < Portals.Num(); ++J)
            {
                auto& A = Portals[I]; auto& B = Portals[J];
                const int32 Shared = (A.First == B.First || A.First == B.Second) ? A.First
                    : (A.Second == B.First || A.Second == B.Second) ? A.Second : INDEX_NONE;
                if (Shared == INDEX_NONE) continue;
                const double LowA = Shared == A.First ? A.LowFirst : A.LowSecond;
                const double HighA = Shared == A.First ? A.HighFirst : A.HighSecond;
                const double LowB = Shared == B.First ? B.LowFirst : B.LowSecond;
                const double HighB = Shared == B.First ? B.HighFirst : B.HighSecond;
                if (FMath::Min(HighA,HighB)-FMath::Max(LowA,LowB) > Tolerance) A.bAmbiguous = B.bAmbiguous = true;
            }
        for (const auto& P : Portals)
            if (!P.bAmbiguous && Triangles[Fragments[P.First].Triangle].bCleanable
                && Triangles[Fragments[P.Second].Triangle].bCleanable)
            {
                AddMissing(Fragments[P.First].Triangle,Fragments[P.Second].Triangle,P.A,P.B);
                AddMissing(Fragments[P.Second].Triangle,Fragments[P.First].Triangle,P.A,P.B);
            }
        Begin = End;
    }
    ExactAdjacencyTriangleData = Triangles.GetData();
    ExactAdjacencyTriangleCount = Triangles.Num();
    ExactAdjacencyLayoutId = LayoutId;
}

TConstArrayView<FMD_ScrapeMeshUVNeighbor> UMD_ScrapeMeshUVData::GetTriangleNeighbors(int32 TriangleIndex) const
{
    if (!Triangles.IsValidIndex(TriangleIndex)) return {};
    BuildExactAdjacencyCache();
    if (const auto* Override = ExactAdjacencyOverrides.Find(TriangleIndex)) return MakeArrayView(*Override);
    return MakeArrayView(Triangles[TriangleIndex].Neighbors);
}
