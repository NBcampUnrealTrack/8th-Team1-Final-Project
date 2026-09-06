#include "Gameplay/Scrape/MD_ScrapeExposedSurface.h"

#if WITH_EDITOR
#include "Gameplay/Scrape/MD_ScrapeSurfaceExposureClip.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/FastWinding.h"
#include "StaticMeshAttributes.h"

using namespace UE::Geometry;

namespace
{
    double FragmentArea(const FMD_ScrapeSurfaceFragment& F)
    {
        return FVector::CrossProduct(F.B - F.A, F.C - F.A).Length() * .5;
    }

    bool ResolveSourcePoint(const FMD_ScrapeSurfaceFragment& Source, const FVector& Point,
        const double ToleranceCm, FVector& OutBarycentric, FVector& OutPosition)
    {
        const FVector E0 = Source.B - Source.A, E1 = Source.C - Source.A;
        const FVector Cross = FVector::CrossProduct(E0, E1);
        const double Denominator = Cross.SquaredLength();
        if (!FMath::IsFinite(Denominator) || Denominator <= 1e-20) return false;
        // The cutter may snap a boundary point by its explicit tolerance. Return it to the
        // original source plane/triangle before restoring UVs and shading attributes.
        const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Point, Source.A, Source.B, Source.C);
        if (Closest.ContainsNaN() || FVector::Distance(Closest, Point) > ToleranceCm) return false;
        const FVector D = Closest - Source.A;
        const double B = FVector::DotProduct(FVector::CrossProduct(D, E1), Cross) / Denominator;
        const double C = FVector::DotProduct(FVector::CrossProduct(E0, D), Cross) / Denominator;
        OutBarycentric = FVector(FMath::Max(0.0, 1.0 - B - C), FMath::Max(0.0, B), FMath::Max(0.0, C));
        const double Sum = OutBarycentric.X + OutBarycentric.Y + OutBarycentric.Z;
        if (!FMath::IsFinite(Sum) || Sum <= 0) return false;
        OutBarycentric /= Sum;
        OutPosition = Source.A * OutBarycentric.X + Source.B * OutBarycentric.Y + Source.C * OutBarycentric.Z;
        return !OutPosition.ContainsNaN();
    }
}

bool FMD_ScrapeExposedSurface::Build(const FMeshDescription& Source,
    const FMD_ScrapeExposedSurfaceSettings& Settings,
    FMeshDescription& OutDescription, FMD_ScrapeExposedSurfaceResult& OutResult,
    FString& OutError)
{
    OutDescription.Empty();
    OutResult = FMD_ScrapeExposedSurfaceResult();
    OutError.Reset();
    auto Fail = [&OutError](const FString& Message) { OutError = Message; return false; };
    if (Source.Triangles().Num() == 0 || !FMath::IsFinite(Settings.IntersectionToleranceCm)
        || Settings.IntersectionToleranceCm <= 0 || !FMath::IsFinite(Settings.ClassificationOffsetCm)
        || Settings.ClassificationOffsetCm <= 0 || !FMath::IsFinite(Settings.ContactGapToleranceCm)
        || Settings.ContactGapToleranceCm < 0 || !FMath::IsFinite(Settings.CoplanarToleranceCm)
        || Settings.CoplanarToleranceCm < 0 || !FMath::IsFinite(Settings.ContactNormalDotThreshold)
        || Settings.ContactNormalDotThreshold < .99 || Settings.ContactNormalDotThreshold > 1
        || Settings.MaximumOutputTriangles < Source.Triangles().Num())
    {
        return Fail(TEXT("Invalid exposed-surface settings or empty original geometry."));
    }
    FStaticMeshConstAttributes SourceAttributes(Source);
    const auto SourcePositions = SourceAttributes.GetVertexPositions();
    TArray<FMD_ScrapeSurfaceFragment> SourceTriangles;
    TMap<int32, int32> SourceIndexById;
    TMap<int32, double> OriginalAreas;
    for (FTriangleID Id : Source.Triangles().GetElementIDs())
    {
        if (Id.GetValue() < 0 || Id.GetValue() == MAX_int32) return Fail(TEXT("Unsupported original triangle ID."));
        const auto Instances = Source.GetTriangleVertexInstances(Id);
        FMD_ScrapeSurfaceFragment F;
        F.SourceTriangleId = Id.GetValue();
        F.A = FVector(SourcePositions[Source.GetVertexInstanceVertex(Instances[0])]);
        F.B = FVector(SourcePositions[Source.GetVertexInstanceVertex(Instances[1])]);
        F.C = FVector(SourcePositions[Source.GetVertexInstanceVertex(Instances[2])]);
        const double Area = FragmentArea(F);
        if (F.A.ContainsNaN() || F.B.ContainsNaN() || F.C.ContainsNaN()
            || !FMath::IsFinite(Area) || Area <= 5e-11)
        {
            return Fail(FString::Printf(TEXT("Invalid original triangle %d in exposed-surface preprocessing."), Id.GetValue()));
        }
        SourceIndexById.Add(F.SourceTriangleId, SourceTriangles.Add(F));
        OriginalAreas.Add(F.SourceTriangleId, Area);
        OutResult.OriginalAreaCm2 += Area;
    }
    OutResult.OriginalTriangleCount = SourceTriangles.Num();

    FDynamicMesh3 OriginalMesh;
    FMeshDescriptionToDynamicMesh Converter;
    Converter.bDisableAttributes = true;
    Converter.bCalculateMaps = true;
    Converter.bEnableOutputGroups = true;
    Converter.Convert(&Source, OriginalMesh);
    if (OriginalMesh.TriangleCount() != SourceTriangles.Num())
    {
        return Fail(TEXT("Dynamic conversion changed original triangle coverage."));
    }
    for (int32 Id : OriginalMesh.TriangleIndicesItr())
    {
        if (!Converter.TriIDMap.IsValidIndex(Id) || !SourceIndexById.Contains(Converter.TriIDMap[Id].GetValue()))
            return Fail(TEXT("Dynamic conversion lost original triangle provenance."));
        OriginalMesh.SetTriangleGroup(Id, Converter.TriIDMap[Id].GetValue() + 1);
    }

    // Intersection queries and polygon partitioning remain in original local centimeters.
    FDynamicMeshAABBTree3 SourceSpatial(&OriginalMesh);
    SourceSpatial.SetTolerance(Settings.IntersectionToleranceCm);
    const MeshIntersection::FIntersectionsQueryResult Intersections = SourceSpatial.FindAllSelfIntersections(
        true, IMeshSpatial::FQueryOptions(), [&Settings](FIntrTriangle3Triangle3d& Intersection)
        {
            Intersection.SetTolerance(Settings.IntersectionToleranceCm);
            return Intersection.Find();
        });
    OutResult.IntersectionSegmentCount = Intersections.Segments.Num();
    // A topology walk can fail to embed all intersection segments on imported meshes with
    // duplicate seams/T-junctions. Partition each immutable source triangle independently
    // by the planes of its actual intersecting triangles instead. Extending a finite
    // intersection to its plane can only add subdivisions: no geometric area is removed.
    TMap<int32, TArray<int32>> CuttingSources;
    for (const MeshIntersection::FSegmentIntersection& Segment : Intersections.Segments)
    {
        const int32 A = Converter.TriIDMap[Segment.TriangleID[0]].GetValue();
        const int32 B = Converter.TriIDMap[Segment.TriangleID[1]].GetValue();
        CuttingSources.FindOrAdd(A).AddUnique(B);
        CuttingSources.FindOrAdd(B).AddUnique(A);
    }
    TFastWindingTree<FDynamicMesh3> Winding(&SourceSpatial);
    TArray<FMD_ScrapeSurfaceFragment> CutFragments;
    const double PositionTolerance = FMath::Max(1e-6, Settings.IntersectionToleranceCm * 4.0);
    for (const FMD_ScrapeSurfaceFragment& Original : SourceTriangles)
    {
        TArray<TArray<FVector>> Polygons;
        Polygons.Add({ Original.A, Original.B, Original.C });
        TArray<int32>* CuttingIds = CuttingSources.Find(Original.SourceTriangleId);
        if (CuttingIds)
        {
            CuttingIds->Sort();
            for (int32 CuttingId : *CuttingIds)
            {
                const FMD_ScrapeSurfaceFragment& Other = SourceTriangles[SourceIndexById.FindChecked(CuttingId)];
                const FVector CuttingNormal = Other.OutwardNormal();
                TArray<TArray<FVector>> Next;
                for (const TArray<FVector>& Polygon : Polygons)
                {
                    double Minimum = TNumericLimits<double>::Max(), Maximum = -TNumericLimits<double>::Max();
                    for (const FVector& P : Polygon)
                    {
                        const double D = FVector::DotProduct(P - Other.A, CuttingNormal);
                        Minimum = FMath::Min(Minimum, D); Maximum = FMath::Max(Maximum, D);
                    }
                    // A crossing narrower than the explicit cm tolerance needs no new
                    // vertex; keeping its original polygon preserves all geometry.
                    if (Minimum >= -Settings.IntersectionToleranceCm || Maximum <= Settings.IntersectionToleranceCm)
                    {
                        Next.Add(Polygon); continue;
                    }
                    TArray<FVector> Positive, Negative;
                    auto AddPoint = [](TArray<FVector>& Points, const FVector& Point)
                    {
                        if (Points.IsEmpty() || FVector::DistSquared(Points.Last(), Point) > 1e-24) Points.Add(Point);
                    };
                    for (int32 K = 0; K < Polygon.Num(); ++K)
                    {
                        const FVector& A = Polygon[K]; const FVector& B = Polygon[(K + 1) % Polygon.Num()];
                        const double DA = FVector::DotProduct(A - Other.A, CuttingNormal);
                        const double DB = FVector::DotProduct(B - Other.A, CuttingNormal);
                        if (DA >= 0) AddPoint(Positive, A);
                        if (DA <= 0) AddPoint(Negative, A);
                        if ((DA < 0 && DB > 0) || (DA > 0 && DB < 0))
                        {
                            const FVector Point = A + (B - A) * (DA / (DA - DB));
                            AddPoint(Positive, Point); AddPoint(Negative, Point);
                        }
                    }
                    for (TArray<FVector>* Part : { &Positive, &Negative })
                    {
                        if (Part->Num() > 1 && FVector::DistSquared((*Part)[0], Part->Last()) <= 1e-24) Part->Pop();
                        if (Part->Num() < 3) return Fail(TEXT("Intersection plane partition produced an invalid polygon."));
                        Next.Add(MoveTemp(*Part));
                    }
                }
                int64 Budget = CutFragments.Num();
                for (const TArray<FVector>& Polygon : Next) Budget += Polygon.Num() - 2;
                if (Budget > Settings.MaximumOutputTriangles) return Fail(TEXT("Intersection plane partition exceeds the output triangle budget."));
                Polygons = MoveTemp(Next);
            }
        }
        for (const TArray<FVector>& Polygon : Polygons)
        {
            // Independently verify no output polygon still straddles an actual cut plane
            // beyond the configured numerical tolerance before centroid classification.
            if (CuttingIds) for (int32 CuttingId : *CuttingIds)
            {
                const FMD_ScrapeSurfaceFragment& Other = SourceTriangles[SourceIndexById.FindChecked(CuttingId)];
                const FVector N = Other.OutwardNormal();
                double Minimum = TNumericLimits<double>::Max(), Maximum = -TNumericLimits<double>::Max();
                for (const FVector& P : Polygon)
                {
                    const double D = FVector::DotProduct(P - Other.A, N);
                    Minimum = FMath::Min(Minimum, D); Maximum = FMath::Max(Maximum, D);
                }
                if (Minimum < -Settings.IntersectionToleranceCm * 1.001 && Maximum > Settings.IntersectionToleranceCm * 1.001)
                    return Fail(TEXT("An output fragment still crosses an original intersection plane."));
            }
            for (int32 Corner = 1; Corner + 1 < Polygon.Num(); ++Corner)
            {
                FMD_ScrapeSurfaceFragment F;
                F.SourceTriangleId = Original.SourceTriangleId;
                F.A = Polygon[0]; F.B = Polygon[Corner]; F.C = Polygon[Corner + 1];
                const double Area = FragmentArea(F);
                if (Area == 0) continue;
                if (!FMath::IsFinite(Area) || CutFragments.Num() >= Settings.MaximumOutputTriangles)
                    return Fail(TEXT("Intersection plane partition produced invalid geometry or exceeded its triangle budget."));
                const FVector Normal = Original.OutwardNormal();
                const FVector Center = (F.A + F.B + F.C) / 3.0;
                // UE winding is positive inside clockwise-front source parts. Keep winding.
                const double OutsideWinding = Winding.FastWindingNumber(Center + Normal * Settings.ClassificationOffsetCm);
                if (!FMath::IsFinite(OutsideWinding)) return Fail(TEXT("Non-finite source winding classification."));
                if (OutsideWinding >= .5) F.ExclusionReason = EMD_ScrapeExposureReason::InsideOtherSurface;
                CutFragments.Add(F);
            }
        }
    }

    TArray<FMD_ScrapeSurfaceFragment> Fragments;
    if (!FMD_ScrapeSurfaceExposureClip::SplitAndClassify(SourceTriangles, CutFragments, Settings, Fragments, OutError)) return false;
    if (Fragments.IsEmpty() || Fragments.Num() > Settings.MaximumOutputTriangles)
        return Fail(TEXT("Exposed-surface output exceeds supported geometry limits."));

    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto UVs = Attributes.GetVertexInstanceUVs();
    auto Normals = Attributes.GetVertexInstanceNormals();
    auto Tangents = Attributes.GetVertexInstanceTangents();
    auto Signs = Attributes.GetVertexInstanceBinormalSigns();
    auto Colors = Attributes.GetVertexInstanceColors();
    auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    const auto SourceUVs = SourceAttributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(SourceUVs.GetNumChannels());
    TMap<int32, FPolygonGroupID> Groups;
    for (FPolygonGroupID Group : Source.PolygonGroups().GetElementIDs())
    {
        const FPolygonGroupID NewGroup = Description.CreatePolygonGroup();
        SlotNames[NewGroup] = SourceAttributes.GetPolygonGroupMaterialSlotNames()[Group];
        Groups.Add(Group.GetValue(), NewGroup);
    }
    TMap<int32, double> ReconstructedAreas;
    TMap<int32, double> PreStorageAreas;
    TMap<int32, double> StorageRoundingBounds;
    double MaximumPositionRoundingCm = 0;
    double TotalStorageRoundingBoundCm2 = 0;
    double TotalStoredAreaDifferenceCm2 = 0;
    int32 WorstRoundingSourceId = INDEX_NONE;
    double WorstSourceAreaDelta = 0, WorstSourceAreaBound = 0;
    int32 FloatCollapsedFragments = 0;
    double FloatCollapsedDoubleArea = 0;
    for (const FMD_ScrapeSurfaceFragment& F : Fragments)
    {
        const int32* OriginalIndex = SourceIndexById.Find(F.SourceTriangleId);
        if (!OriginalIndex) return Fail(TEXT("Contact clipping lost source triangle provenance."));
        const FMD_ScrapeSurfaceFragment& Original = SourceTriangles[*OriginalIndex];
        const FTriangleID OriginalId(F.SourceTriangleId);
        const auto SourceInstances = Source.GetTriangleVertexInstances(OriginalId);
        const FVector InputPositions[] = { F.A, F.B, F.C };
        FVector Barycentrics[3], RestoredPositions[3];
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            if (!ResolveSourcePoint(Original, InputPositions[Corner], PositionTolerance,
                Barycentrics[Corner], RestoredPositions[Corner]))
                return Fail(TEXT("A contact fragment left its original source triangle."));
        }
        const double DoubleArea = FVector::CrossProduct(RestoredPositions[1] - RestoredPositions[0],
            RestoredPositions[2] - RestoredPositions[0]).Length() * .5;
        const FVector StoredPoints[] = { FVector(FVector3f(RestoredPositions[0])),
            FVector(FVector3f(RestoredPositions[1])), FVector(FVector3f(RestoredPositions[2])) };
        const double Area = FVector::CrossProduct(StoredPoints[1] - StoredPoints[0], StoredPoints[2] - StoredPoints[0]).Length() * .5;
        if (!FMath::IsFinite(Area)) return Fail(TEXT("Non-finite stored fragment geometry."));
        const FVector U = RestoredPositions[1] - RestoredPositions[0];
        const FVector V = RestoredPositions[2] - RestoredPositions[0];
        const FVector DU = StoredPoints[1] - StoredPoints[0] - U;
        const FVector DV = StoredPoints[2] - StoredPoints[0] - V;
        // Triangle inequality on (U+DU)x(V+DV)-UxV bounds only the actual
        // float32 position rounding. This is independent of polygon area preservation.
        const double StorageBound = .5 * (U.Length() * DV.Length() + V.Length() * DU.Length()
            + DU.Length() * DV.Length());
        if (!FMath::IsFinite(StorageBound)) return Fail(TEXT("Non-finite float storage area bound."));
        PreStorageAreas.FindOrAdd(F.SourceTriangleId) += DoubleArea;
        StorageRoundingBounds.FindOrAdd(F.SourceTriangleId) += StorageBound;
        TotalStorageRoundingBoundCm2 += StorageBound;
        for (int32 K = 0; K < 3; ++K)
            MaximumPositionRoundingCm = FMath::Max(MaximumPositionRoundingCm, FVector::Distance(StoredPoints[K], RestoredPositions[K]));
        if (Area == 0)
        {
            // These corners have exactly zero surface area in the renderer's float32
            // position format. Never drop a positive-area stored triangle by a cutoff.
            ++FloatCollapsedFragments; FloatCollapsedDoubleArea += DoubleArea;
            continue;
        }
        TArray<FVertexInstanceID> Instances;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const FVector& Bary = Barycentrics[Corner];
            const FVertexID Vertex = Description.CreateVertex();
            Positions[Vertex] = FVector3f(RestoredPositions[Corner]);
            const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
            Instances.Add(Instance);
            FVector Normal = FVector::ZeroVector, Tangent = FVector::ZeroVector;
            FVector4d Color(0, 0, 0, 0);
            double Sign = 0;
            for (int32 K = 0; K < 3; ++K)
            {
                Normal += FVector(SourceAttributes.GetVertexInstanceNormals()[SourceInstances[K]]) * Bary[K];
                Tangent += FVector(SourceAttributes.GetVertexInstanceTangents()[SourceInstances[K]]) * Bary[K];
                Color += FVector4d(SourceAttributes.GetVertexInstanceColors()[SourceInstances[K]]) * Bary[K];
                Sign += SourceAttributes.GetVertexInstanceBinormalSigns()[SourceInstances[K]] * Bary[K];
            }
            Normals[Instance] = FVector3f(Normal.GetSafeNormal());
            Tangents[Instance] = FVector3f(Tangent.GetSafeNormal());
            Signs[Instance] = Sign < 0 ? -1.0f : 1.0f;
            Colors[Instance] = FVector4f(Color);
            for (int32 Channel = 0; Channel < SourceUVs.GetNumChannels(); ++Channel)
            {
                FVector2D UV = FVector2D::ZeroVector;
                for (int32 K = 0; K < 3; ++K) UV += FVector2D(SourceUVs.Get(SourceInstances[K], Channel)) * Bary[K];
                UVs.Set(Instance, Channel, FVector2f(UV));
            }
        }
        const FPolygonGroupID Group = Groups.FindChecked(Source.GetTrianglePolygonGroup(OriginalId).GetValue());
        const FTriangleID NewId = Description.CreateTriangle(Group, Instances);
        FMD_ScrapeSourceTriangleRef Ref;
        Ref.SourceTriangleId = F.SourceTriangleId;
        Ref.BarycentricA = Barycentrics[0]; Ref.BarycentricB = Barycentrics[1]; Ref.BarycentricC = Barycentrics[2];
        Ref.ExclusionReason = F.ExclusionReason;
        OutResult.TriangleSourceRefs.Add(NewId.GetValue(), Ref);
        ReconstructedAreas.FindOrAdd(F.SourceTriangleId) += Area;
        switch (F.ExclusionReason)
        {
        case EMD_ScrapeExposureReason::Exposed: ++OutResult.ExposedTriangleCount; OutResult.ExposedAreaCm2 += Area; break;
        case EMD_ScrapeExposureReason::InsideOtherSurface: OutResult.InteriorAreaCm2 += Area; break;
        case EMD_ScrapeExposureReason::NearOpposingContact: OutResult.NearContactAreaCm2 += Area; break;
        case EMD_ScrapeExposureReason::CoincidentDuplicate: OutResult.DuplicateAreaCm2 += Area; break;
        default: return Fail(TEXT("Unknown fragment exclusion reason."));
        }
    }
    for (const auto& Pair : OriginalAreas)
    {
        const double BeforeStorage = PreStorageAreas.FindRef(Pair.Key);
        const double Stored = ReconstructedAreas.FindRef(Pair.Key);
        const double RoundingBound = StorageRoundingBounds.FindRef(Pair.Key);
        const double SplitTolerance = FMath::Max(.0001, Pair.Value * 1e-5);
        const double NumericRoundoff = FMath::Max(1e-10, Pair.Value * 1e-12);
        if (FMath::Abs(BeforeStorage - Pair.Value) > SplitTolerance)
            return Fail(FString::Printf(TEXT("Source %d pre-storage area conservation failed: original=%.12g, split=%.12g, tolerance=%.12g cm2."),
                Pair.Key, Pair.Value, BeforeStorage, SplitTolerance));
        if (Stored <= 0 || FMath::Abs(Stored - BeforeStorage) > RoundingBound + NumericRoundoff)
            return Fail(FString::Printf(TEXT("Source %d float storage area exceeded its measured rounding bound: original=%.12g, pre-storage=%.12g, stored=%.12g, bound=%.12g, numerical margin=%.12g cm2."),
                Pair.Key, Pair.Value, BeforeStorage, Stored, RoundingBound, NumericRoundoff));
        const double SourceDelta = FMath::Abs(Stored - BeforeStorage);
        TotalStoredAreaDifferenceCm2 += SourceDelta;
        if (SourceDelta > WorstSourceAreaDelta)
        {
            WorstRoundingSourceId = Pair.Key; WorstSourceAreaDelta = SourceDelta; WorstSourceAreaBound = RoundingBound;
        }
    }
    OutResult.OutputTriangleCount = Description.Triangles().Num();
    if (OutResult.ExposedTriangleCount == 0) return Fail(TEXT("Source classification left no exposed surface."));
    OutResult.Diagnostics = FString::Printf(TEXT("Original=%d; intersection segments=%d; fragments=%d; exposed=%.6f m2; interior=%.6f m2; near contact=%.6f m2; coincident duplicates=%.6f m2. Geometry and source material/UV provenance retained; original-plane partition (no topology walk); tolerance=%.6f cm, outward offset=%.6f cm, contact band=%.6f cm."),
        OutResult.OriginalTriangleCount, OutResult.IntersectionSegmentCount, OutResult.OutputTriangleCount,
        OutResult.ExposedAreaCm2 / 10000.0, OutResult.InteriorAreaCm2 / 10000.0, OutResult.NearContactAreaCm2 / 10000.0,
        OutResult.DuplicateAreaCm2 / 10000.0, Settings.IntersectionToleranceCm, Settings.ClassificationOffsetCm, Settings.ContactGapToleranceCm);
    OutResult.Diagnostics += FString::Printf(TEXT(" Float32-zero-area fragments omitted=%d (pre-storage double area %.12g cm2); strict per-source split area conserved; total absolute source storage area delta=%.12g cm2 <= measured rounding bound %.12g cm2; max position rounding %.12g cm."),
        FloatCollapsedFragments, FloatCollapsedDoubleArea, TotalStoredAreaDifferenceCm2, TotalStorageRoundingBoundCm2, MaximumPositionRoundingCm);
    OutResult.Diagnostics += FString::Printf(TEXT(" Largest source storage delta: source=%d, delta=%.12g, bound=%.12g cm2."),
        WorstRoundingSourceId, WorstSourceAreaDelta, WorstSourceAreaBound);
    OutDescription = MoveTemp(Description);
    return true;
}
#endif
