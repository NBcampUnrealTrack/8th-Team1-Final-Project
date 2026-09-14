#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

#include "Engine/StaticMesh.h"
#include "UObject/Package.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVSeam.h"
#include "Gameplay/Scrape/MD_ScrapeVisualPreparation.h"
#include "Gameplay/Scrape/MD_ScrapeFaceProgress.h"
#include "Gameplay/Scrape/MD_ScrapeCellView.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    bool ClosestPointOnPositiveTriangle(const FVector& A, const FVector& B, const FVector& C,
        const FVector& Point, FVector& OutClosest)
    {
        OutClosest = FVector::ZeroVector;
        const FVector E0 = B - A, E1 = C - A;
        const FVector Cross = FVector::CrossProduct(E0, E1);
        const double Denominator = Cross.SizeSquared();
        if (A.ContainsNaN() || B.ContainsNaN() || C.ContainsNaN() || Point.ContainsNaN()
            || !FMath::IsFinite(Denominator) || Denominator <= 0.0) { return false; }
        const FVector Normal = Cross / FMath::Sqrt(Denominator);
        const FVector Projected = Point - FVector::DotProduct(Point - A, Normal) * Normal;
        const FVector Delta = Projected - A;
        const double U = FVector::DotProduct(FVector::CrossProduct(Delta, E1), Cross) / Denominator;
        const double V = FVector::DotProduct(FVector::CrossProduct(E0, Delta), Cross) / Denominator;
        if (FMath::IsFinite(U) && FMath::IsFinite(V) && U >= 0.0 && V >= 0.0 && U + V <= 1.0)
        { OutClosest = A + E0 * U + E1 * V; return !OutClosest.ContainsNaN(); }
        double BestDistance = TNumericLimits<double>::Max();
        const FVector Vertices[] = {A, B, C};
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector Edge = Vertices[(I + 1) % 3] - Vertices[I];
            const double LengthSquared = Edge.SizeSquared();
            if (!FMath::IsFinite(LengthSquared) || LengthSquared <= 0.0) { continue; }
            const double T = FMath::Clamp(FVector::DotProduct(Point - Vertices[I], Edge) / LengthSquared, 0.0, 1.0);
            const FVector Candidate = Vertices[I] + Edge * T;
            const double Distance = FVector::DistSquared(Point, Candidate);
            if (FMath::IsFinite(Distance) && Distance < BestDistance)
            { BestDistance = Distance; OutClosest = Candidate; }
        }
        return BestDistance < TNumericLimits<double>::Max();
    }

	bool PreferExistingCoincidentSurface(const FMD_ScrapeMeshUVTriangle& Existing,
		const FMD_ScrapeMeshUVTriangle& Candidate)
	{
		if (Existing.bCleanable == Candidate.bCleanable) { return true; }
		// A coincident duplicate must not hide the one active owner of the same
		// outward-facing patch. Differently oriented caps still block at corners.
		const bool bSameFacingPlane = FVector::DotProduct(Existing.NormalLocal, Candidate.NormalLocal) >= 1.0 - 1.0e-6;
		return bSameFacingPlane ? Existing.bCleanable : !Existing.bCleanable;
	}

	bool IsFiniteUV(const FVector2D& UV)
	{
		return FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y);
	}

	bool UVBarycentric(const FMD_ScrapeMeshUVTriangle& T, const FVector2D& P, FVector& Out)
	{
		const FVector2D E0 = T.UVB - T.UVA;
		const FVector2D E1 = T.UVC - T.UVA;
		const FVector2D D = P - T.UVA;
		const double Det = E0.X * E1.Y - E0.Y * E1.X;
		if (!FMath::IsFinite(Det) || Det == 0.0)
		{
			return false;
		}
		const double B = (D.X * E1.Y - D.Y * E1.X) / Det;
		const double C = (E0.X * D.Y - E0.Y * D.X) / Det;
		Out = FVector(1.0 - B - C, B, C);
		return !Out.ContainsNaN();
	}

    double PortalEdgeDistanceSquared(const FMD_ScrapeMeshUVTriangle& Triangle,
        const FVector& A, const FVector& B, int32& OutEdge)
    {
        OutEdge = INDEX_NONE;
        double Best = TNumericLimits<double>::Max();
        const FVector Points[] = {Triangle.A, Triangle.B, Triangle.C};
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector Edge = Points[(I + 1) % 3] - Points[I];
            const double SquaredLength = Edge.SizeSquared();
            if (SquaredLength <= 0.0) { continue; }
            const double TA = FMath::Clamp(FVector::DotProduct(A - Points[I], Edge) / SquaredLength, 0.0, 1.0);
            const double TB = FMath::Clamp(FVector::DotProduct(B - Points[I], Edge) / SquaredLength, 0.0, 1.0);
            const double Distance = FMath::Max(FVector::DistSquared(A, Points[I] + Edge * TA),
                FVector::DistSquared(B, Points[I] + Edge * TB));
            if (FMath::IsFinite(Distance) && Distance < Best) { Best = Distance; OutEdge = I; }
        }
        return Best;
    }

    bool PortalOnTriangleEdge(const FMD_ScrapeMeshUVTriangle& Triangle,
        const FVector& A, const FVector& B, const double ToleranceSquared)
    {
        int32 Edge;
        return PortalEdgeDistanceSquared(Triangle, A, B, Edge) <= ToleranceSquared;
    }

	void SetSample(const FMD_ScrapeMeshUVTriangle& T, int32 Index,
		const FVector& Barycentric, FMD_ScrapeMeshUVSample& Out)
	{
		Out.TriangleIndex = Index;
		Out.Barycentric = Barycentric;
		Out.PositionLocal = T.A * Barycentric.X + T.B * Barycentric.Y + T.C * Barycentric.Z;
		Out.NormalLocal = T.NormalLocal;
		Out.CanonicalUV = T.UVA * Barycentric.X + T.UVB * Barycentric.Y + T.UVC * Barycentric.Z;
	}

	// A triangle clipped by a rectangle has at most seven vertices.
	using FCellUVPolygon = TArray<FVector2D, TInlineAllocator<8>>;

	void ClipHalfPlane(FCellUVPolygon& Polygon, int32 Axis, double Limit, bool bKeepGreater)
	{
		if (Polygon.IsEmpty()) return;
		FCellUVPolygon Output;
		Output.Reserve(Polygon.Num() + 1);
		FVector2D Previous = Polygon.Last();
		auto Inside = [=](const FVector2D& P)
		{
			return bKeepGreater ? P[Axis] >= Limit : P[Axis] <= Limit;
		};
		bool bPreviousInside = Inside(Previous);
		for (const FVector2D& Current : Polygon)
		{
			const bool bCurrentInside = Inside(Current);
			if (bPreviousInside != bCurrentInside)
			{
				const double Denominator = Current[Axis] - Previous[Axis];
				if (FMath::Abs(Denominator) > 1.0e-30)
				{
					FVector2D Intersection = FMath::Lerp(Previous, Current,
						(Limit - Previous[Axis]) / Denominator);
					Intersection[Axis] = Limit;
					Output.Add(Intersection);
				}
			}
			if (bCurrentInside) Output.Add(Current);
			Previous = Current;
			bPreviousInside = bCurrentInside;
		}
		Polygon = MoveTemp(Output);
	}
}

bool UMD_ScrapeMeshUVData::IsActiveCell(const int32 CellIndex) const
{
	return Cells.IsValidIndex(CellIndex) && Cells[CellIndex].IsActive();
}

const TMultiMap<int32, int32>& UMD_ScrapeMeshUVData::GetVisualDependents() const
{
    check(IsInGameThread());
    if (VisualPreparation)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_InstallPreparedVisualData);
        // Immediate callers (including travel) complete an unfinished lobby snapshot.
        if (!VisualPreparation->Result.IsValid()) BeginVisualPreparation();
        auto Pending = MoveTemp(VisualPreparation);
        // TFuture::Get returns a const reference; Consume transfers the large arrays once.
        auto Prepared = Pending->Result.Consume();
        if (Pending->Revision == RuntimeCacheRevision && Pending->LayoutId == LayoutId)
        {
            CachedVisualDependents = MoveTemp(Prepared.Dependents);
            CachedVisualSeamOffsets = MoveTemp(Prepared.Offsets);
            CachedVisualSeamSamples = MoveTemp(Prepared.Samples);
            CachedVisualSeamDependentOffsets = MoveTemp(Prepared.ReverseOffsets);
            CachedVisualSeamDependents = MoveTemp(Prepared.Reverse);
            ExactAdjacencyOverrides = MoveTemp(Prepared.Adjacency);
            ExactAdjacencyTriangleData = Triangles.GetData();
            ExactAdjacencyTriangleCount = Triangles.Num();
            ExactAdjacencyLayoutId = LayoutId;
            PreparedMaskLayout = MoveTemp(Prepared.MaskLayout);
            VisualDependentsLayoutId = LayoutId;
            VisualDependentsAtlasDimensions = AtlasDimensions;
            VisualDependentsCellCount = Cells.Num();
        }
    }
    if (VisualDependentsLayoutId != LayoutId
		|| VisualDependentsAtlasDimensions != AtlasDimensions
		|| VisualDependentsCellCount != Cells.Num())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_BuildVisualSeamCache);
		CachedVisualDependents.Reset();
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			const int32 Source = Cells[Index].VisualSourceCellIndex;
			if (Source != INDEX_NONE && Source != Index)
			{
				CachedVisualDependents.Add(Source, Index);
			}
		}
        MD_ScrapeMeshUVSeam::Build(*this, CachedVisualSeamOffsets, CachedVisualSeamSamples,
            CachedVisualSeamDependentOffsets, CachedVisualSeamDependents);
		VisualDependentsLayoutId = LayoutId;
		VisualDependentsAtlasDimensions = AtlasDimensions;
		VisualDependentsCellCount = Cells.Num();
	}
	return CachedVisualDependents;
}

TConstArrayView<FMD_ScrapeMeshUVVisualSample> UMD_ScrapeMeshUVData::GetVisualSeamSamples(int32 PixelIndex) const
{
    GetVisualDependents();
    if (PixelIndex < 0 || PixelIndex + 1 >= CachedVisualSeamOffsets.Num()) return {};
    const int32 Begin = CachedVisualSeamOffsets[PixelIndex];
    const int32 Count = CachedVisualSeamOffsets[PixelIndex + 1] - Begin;
    return Count > 0 ? TConstArrayView<FMD_ScrapeMeshUVVisualSample>(CachedVisualSeamSamples.GetData() + Begin, Count)
        : TConstArrayView<FMD_ScrapeMeshUVVisualSample>();
}

FMD_ScrapeMeshUVVisualSeamView UMD_ScrapeMeshUVData::GetVisualSeamView() const
{
    GetVisualDependents();
    return {MakeArrayView(CachedVisualSeamOffsets), MakeArrayView(CachedVisualSeamSamples)};
}

TConstArrayView<int32> UMD_ScrapeMeshUVData::GetVisualSeamDependents(int32 SourceCellIndex) const
{
    GetVisualDependents();
    if (SourceCellIndex < 0 || SourceCellIndex + 1 >= CachedVisualSeamDependentOffsets.Num()) return {};
    const int32 Begin = CachedVisualSeamDependentOffsets[SourceCellIndex];
    const int32 Count = CachedVisualSeamDependentOffsets[SourceCellIndex + 1] - Begin;
    return Count > 0 ? MakeArrayView(CachedVisualSeamDependents.GetData() + Begin, Count) : TConstArrayView<int32>();
}

void UMD_ScrapeMeshUVData::InvalidateVisualDependentsCache() const
{
    VisualPreparation.Reset();
    PreparedMaskLayout.Reset();
    SharedMaskLayout.Reset();
    ExactAdjacencyOverrides.Empty();
    ExactAdjacencyTriangleData = nullptr;
    ExactAdjacencyTriangleCount = INDEX_NONE;
    ExactAdjacencyLayoutId.Invalidate();
    CarrierWeightCache.Empty();
    CarrierWeightsCellCount = INDEX_NONE;
    CachedCellLayout.Reset();
	CachedVisualDependents.Empty();
    CachedVisualSeamOffsets.Empty();
    CachedVisualSeamDependentOffsets.Empty();
    CachedVisualSeamDependents.Empty();
    CachedVisualSeamSamples.Empty();
	VisualDependentsLayoutId.Invalidate();
	VisualDependentsAtlasDimensions = FIntPoint::ZeroValue;
	VisualDependentsCellCount = INDEX_NONE;
}

UMD_ScrapeMeshUVData* UMD_ScrapeMeshUVData::ResolveCompatibleLayout(const FGuid& RequestedLayout)
{
    if (!RequestedLayout.IsValid()) return nullptr;
    if (RequestedLayout == LayoutId) return this;
    // Never follow arbitrary network/save paths or recursively load a chain of profiles.
    if (RequestedLayout != PreviousLayoutId || PreviousLayoutData.IsNull()) return nullptr;
    UMD_ScrapeMeshUVData* Previous = PreviousLayoutData.LoadSynchronous();
    return Previous && Previous != this && Previous->LayoutId == RequestedLayout
        && Previous->SourceMesh == SourceMesh && Previous->ValidateForRuntime() ? Previous : nullptr;
}

bool UMD_ScrapeMeshUVData::IsValidForMesh(const UStaticMesh* Mesh) const
{
	return Mesh && (Mesh == SourceMesh.Get() || Mesh == BakedRenderMesh.Get())
		&& BakedRenderMesh && LayoutId.IsValid() && ValidCellCount > 0
		&& SourceMesh
#if WITH_EDITORONLY_DATA
		&& SourceMeshLightingGuid == SourceMesh->GetLightingGuid()
#endif
		&& AtlasDimensions.X > 0 && AtlasDimensions.Y > 0
		&& Cells.Num() == static_cast<int64>(AtlasDimensions.X) * AtlasDimensions.Y;
}

bool UMD_ScrapeMeshUVData::FRuntimeValidationKey::Matches(const UMD_ScrapeMeshUVData& Data) const
{
    return Source == Data.SourceMesh && RenderMesh == Data.BakedRenderMesh
        && CellData == Data.Cells.GetData() && TriangleData == Data.Triangles.GetData()
        && CellCount == Data.Cells.Num() && TriangleCount == Data.Triangles.Num()
        && ActiveCount == Data.ValidCellCount && UVChannel == Data.CanonicalUVChannel
        && Dimensions == Data.AtlasDimensions && Layout == Data.LayoutId
        && SourceGuid == Data.SourceMeshLightingGuid && Area == Data.CleanableAreaCm2
        && WeldTolerance == Data.AdjacencyWeldToleranceCm && CellSize == Data.TargetCellSizeCm
        && CarrierSize == Data.CarrierReferenceCellSizeCm;
}

void UMD_ScrapeMeshUVData::FRuntimeValidationKey::Capture(const UMD_ScrapeMeshUVData& Data)
{
    Source = Data.SourceMesh; RenderMesh = Data.BakedRenderMesh;
    CellData = Data.Cells.GetData(); TriangleData = Data.Triangles.GetData();
    CellCount = Data.Cells.Num(); TriangleCount = Data.Triangles.Num();
    ActiveCount = Data.ValidCellCount; UVChannel = Data.CanonicalUVChannel;
    Dimensions = Data.AtlasDimensions; Layout = Data.LayoutId; SourceGuid = Data.SourceMeshLightingGuid;
    Area = Data.CleanableAreaCm2; WeldTolerance = Data.AdjacencyWeldToleranceCm; CellSize = Data.TargetCellSizeCm;
    CarrierSize = Data.CarrierReferenceCellSizeCm;
}

void UMD_ScrapeMeshUVData::InvalidateRuntimeCaches() const
{
    ++RuntimeCacheRevision;
    bRuntimeValidationCached = false;
    RuntimeValidationKey = FRuntimeValidationKey();
    InvalidateVisualDependentsCache();
}

void UMD_ScrapeMeshUVData::PostLoad()
{
    Super::PostLoad();
    InvalidateRuntimeCaches();
    // Validate a loaded profile before registration needs it. Cooked data can be
    // checked on the async loader; editor PostLoad stays on the game thread and
    // edit/undo invalidation is unchanged. Validate reads only this profile payload.
    if (!IsTemplate() && HasAnyFlags(RF_WasLoaded) && Validate())
    {
        RuntimeValidationKey.Capture(*this);
        bRuntimeValidationCached = true;
    }
}

bool UMD_ScrapeMeshUVData::IsPostLoadThreadSafe() const
{
    // Editor rebuilds/transactions and their cache invalidation stay on the game thread.
    return !GIsEditor;
}

#if WITH_EDITOR
void UMD_ScrapeMeshUVData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    InvalidateRuntimeCaches();
    Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMD_ScrapeMeshUVData::PostEditUndo()
{
    Super::PostEditUndo();
    InvalidateRuntimeCaches();
}
#endif

bool UMD_ScrapeMeshUVData::ValidateForRuntime(FString* OutError) const
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_ValidateForRuntime);
    const UPackage* Package = GetOutermost();
    const bool bCanCache = HasAnyFlags(RF_WasLoaded) && !HasAnyFlags(RF_Transient)
        && !IsTemplate() && Package && Package != GetTransientPackage()
        && !Package->HasAnyPackageFlags(PKG_NewlyCreated) && !Package->IsDirty();
    if (!bCanCache)
    {
        // A dirty/transient edit must not leave an earlier successful result reusable later.
        bRuntimeValidationCached = false;
        return Validate(OutError);
    }
    if (bRuntimeValidationCached && RuntimeValidationKey.Matches(*this))
    {
        if (OutError) OutError->Reset();
        return true;
    }
    bRuntimeValidationCached = false;
    if (!Validate(OutError)) return false;
    RuntimeValidationKey.Capture(*this);
    bRuntimeValidationCached = true;
    return true;
}

bool UMD_ScrapeMeshUVData::Validate(FString* OutError) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Scrape_ValidateFull);
	auto Fail = [OutError](const TCHAR* Error)
	{
		if (OutError) *OutError = Error;
		return false;
	};
	if (OutError) OutError->Reset();
	if (!SourceMesh || !BakedRenderMesh || !LayoutId.IsValid()
        || !FMath::IsFinite(CarrierReferenceCellSizeCm) || CarrierReferenceCellSizeCm < 0.0
		|| AtlasDimensions.X <= 0 || AtlasDimensions.Y <= 0
		|| AtlasDimensions.X > 1024 || AtlasDimensions.Y > 1024
		|| Cells.Num() != static_cast<int64>(AtlasDimensions.X) * AtlasDimensions.Y
		|| Triangles.IsEmpty() || ValidCellCount <= 0 || CanonicalUVChannel < 1
		|| CanonicalUVChannel > 7)
	{
		return Fail(TEXT("Missing or unsupported baked mesh/atlas metadata."));
	}
	int32 ActiveCount = 0;
	double ActiveArea = 0.0;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FMD_ScrapeMeshUVCell& Cell = Cells[Index];
		if (Cell.IsActive())
		{
			if (!Triangles.IsValidIndex(Cell.TriangleIndex)
				|| !Triangles[Cell.TriangleIndex].bCleanable
				|| Cell.PositionLocal.ContainsNaN() || Cell.Barycentric.ContainsNaN()
				|| !FMath::IsFinite(Cell.AreaCm2) || !FMath::IsFinite(Cell.SupportRadiusCm)
				|| Cell.SupportRadiusCm < 0.0 || Cell.VisualSourceCellIndex != Index
				|| Cell.Barycentric.GetMin() < -1.0e-6
				|| FMath::Abs(Cell.Barycentric.X + Cell.Barycentric.Y + Cell.Barycentric.Z - 1.0) > 1.0e-6)
			{
                const FMD_ScrapeMeshUVTriangle* Owner = Triangles.IsValidIndex(Cell.TriangleIndex) ? &Triangles[Cell.TriangleIndex] : nullptr;
                return Fail(*FString::Printf(TEXT("Invalid active cell %d: triangle=%d source=%d cleanable=%d area=%.17g radius=%.17g visual=%d positionFinite=%d bary=(%.17g,%.17g,%.17g) barySum=%.17g uvDet=%.17g."),
                    Index, Cell.TriangleIndex, Owner ? Owner->SourceTriangleId : INDEX_NONE, Owner && Owner->bCleanable,
                    Cell.AreaCm2, Cell.SupportRadiusCm, Cell.VisualSourceCellIndex, !Cell.PositionLocal.ContainsNaN(),
                    Cell.Barycentric.X, Cell.Barycentric.Y, Cell.Barycentric.Z,
                    Cell.Barycentric.X + Cell.Barycentric.Y + Cell.Barycentric.Z,
                    Owner ? FVector2D::CrossProduct(Owner->UVB - Owner->UVA, Owner->UVC - Owner->UVA) : 0.0));
			}
			++ActiveCount;
			ActiveArea += Cell.AreaCm2;
		}
		else if (Cell.TriangleIndex != INDEX_NONE || Cell.AreaCm2 != 0.0)
		{
			return Fail(TEXT("An inactive cell still owns triangle area."));
		}
		if (Cell.VisualSourceCellIndex != INDEX_NONE && !IsActiveCell(Cell.VisualSourceCellIndex))
		{
			return Fail(TEXT("A visual gutter does not point to an active cell."));
		}
	}
	int32 TriangleCellCount = 0;
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		const FMD_ScrapeMeshUVTriangle& T = Triangles[Index];
		if (T.A.ContainsNaN() || T.B.ContainsNaN() || T.C.ContainsNaN()
			|| T.NormalLocal.ContainsNaN() || !T.NormalLocal.IsNormalized()
			|| !IsFiniteUV(T.UVA) || !IsFiniteUV(T.UVB) || !IsFiniteUV(T.UVC))
		{
			return Fail(TEXT("Invalid original triangle geometry."));
		}
		if (T.bCleanable && T.CellIndices.IsEmpty())
		{
			return Fail(TEXT("A cleanable triangle has no reachable cells."));
		}
		int32 PreviousCell = INDEX_NONE;
		for (int32 CellIndex : T.CellIndices)
		{
			if (CellIndex <= PreviousCell || !IsActiveCell(CellIndex)
				|| Cells[CellIndex].TriangleIndex != Index)
			{
				return Fail(TEXT("Triangle cell lists are not sorted unique ownership lists."));
			}
			PreviousCell = CellIndex;
			++TriangleCellCount;
		}
        if (T.Neighbors.Num() > 2048 || (!T.bCleanable && !T.Neighbors.IsEmpty()))
        { return Fail(TEXT("Invalid partial-edge neighbor count or excluded surface link.")); }
        const double PortalToleranceSquared = FMath::Square(FMath::Max(0.0, AdjacencyWeldToleranceCm) + 1e-8);
        for (const FMD_ScrapeMeshUVNeighbor& Portal : T.Neighbors)
        {
            if (!Triangles.IsValidIndex(Portal.TriangleIndex) || Portal.TriangleIndex == Index
                || !Triangles[Portal.TriangleIndex].bCleanable || Portal.SharedEdgeA.ContainsNaN()
                || Portal.SharedEdgeB.ContainsNaN() || FVector::DistSquared(Portal.SharedEdgeA, Portal.SharedEdgeB) <= 0.0
                || !PortalOnTriangleEdge(T, Portal.SharedEdgeA, Portal.SharedEdgeB, PortalToleranceSquared)
                || !PortalOnTriangleEdge(Triangles[Portal.TriangleIndex], Portal.SharedEdgeA, Portal.SharedEdgeB, PortalToleranceSquared))
            {
                int32 OwnerEdge = INDEX_NONE, OtherEdge = INDEX_NONE;
                const double OwnerDistance = PortalEdgeDistanceSquared(T, Portal.SharedEdgeA, Portal.SharedEdgeB, OwnerEdge);
                const FMD_ScrapeMeshUVTriangle* Other = Triangles.IsValidIndex(Portal.TriangleIndex) ? &Triangles[Portal.TriangleIndex] : nullptr;
                const double OtherDistance = Other ? PortalEdgeDistanceSquared(*Other, Portal.SharedEdgeA, Portal.SharedEdgeB, OtherEdge) : TNumericLimits<double>::Max();
                return Fail(*FString::Printf(TEXT("Invalid partial-edge portal: triangle=%d source=%d neighbor=%d neighborSource=%d cleanable=%d/%d finite=%d/%d length2=%.17g ownerEdge=%d ownerDistance=%.17g neighborEdge=%d neighborDistance=%.17g tolerance=%.17g A=(%.17g,%.17g,%.17g) B=(%.17g,%.17g,%.17g)."),
                    Index, T.SourceTriangleId, Portal.TriangleIndex, Other ? Other->SourceTriangleId : INDEX_NONE,
                    T.bCleanable, Other && Other->bCleanable, !Portal.SharedEdgeA.ContainsNaN(), !Portal.SharedEdgeB.ContainsNaN(),
                    FVector::DistSquared(Portal.SharedEdgeA, Portal.SharedEdgeB), OwnerEdge, FMath::Sqrt(OwnerDistance),
                    OtherEdge, FMath::Sqrt(OtherDistance), FMath::Sqrt(PortalToleranceSquared),
                    Portal.SharedEdgeA.X, Portal.SharedEdgeA.Y, Portal.SharedEdgeA.Z,
                    Portal.SharedEdgeB.X, Portal.SharedEdgeB.Y, Portal.SharedEdgeB.Z));
            }
            bool bReciprocal = false;
            for (const FMD_ScrapeMeshUVNeighbor& Reverse : Triangles[Portal.TriangleIndex].Neighbors)
            {
                if (Reverse.TriangleIndex == Index
                    && Reverse.SharedEdgeA.Equals(Portal.SharedEdgeB, 1e-8)
                    && Reverse.SharedEdgeB.Equals(Portal.SharedEdgeA, 1e-8)) { bReciprocal = true; break; }
            }
            if (!bReciprocal) { return Fail(TEXT("A partial-edge portal is not reciprocal.")); }
        }
		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const int32 Neighbor = T.AdjacentTriangles[Edge];
			if (Neighbor != INDEX_NONE && (!Triangles.IsValidIndex(Neighbor) || Neighbor == Index))
			{
				return Fail(TEXT("Invalid triangle adjacency."));
			}
		}
	}
	if (ActiveCount != ValidCellCount || TriangleCellCount != ActiveCount
		|| !FMath::IsFinite(CleanableAreaCm2) || CleanableAreaCm2 <= 0.0
		|| FMath::Abs(ActiveArea - CleanableAreaCm2) > FMath::Max(0.001, CleanableAreaCm2 * 1.0e-7))
	{
		return Fail(TEXT("Active cells do not cover the cleanable surface exactly."));
	}
	return true;
}

bool FMD_ScrapeMeshUVGeometry::ClosestPointOnSourceTriangle(
    const FMD_ScrapeMeshUVTriangle& Triangle, const FVector& Point, FVector& OutClosest)
{
    return ClosestPointOnPositiveTriangle(Triangle.A, Triangle.B, Triangle.C, Point, OutClosest);
}

bool FMD_ScrapeMeshUVGeometry::ClosestPointOnSourceTriangle(
    const FMD_ScrapeMeshUVTriangle& Triangle, const FTransform& MeshToWorld,
    const FVector& Point, FVector& OutClosest)
{
    return !MeshToWorld.ContainsNaN() && ClosestPointOnPositiveTriangle(
        MeshToWorld.TransformPosition(Triangle.A), MeshToWorld.TransformPosition(Triangle.B),
        MeshToWorld.TransformPosition(Triangle.C), Point, OutClosest);
}

bool FMD_ScrapeMeshUVGeometry::TryCalculateBarycentric(
	const FMD_ScrapeMeshUVTriangle& T, const FVector& Point, FVector& Out)
{
	Out = FVector::ZeroVector;
	const FVector E0 = T.B - T.A, E1 = T.C - T.A, D = Point - T.A;
	// Cross products retain precision for very narrow original triangles.
	const FVector Cross = FVector::CrossProduct(E0, E1);
	const double Denominator = Cross.SquaredLength();
	if (!FMath::IsFinite(Denominator) || Denominator <= 0.0) return false;
	const double B = FVector::DotProduct(FVector::CrossProduct(D, E1), Cross) / Denominator;
	const double C = FVector::DotProduct(FVector::CrossProduct(E0, D), Cross) / Denominator;
	Out = FVector(1.0 - B - C, B, C);
	return !Out.ContainsNaN();
}

bool FMD_ScrapeMeshUVGeometry::RaycastSource(const UMD_ScrapeMeshUVData& Data,
	const FVector& Start, const FVector& End, FMD_ScrapeMeshUVSample& Out, bool bCullBackFaces)
{
	Out = FMD_ScrapeMeshUVSample();
	const FVector Delta = End - Start;
	const double Length = Delta.Length();
	if (Start.ContainsNaN() || End.ContainsNaN() || !FMath::IsFinite(Length) || Length <= UE_SMALL_NUMBER) return false;
	const FVector Direction = Delta / Length;
	double BestDistance = Length + 1.0e-7;
	int32 BestIndex = INDEX_NONE;
	FVector BestBarycentric = FVector::ZeroVector;
	for (int32 Index = 0; Index < Data.Triangles.Num(); ++Index)
	{
		const FMD_ScrapeMeshUVTriangle& T = Data.Triangles[Index];
		if (bCullBackFaces && FVector::DotProduct(Direction, T.NormalLocal) >= -1.0e-10) continue;
		const FVector E0 = T.B - T.A, E1 = T.C - T.A;
		const FVector P = FVector::CrossProduct(Direction, E1);
		const double Det = FVector::DotProduct(E0, P);
        const double DoubleArea = FVector::CrossProduct(E0, E1).Size();
        if (!FMath::IsFinite(Det) || !FMath::IsFinite(DoubleArea) || DoubleArea <= 0.0
            || FMath::Abs(Det) <= DoubleArea * 1.0e-12) continue;
		const FVector D = Start - T.A;
		const double B = FVector::DotProduct(D, P) / Det;
		const FVector Q = FVector::CrossProduct(D, E0);
		const double C = FVector::DotProduct(Direction, Q) / Det;
		const double Distance = FVector::DotProduct(E1, Q) / Det;
		if (B < -1.0e-8 || C < -1.0e-8 || B + C > 1.0 + 1.0e-8
			|| Distance < 0.0 || Distance > Length || Distance > BestDistance + 1.0e-7) continue;
		if (BestIndex != INDEX_NONE && FMath::Abs(Distance - BestDistance) <= 1.0e-7
			&& PreferExistingCoincidentSurface(Data.Triangles[BestIndex], T)) continue;
		BestIndex = Index;
		BestDistance = Distance;
		BestBarycentric = FVector(1.0 - B - C, B, C);
	}
	if (!Data.Triangles.IsValidIndex(BestIndex) || !Data.Triangles[BestIndex].bCleanable) return false;
	SetSample(Data.Triangles[BestIndex], BestIndex, BestBarycentric, Out);
	Out.DistanceCm = BestDistance;
	return true;
}

bool FMD_ScrapeMeshUVGeometry::FindNearestSource(const UMD_ScrapeMeshUVData& Data,
	const FVector& Point, const FVector& Normal, double MaximumDistance,
	FMD_ScrapeMeshUVSample& Out, double MinimumNormalDot)
{
	Out = FMD_ScrapeMeshUVSample();
	if (Point.ContainsNaN() || Normal.ContainsNaN() || !FMath::IsFinite(MaximumDistance)
		|| MaximumDistance < 0.0 || !FMath::IsFinite(MinimumNormalDot)
		|| MinimumNormalDot < -1.0 || MinimumNormalDot > 1.0) return false;
	const FVector SafeNormal = Normal.GetSafeNormal();
	if (SafeNormal.IsNearlyZero()) return false;
	double BestDistanceSquared = FMath::Square(MaximumDistance);
	int32 BestIndex = INDEX_NONE;
	FVector BestPoint = FVector::ZeroVector;
	for (int32 Index = 0; Index < Data.Triangles.Num(); ++Index)
	{
		const FMD_ScrapeMeshUVTriangle& T = Data.Triangles[Index];
		if (FVector::DotProduct(SafeNormal, T.NormalLocal) < MinimumNormalDot) continue;
        FVector Closest;
        if (!ClosestPointOnSourceTriangle(T, Point, Closest)) { continue; }
		const double DistanceSquared = FVector::DistSquared(Point, Closest);
		if (DistanceSquared > BestDistanceSquared) continue;
		if (BestIndex != INDEX_NONE && FMath::Abs(DistanceSquared - BestDistanceSquared) <= 1.0e-12
			&& PreferExistingCoincidentSurface(Data.Triangles[BestIndex], T)) continue;
		BestDistanceSquared = DistanceSquared;
		BestIndex = Index;
		BestPoint = Closest;
	}
	FVector Barycentric;
	if (!Data.Triangles.IsValidIndex(BestIndex) || !Data.Triangles[BestIndex].bCleanable
		|| !TryCalculateBarycentric(Data.Triangles[BestIndex], BestPoint, Barycentric)) return false;
	SetSample(Data.Triangles[BestIndex], BestIndex, Barycentric, Out);
	Out.DistanceCm = FMath::Sqrt(BestDistanceSquared);
	return true;
}

bool FMD_ScrapeMeshUVGeometry::SampleCanonicalUV(const UMD_ScrapeMeshUVData& Data,
	const FVector2D& UV, FMD_ScrapeMeshUVSample& Out)
{
	Out = FMD_ScrapeMeshUVSample();
	if (!IsFiniteUV(UV) || UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0
		|| Data.AtlasDimensions.X <= 0 || Data.AtlasDimensions.Y <= 0) return false;
	const FVector2D Pixel(UV.X * Data.AtlasDimensions.X, UV.Y * Data.AtlasDimensions.Y);
	const int32 X = FMath::Min(FMath::FloorToInt(Pixel.X), Data.AtlasDimensions.X - 1);
	const int32 Y = FMath::Min(FMath::FloorToInt(Pixel.Y), Data.AtlasDimensions.Y - 1);
	// At an exact shared pixel edge, the triangle can own only the cell on its other side.
	for (int32 DY = 0; DY >= -1; --DY)
	{
		for (int32 DX = 0; DX >= -1; --DX)
		{
			if ((DX && FMath::Abs(Pixel.X - FMath::RoundToDouble(Pixel.X)) > 1.0e-7)
				|| (DY && FMath::Abs(Pixel.Y - FMath::RoundToDouble(Pixel.Y)) > 1.0e-7)
				|| X + DX < 0 || Y + DY < 0) continue;
			const int32 CellIndex = (Y + DY) * Data.AtlasDimensions.X + X + DX;
			if (!Data.IsActiveCell(CellIndex)) continue;
			const int32 Index = Data.Cells[CellIndex].TriangleIndex;
			if (!Data.Triangles.IsValidIndex(Index)) continue;
			FVector Barycentric;
			if (UVBarycentric(Data.Triangles[Index], UV, Barycentric)
				&& Barycentric.GetMin() >= -1.0e-7)
			{
				SetSample(Data.Triangles[Index], Index, Barycentric, Out);
				return true;
			}
		}
	}
	return false;
}

bool FMD_ScrapeMeshUVGeometry::GetCellPolygonBarycentric(const UMD_ScrapeMeshUVData& Data,
	int32 CellIndex, TArray<FVector>& OutPolygon)
{
	OutPolygon.Reset();
	if (!Data.Cells.IsValidIndex(CellIndex) || Data.AtlasDimensions.X <= 0 || Data.AtlasDimensions.Y <= 0) return false;
	const int32 Index = Data.Cells[CellIndex].TriangleIndex;
	if (!Data.Triangles.IsValidIndex(Index) || !Data.Triangles[Index].bCleanable) return false;
	const FMD_ScrapeMeshUVTriangle& T = Data.Triangles[Index];
	const int32 X = CellIndex % Data.AtlasDimensions.X, Y = CellIndex / Data.AtlasDimensions.X;
	FCellUVPolygon Polygon = { T.UVA, T.UVB, T.UVC };
	ClipHalfPlane(Polygon, 0, static_cast<double>(X) / Data.AtlasDimensions.X, true);
	ClipHalfPlane(Polygon, 0, static_cast<double>(X + 1) / Data.AtlasDimensions.X, false);
	ClipHalfPlane(Polygon, 1, static_cast<double>(Y) / Data.AtlasDimensions.Y, true);
	ClipHalfPlane(Polygon, 1, static_cast<double>(Y + 1) / Data.AtlasDimensions.Y, false);
	if (Polygon.Num() < 3) return false;
	for (const FVector2D& UV : Polygon)
	{
		FVector Barycentric;
		if (!UVBarycentric(T, UV, Barycentric)) return false;
		OutPolygon.Add(Barycentric);
	}
	return true;
}


bool FMD_ScrapeMeshUVGeometry::GetCellPolygonLocal(const UMD_ScrapeMeshUVData& Data,
    int32 CellIndex, TArray<FVector>& OutPolygon)
{
    // Reuse the caller's scratch storage for both coordinate systems.
    if (!GetCellPolygonBarycentric(Data, CellIndex, OutPolygon)) { OutPolygon.Reset(); return false; }
    const FMD_ScrapeMeshUVTriangle& Triangle = Data.Triangles[Data.Cells[CellIndex].TriangleIndex];
    for (FVector& Barycentric : OutPolygon)
    { Barycentric = Triangle.A + (Triangle.B - Triangle.A) * Barycentric.Y + (Triangle.C - Triangle.A) * Barycentric.Z; }
    return true;
}

TSharedPtr<const FMD_ScrapeCarrierWeights> UMD_ScrapeMeshUVData::GetSharedCellCarrierWeights(
    const FTransform& Transform) const
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_ScrapeSharedCarrierWeights);
    if (Transform.ContainsNaN()) { return nullptr; }
    if (CarrierWeightsLayoutId != LayoutId || CarrierWeightsAtlasDimensions != AtlasDimensions
        || CarrierWeightsCellCount != Cells.Num() || CarrierWeightsTriangleCount != Triangles.Num()
        || CarrierWeightsTargetCellSize != GetCarrierReferenceCellSizeCm())
    {
        CarrierWeightCache.Empty();
        CarrierWeightsLayoutId = LayoutId;
        CarrierWeightsAtlasDimensions = AtlasDimensions;
        CarrierWeightsCellCount = Cells.Num();
        CarrierWeightsTriangleCount = Triangles.Num();
        CarrierWeightsTargetCellSize = GetCarrierReferenceCellSizeCm();
    }
    const FVector Scale = Transform.GetScale3D();
    for (int32 Index = CarrierWeightCache.Num() - 1; Index >= 0; --Index)
    {
        TSharedPtr<const FMD_ScrapeCarrierWeights> Existing = CarrierWeightCache[Index].Weights.Pin();
        if (!Existing) { CarrierWeightCache.RemoveAtSwap(Index); }
        else if (CarrierWeightCache[Index].Scale == Scale) { return Existing; }
    }
    TSharedRef<FMD_ScrapeCarrierWeights> Weights = MakeShared<FMD_ScrapeCarrierWeights>();
    if (!Weights->Initialize(*this, Transform))
    {
        return nullptr;
    }
    FCarrierWeightCacheEntry& Entry = CarrierWeightCache.AddDefaulted_GetRef();
    Entry.Scale = Scale;
    Entry.Weights = Weights;
    return Weights;
}

TSharedPtr<const FMD_ScrapeCellLayout> UMD_ScrapeMeshUVData::GetSharedCellLayout() const
{
    check(IsInGameThread());
    if (!CachedCellLayout || CellLayoutId != LayoutId || CellLayoutAtlasDimensions != AtlasDimensions
        || CachedCellLayout->AtlasToActive.Num() != Cells.Num())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_ScrapeSharedCellLayout);
        TSharedRef<FMD_ScrapeCellLayout> Layout = MakeShared<FMD_ScrapeCellLayout>();
        Layout->AtlasToActive.Init(INDEX_NONE, Cells.Num());
        Layout->ActiveToAtlas.Reserve(ValidCellCount);
        for (int32 AtlasId = 0; AtlasId < Cells.Num(); ++AtlasId)
        {
            if (Cells[AtlasId].IsActive())
            {
                Layout->AtlasToActive[AtlasId] = Layout->ActiveToAtlas.Add(AtlasId);
            }
        }
        CachedCellLayout = Layout;
        CellLayoutId = LayoutId;
        CellLayoutAtlasDimensions = AtlasDimensions;
    }
    return CachedCellLayout;
}
