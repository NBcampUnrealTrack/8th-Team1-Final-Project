#include "Gameplay/Scrape/MD_ScrapeMeshUVAdjacency.h"

#if WITH_EDITOR
#include "Algo/Sort.h"

namespace
{
    constexpr double MinimumPortalLengthCm = 0.0;
    constexpr int64 MaximumCandidateChecks = 50000000;
    constexpr int32 MaximumPairCount = 1000000;
    constexpr int32 MaximumTriangleLinks = 2048;

    struct FMDAdjacencyEdge
    {
        int32 TriangleIndex;
        FVector A, B, Direction;
        double Length;
        FBox Bounds;
        TArray<int32> Pairs;
    };
    struct FPair
    {
        int32 EdgeA, EdgeB;
        double StartA, EndA, StartB, EndB;
        FVector A, B;
        bool bAmbiguous = false;
    };

    // Only budget-risk bakes use this broad phase. Exact expanded boxes and original
    // sweep ranks preserve every accepted pair, its orientation and its trim order.
    class FMDAdjacencyBoundsTree
    {
        struct FNode
        {
            FBox Bounds = FBox(ForceInit);
            int32 First = 0, Count = 0;
            int32 Left = INDEX_NONE, Right = INDEX_NONE;
        };

        const TArray<FMDAdjacencyEdge>& Edges;
        TArray<int32> EdgeRanks;
        TArray<int32> Indices;
        TArray<FNode> Nodes;

        int32 BuildNode(const int32 First, const int32 Count)
        {
            FNode Node;
            Node.First = First; Node.Count = Count;
            FBox Centers(ForceInit);
            for (int32 I = First; I < First + Count; ++I)
            {
                const FBox& Bounds = Edges[Indices[I]].Bounds;
                Node.Bounds += Bounds;
                Centers += Bounds.GetCenter();
            }
            const int32 NodeIndex = Nodes.Add(Node);
            if (Count > 8)
            {
                const FVector Size = Centers.GetSize();
                const int32 Axis = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : (Size.Y >= Size.Z ? 1 : 2);
                Algo::Sort(MakeArrayView(Indices.GetData() + First, Count), [&](const int32 A, const int32 B)
                {
                    const double CenterA = Edges[A].Bounds.GetCenter()[Axis];
                    const double CenterB = Edges[B].Bounds.GetCenter()[Axis];
                    return CenterA == CenterB ? EdgeRanks[A] < EdgeRanks[B] : CenterA < CenterB;
                });
                const int32 LeftCount = Count / 2;
                const int32 Left = BuildNode(First, LeftCount);
                const int32 Right = BuildNode(First + LeftCount, Count - LeftCount);
                // Recursive Add can reallocate Nodes; reacquire by index.
                Nodes[NodeIndex].Left = Left; Nodes[NodeIndex].Right = Right;
            }
            return NodeIndex;
        }

    public:
        FMDAdjacencyBoundsTree(const TArray<FMDAdjacencyEdge>& InEdges, const TArray<int32>& Order)
            : Edges(InEdges), Indices(Order)
        {
            EdgeRanks.SetNumUninitialized(Edges.Num());
            for (int32 I = 0; I < Order.Num(); ++I) { EdgeRanks[Order[I]] = I; }
            if (!Indices.IsEmpty()) { BuildNode(0, Indices.Num()); }
        }

        bool Collect(const FBox& Bounds, const int32 AfterRank, TArray<int32>& OutRanks,
            int64& WorkChecks, FString& OutError) const
        {
            OutRanks.Reset();
            TArray<int32, TInlineAllocator<64>> Stack;
            if (!Nodes.IsEmpty()) { Stack.Add(0); }
            while (!Stack.IsEmpty())
            {
                const FNode& Node = Nodes[Stack.Pop(EAllowShrinking::No)];
                if (++WorkChecks > MaximumCandidateChecks)
                { OutError = TEXT("Partial-edge adjacency exceeded its bounded candidate budget."); return false; }
                if (!Bounds.Intersect(Node.Bounds)) { continue; }
                if (Node.Left != INDEX_NONE)
                {
                    Stack.Add(Node.Right); Stack.Add(Node.Left);
                    continue;
                }
                for (int32 I = Node.First; I < Node.First + Node.Count; ++I)
                {
                    const int32 EdgeIndex = Indices[I], Rank = EdgeRanks[EdgeIndex];
                    if (Rank <= AfterRank) { continue; }
                    if (++WorkChecks > MaximumCandidateChecks)
                    { OutError = TEXT("Partial-edge adjacency exceeded its bounded candidate budget."); return false; }
                    if (Bounds.Intersect(Edges[EdgeIndex].Bounds)) { OutRanks.Add(Rank); }
                }
            }
            OutRanks.Sort();
            return true;
        }

        SIZE_T GetAllocatedSize() const
        { return EdgeRanks.GetAllocatedSize() + Indices.GetAllocatedSize() + Nodes.GetAllocatedSize(); }
    };

    void TrimPair(FPair& Pair, const int32 EdgeIndex, const double Low, const double High)
    {
        const double Start = Pair.EdgeA == EdgeIndex ? Pair.StartA : Pair.StartB;
        const double End = Pair.EdgeA == EdgeIndex ? Pair.EndA : Pair.EndB;
        const double T0 = FMath::Clamp((Pair.EdgeA == EdgeIndex ? Low - Start : End - High) / (End - Start), 0.0, 1.0);
        const double T1 = FMath::Clamp((Pair.EdgeA == EdgeIndex ? High - Start : End - Low) / (End - Start), 0.0, 1.0);
        const FVector OriginalA = Pair.A, OriginalB = Pair.B;
        const double OriginalStartA = Pair.StartA, OriginalEndB = Pair.EndB;
        const double LengthA = Pair.EndA - Pair.StartA, LengthB = Pair.EndB - Pair.StartB;
        Pair.A = FMath::Lerp(OriginalA, OriginalB, T0); Pair.B = FMath::Lerp(OriginalA, OriginalB, T1);
        Pair.StartA = OriginalStartA + LengthA * T0; Pair.EndA = OriginalStartA + LengthA * T1;
        Pair.StartB = OriginalEndB - LengthB * T1; Pair.EndB = OriginalEndB - LengthB * T0;
    }

    bool FindOverlap(const FMDAdjacencyEdge& A, const FMDAdjacencyEdge& B, const double Tolerance, FPair& Out)
    {
        // Reversed collinearity distinguishes a surface seam from overlapping same-sided triangles.
        if (FVector::DotProduct(A.Direction, B.Direction) > -1.0 + 1e-8) { return false; }
        const double Start = FVector::DotProduct(B.A - A.A, A.Direction);
        const double End = FVector::DotProduct(B.B - A.A, A.Direction);
        const double Low = FMath::Max(0.0, FMath::Min(Start, End));
        const double High = FMath::Min(A.Length, FMath::Max(Start, End));
        if (High - Low <= MinimumPortalLengthCm) { return false; }
        const FVector PA = A.A + A.Direction * Low;
        const FVector PB = A.A + A.Direction * High;
        const double BA = FMath::Clamp(FVector::DotProduct(PA - B.A, B.Direction), 0.0, B.Length);
        const double BB = FMath::Clamp(FVector::DotProduct(PB - B.A, B.Direction), 0.0, B.Length);
        const FVector QA = B.A + B.Direction * BA;
        const FVector QB = B.A + B.Direction * BB;
        const double SquaredTolerance = Tolerance * Tolerance + 1e-20;
        if (FVector::DistSquared(PA, QA) > SquaredTolerance || FVector::DistSquared(PB, QB) > SquaredTolerance)
        { return false; }
        Out.StartA = Low; Out.EndA = High;
        Out.StartB = FMath::Min(BA, BB); Out.EndB = FMath::Max(BA, BB);
        if (Out.EndB - Out.StartB <= MinimumPortalLengthCm) { return false; }
        // The midpoint is within half the accepted seam tolerance of both original surfaces.
        Out.A = PA + (QA - PA) * 0.5; Out.B = PB + (QB - PB) * 0.5;
        // Positive projected intervals can still coincide after adding large source coordinates.
        return !Out.A.ContainsNaN() && !Out.B.ContainsNaN() && FVector::DistSquared(Out.A, Out.B) > 0.0;
    }
}

bool FMD_ScrapeMeshUVAdjacency::Build(TArray<FMD_ScrapeMeshUVTriangle>& Triangles,
    const double ToleranceCm, int32& OutConnectedPairs, FString& OutError)
{
    OutConnectedPairs = 0; OutError.Reset();
    if (!FMath::IsFinite(ToleranceCm) || ToleranceCm < 0 || ToleranceCm > 0.01 || Triangles.Num() > 100000)
    { OutError = TEXT("Invalid adjacency tolerance or triangle budget."); return false; }
    TArray<FMDAdjacencyEdge> Edges;
    FBox AllBounds(ForceInit);
    for (int32 Index = 0; Index < Triangles.Num(); ++Index)
    {
        const FMD_ScrapeMeshUVTriangle& Triangle = Triangles[Index];
        if (!Triangle.bCleanable) { continue; }
        const FVector Vertices[] = {Triangle.A, Triangle.B, Triangle.C};
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            FMDAdjacencyEdge Edge; Edge.TriangleIndex = Index;
            Edge.A = Vertices[EdgeIndex]; Edge.B = Vertices[(EdgeIndex + 1) % 3];
            Edge.Length = FVector::Distance(Edge.A, Edge.B);
            if (Edge.A.ContainsNaN() || Edge.B.ContainsNaN() || !FMath::IsFinite(Edge.Length))
            { OutError = TEXT("Invalid edge geometry in adjacency bake."); return false; }
            if (Edge.Length <= MinimumPortalLengthCm) { continue; }
            Edge.Direction = (Edge.B - Edge.A) / Edge.Length;
            Edge.Bounds = FBox(Edge.A.ComponentMin(Edge.B), Edge.A.ComponentMax(Edge.B)).ExpandBy(ToleranceCm);
            AllBounds += Edge.Bounds;
            Edges.Add(MoveTemp(Edge));
        }
    }
    const FVector Extent = AllBounds.IsValid ? AllBounds.GetSize() : FVector::ZeroVector;
    const int32 ExtentAxis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
    int32 Axis = ExtentAxis;
    TArray<int32> Order;
    int64 AxisCandidateCounts[3] = { 0, 0, 0 };
    int64 BestCandidateCount = MAX_int64;
    for (int32 CandidateAxis = 0; CandidateAxis < 3; ++CandidateAxis)
    {
        TArray<int32> CandidateOrder;
        TArray<double> SortedMaxima;
        CandidateOrder.Reserve(Edges.Num()); SortedMaxima.Reserve(Edges.Num());
        for (int32 I = 0; I < Edges.Num(); ++I)
        {
            CandidateOrder.Add(I); SortedMaxima.Add(Edges[I].Bounds.Max[CandidateAxis]);
        }
        CandidateOrder.Sort([&](const int32 A, const int32 B)
        {
            return Edges[A].Bounds.Min[CandidateAxis] == Edges[B].Bounds.Min[CandidateAxis]
                ? A < B : Edges[A].Bounds.Min[CandidateAxis] < Edges[B].Bounds.Min[CandidateAxis];
        });
        SortedMaxima.Sort();
        // Count interval overlaps without visiting every pair. A wall's longest axis can
        // still place millions of unrelated edges in the same sweep interval.
        int32 Expired = 0;
        int64 Count = 0;
        for (int32 I = 0; I < CandidateOrder.Num(); ++I)
        {
            const double Minimum = Edges[CandidateOrder[I]].Bounds.Min[CandidateAxis];
            while (Expired < SortedMaxima.Num() && SortedMaxima[Expired] < Minimum) { ++Expired; }
            Count += static_cast<int64>(I) - Expired;
        }
        AxisCandidateCounts[CandidateAxis] = Count;
        if (Count < BestCandidateCount || (Count == BestCandidateCount && CandidateAxis == ExtentAxis))
        {
            BestCandidateCount = Count; Axis = CandidateAxis; Order = MoveTemp(CandidateOrder);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("MeshUV adjacency: %d edges; candidate overlaps X=%lld Y=%lld Z=%lld; sweep axis=%d."),
        Edges.Num(), AxisCandidateCounts[0], AxisCandidateCounts[1], AxisCandidateCounts[2], Axis);
    TUniquePtr<FMDAdjacencyBoundsTree> BoundsTree;
    if (BestCandidateCount > MaximumCandidateChecks)
    {
        BoundsTree = MakeUnique<FMDAdjacencyBoundsTree>(Edges, Order);
    }
    TArray<FPair> Pairs;
    TArray<int32> CandidateRanks;
    int64 CandidateChecks = 0;
    int64 BroadPhaseChecks = 0;
    for (int32 I = 0; I < Order.Num(); ++I)
    {
        const int32 EdgeA = Order[I];
        if (BoundsTree && !BoundsTree->Collect(Edges[EdgeA].Bounds, I, CandidateRanks, BroadPhaseChecks, OutError))
        { return false; }
        const int32 CandidateCount = BoundsTree ? CandidateRanks.Num() : Order.Num() - I - 1;
        for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
        {
            const int32 J = BoundsTree ? CandidateRanks[CandidateIndex] : I + 1 + CandidateIndex;
            const int32 EdgeB = Order[J];
            const FMDAdjacencyEdge& A = Edges[EdgeA]; const FMDAdjacencyEdge& B = Edges[EdgeB];
            if (B.Bounds.Min[Axis] > A.Bounds.Max[Axis]) { break; }
            if (++CandidateChecks > MaximumCandidateChecks)
            { OutError = TEXT("Partial-edge adjacency exceeded its bounded candidate budget."); return false; }
            if (A.TriangleIndex == B.TriangleIndex || !A.Bounds.Intersect(B.Bounds)) { continue; }
            if (FVector::DotProduct(Triangles[A.TriangleIndex].NormalLocal,
                Triangles[B.TriangleIndex].NormalLocal) < -0.999) { continue; }
            FPair Pair; Pair.EdgeA = EdgeA; Pair.EdgeB = EdgeB;
            if (!FindOverlap(A, B, ToleranceCm, Pair)) { continue; }
            if (Pairs.Num() >= MaximumPairCount)
            { OutError = TEXT("Partial-edge adjacency exceeded its pair budget."); return false; }
            const int32 PairIndex = Pairs.Add(Pair);
            Edges[EdgeA].Pairs.Add(PairIndex); Edges[EdgeB].Pairs.Add(PairIndex);
        }
    }
    if (BoundsTree)
    {
        UE_LOG(LogTemp, Display, TEXT("MeshUV adjacency spatial broad phase: %lld box checks, %lld intersecting candidates, %.3f MiB search storage."),
            BroadPhaseChecks, CandidateChecks, (BoundsTree->GetAllocatedSize() + CandidateRanks.GetAllocatedSize()) / (1024.0 * 1024.0));
    }
    // Two short edges may meet at a point on one long edge. Positive overlapping intervals,
    // however, indicate more than two incident faces and are not a safe bridge for a brush.
    for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
    {
        const FMDAdjacencyEdge& Edge = Edges[EdgeIndex];
        if (Edge.Pairs.Num() > MaximumTriangleLinks)
        { OutError = TEXT("Partial-edge adjacency exceeded its local branch budget."); return false; }
        for (int32 I = 0; I < Edge.Pairs.Num(); ++I)
        {
            FPair& A = Pairs[Edge.Pairs[I]];
            for (int32 J = I + 1; J < Edge.Pairs.Num(); ++J)
            {
                FPair& B = Pairs[Edge.Pairs[J]];
                const double LowA = A.EdgeA == EdgeIndex ? A.StartA : A.StartB;
                const double HighA = A.EdgeA == EdgeIndex ? A.EndA : A.EndB;
                const double LowB = B.EdgeA == EdgeIndex ? B.StartA : B.StartB;
                const double HighB = B.EdgeA == EdgeIndex ? B.EndA : B.EndB;
                const double Overlap = FMath::Min(HighA, HighB) - FMath::Max(LowA, LowB);
                if (Overlap <= MinimumPortalLengthCm) { continue; }
                const bool bABeforeB = LowA < LowB && HighA < HighB;
                const bool bBBeforeA = LowB < LowA && HighB < HighA;
                if (Overlap <= ToleranceCm && (bABeforeB || bBBeforeA))
                {
                    // Independent float cuts can overlap at their intended shared endpoint.
                    // Trim only these portals to one meeting point; no mesh vertex is moved.
                    const double Meeting = 0.5 * (FMath::Min(HighA, HighB) + FMath::Max(LowA, LowB));
                    TrimPair(A, EdgeIndex, bABeforeB ? LowA : Meeting, bABeforeB ? Meeting : HighA);
                    TrimPair(B, EdgeIndex, bABeforeB ? Meeting : LowB, bABeforeB ? HighB : Meeting);
                }
                else { A.bAmbiguous = true; B.bAmbiguous = true; }
            }
        }
    }
    TArray<TArray<FMD_ScrapeMeshUVNeighbor>> Neighbors;
    Neighbors.SetNum(Triangles.Num());
    for (const FPair& Pair : Pairs)
    {
        if (Pair.bAmbiguous || Pair.EndA - Pair.StartA <= MinimumPortalLengthCm
            || Pair.EndB - Pair.StartB <= MinimumPortalLengthCm
            || FVector::DistSquared(Pair.A, Pair.B) <= 0.0) { continue; }
        const int32 A = Edges[Pair.EdgeA].TriangleIndex, B = Edges[Pair.EdgeB].TriangleIndex;
        if (Neighbors[A].Num() >= MaximumTriangleLinks || Neighbors[B].Num() >= MaximumTriangleLinks)
        { OutError = TEXT("Triangle adjacency exceeded its neighbor budget."); return false; }
        FMD_ScrapeMeshUVNeighbor Forward; Forward.TriangleIndex = B;
        Forward.SharedEdgeA = Pair.A; Forward.SharedEdgeB = Pair.B;
        FMD_ScrapeMeshUVNeighbor Reverse; Reverse.TriangleIndex = A;
        Reverse.SharedEdgeA = Pair.B; Reverse.SharedEdgeB = Pair.A;
        Neighbors[A].Add(Forward); Neighbors[B].Add(Reverse);
        ++OutConnectedPairs;
    }
    for (int32 Index = 0; Index < Triangles.Num(); ++Index)
    {
        Neighbors[Index].Sort([](const FMD_ScrapeMeshUVNeighbor& A, const FMD_ScrapeMeshUVNeighbor& B)
        { return A.TriangleIndex < B.TriangleIndex; });
        Triangles[Index].Neighbors = MoveTemp(Neighbors[Index]);
        Triangles[Index].AdjacentTriangles = FIntVector(INDEX_NONE);
    }
    return true;
}
#endif
