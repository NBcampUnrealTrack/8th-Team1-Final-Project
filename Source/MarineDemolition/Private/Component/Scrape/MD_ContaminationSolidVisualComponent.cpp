#include "Component/Scrape/MD_ContaminationSolidVisualComponent.h"
#include "MDSolidMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Gameplay/Scrape/MD_ContaminationSolidGeometry.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDContaminationSolid, Log, All);
TRACE_DECLARE_INT_COUNTER(MDSolidUpdatedChunks, TEXT("MD/Solid/UpdatedPrimitives"));
TRACE_DECLARE_INT_COUNTER(MDSolidUpdatedVertices, TEXT("MD/Solid/UpdatedVertices"));
TRACE_DECLARE_INT_COUNTER(MDSolidUpdatedNormals, TEXT("MD/Solid/UpdatedNormals"));
TRACE_DECLARE_INT_COUNTER(MDSolidSurfaceTriangles, TEXT("MD/Solid/SurfaceTriangles"));

namespace MD_ContaminationSolidVisualPrivate
{
    constexpr float WallInsetCm = FMD_ContaminationSolidGeometry::WallInsetCm;

    float FiniteUnit(const float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 1.0f) : 0.0f;
    }

    float LayerAmount(const FMD_ScrapeSurfaceCell& State)
    {
        const float Initial = FiniteUnit(State.InitialContamination);
        const float Base = Initial > UE_SMALL_NUMBER ?
            FMath::Min(FiniteUnit(State.RemainingContamination), Initial) / Initial : 0.0f;
        // Remaining mass controls thickness. Coverage only rounds the cut transition; neither
        // the quantized display mask nor its opacity curve is a geometry input.
        const float BeveledBase = Base * FMath::Lerp(1.0f, Base, 0.35f * FiniteUnit(State.CoveredFraction));
        const float Paint = FiniteUnit(State.TransferredContamination);
        return FMath::Clamp(BeveledBase + Paint, 0.0f, 2.0f);
    }

    bool SameSettings(const FMD_ContaminationSolidSettings& A, const FMD_ContaminationSolidSettings& B)
    {
        return A.SampleSpacingCm == B.SampleSpacingCm &&
            A.bAllowAdaptiveSpacing == B.bAllowAdaptiveSpacing &&
            A.MaximumAdaptiveSpacingCm == B.MaximumAdaptiveSpacingCm && A.MinimumThicknessCm == B.MinimumThicknessCm &&
            A.MaximumThicknessCm == B.MaximumThicknessCm && A.NoiseScaleCm == B.NoiseScaleCm &&
            A.Seed == B.Seed && A.MaximumSurfaceTriangles == B.MaximumSurfaceTriangles &&
            A.TrianglesPerChunk == B.TrianglesPerChunk && A.MaximumChunks == B.MaximumChunks &&
            A.CullDistanceCm == B.CullDistanceCm;
    }

    TSharedPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> FindRenderTemplate(
        const TSharedPtr<const FMD_ContaminationSolidGeometry>& Geometry)
    {
        struct FEntry
        {
            TWeakPtr<const FMD_ContaminationSolidGeometry> Geometry;
            TWeakPtr<const FMDSolidRenderTemplate, ESPMode::ThreadSafe> Template;
        };
        static TArray<FEntry> Cache;
        for (int32 I = Cache.Num() - 1; I >= 0; --I)
        {
            const auto ExistingGeometry = Cache[I].Geometry.Pin();
            const auto ExistingTemplate = Cache[I].Template.Pin();
            if (!ExistingGeometry || !ExistingTemplate) { Cache.RemoveAtSwap(I); continue; }
            if (ExistingGeometry.Get() == Geometry.Get()) return ExistingTemplate;
        }
        TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_BuildSharedRenderTemplate);
        auto Template = MakeShared<FMDSolidRenderTemplate, ESPMode::ThreadSafe>();
        Template->HeightCount = Geometry->Vertices.Num();
        Template->NormalCount = Geometry->Vertices.Num() + Geometry->BoundaryEdgeCount * 2;
        const int32 CornerCount = (Geometry->Triangles.Num() + Geometry->BoundaryEdgeCount * 2) * 3;
        Template->Vertices.Reserve(CornerCount);
        Template->Indices.Reserve(CornerCount);
        auto Emit = [&](const int32 HeightId, const int32 NormalId, const FVector2f UV, const bool bBottom)
        {
            const auto& Source = Geometry->Vertices[HeightId];
            FMDSolidRenderVertex Vertex;
            Vertex.BasePosition = FVector3f(Source.Position - FVector3d(Source.OffsetPerCm) * WallInsetCm);
            Vertex.OffsetPerCm = Source.OffsetPerCm;
            Vertex.UV = UV;
            Vertex.HeightIndex = HeightId;
            Vertex.NormalIndex = NormalId;
            Vertex.HeightScale = bBottom ? 0.0f : 1.0f;
            Template->Indices.Add(Template->Vertices.Add(Vertex));
        };
        int32 SkirtNormal = Geometry->Vertices.Num();
        for (const auto& Chunk : Geometry->Chunks)
        {
            Template->LocalBounds += Chunk.LocalBounds;
            for (const int32 TriangleIndex : Chunk.Triangles)
            {
                const auto& Triangle = Geometry->Triangles[TriangleIndex];
                for (int32 Corner = 0; Corner < 3; ++Corner)
                    Emit(Triangle.Vertices[Corner], Triangle.Vertices[Corner], Triangle.UV[Corner], false);
            }
            for (const FIntPoint Edge : Chunk.BoundaryEdges)
            {
                Emit(Edge.Y, SkirtNormal, FVector2f(1, 1), false);
                Emit(Edge.X, SkirtNormal, FVector2f(0, 1), false);
                Emit(Edge.X, SkirtNormal++, FVector2f(0, 0), true);
                Emit(Edge.Y, SkirtNormal, FVector2f(1, 1), false);
                Emit(Edge.X, SkirtNormal, FVector2f(0, 0), true);
                Emit(Edge.Y, SkirtNormal++, FVector2f(1, 0), true);
            }
        }
        Template->BuildReusedIndices();
        FString Error;
        if (!Template->Validate(Error))
        {
            UE_LOG(LogMDContaminationSolid, Error, TEXT("Invalid shared solid render template: %s"), *Error);
            return nullptr;
        }
        Cache.Add({Geometry, Template});
        return Template;
    }
}

UMD_ContaminationSolidVisualComponent::UMD_ContaminationSolidVisualComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
}

bool UMD_ContaminationSolidVisualComponent::Configure(UStaticMeshComponent* Mesh,
    UMD_ScrapeMeshUVData* Data, UMaterialInterface* Material,
    FMD_ScrapeConstCellView States, const FMD_ContaminationSolidSettings& Settings)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Configure);
    if (!IsValid(Mesh) || !IsValid(Data) || !IsValid(Material) || !GetWorld() ||
        GetWorld()->GetNetMode() == NM_DedicatedServer || States.Num() != Data->Cells.Num())
    { ResetVisuals(); return false; }
    if (Geometry && SurfaceMesh == Mesh && Profile == Data && ActiveLayoutId == Data->LayoutId &&
        ActiveScale.Equals(Mesh->GetComponentScale(), 1.e-6) &&
        MD_ContaminationSolidVisualPrivate::SameSettings(ActiveSettings, Settings))
    {
        SolidMaterial = Material;
        SolidMesh->SetMaterial(0, Material);
        RefreshAll(States);
        return true;
    }
    ResetVisuals();
    FString Error;
    Geometry = FMD_ContaminationSolidGeometry::FindOrBuild(*Data, Mesh->GetComponentScale(), Settings, Error);
    if (!Geometry)
    {
        UE_LOG(LogMDContaminationSolid, Warning, TEXT("%s: solid layer unavailable: %s"), *GetNameSafe(GetOwner()), *Error);
        return false;
    }
    SurfaceMesh = Mesh;
    Profile = Data;
    SolidMaterial = Material;
    ActiveSettings = Settings;
    ActiveScale = Mesh->GetComponentScale();
    ActiveLayoutId = Data->LayoutId;
    ResolvedCullDistanceCm = FMath::IsFinite(Settings.CullDistanceCm) ? FMath::Max(0.0f, Settings.CullDistanceCm) : 6000.0f;
    RenderTemplate = MD_ContaminationSolidVisualPrivate::FindRenderTemplate(Geometry);
    if (!RenderTemplate) { ResetVisuals(); return false; }
    DynamicData = MakeShared<FMDSolidDynamicData, ESPMode::ThreadSafe>();
    DynamicData->Heights.SetNumUninitialized(Geometry->Vertices.Num());
    DynamicData->Normals.SetNumUninitialized(RenderTemplate->NormalCount);
    VertexVisitStamps.Init(0, Geometry->Vertices.Num());
    NormalVisitStamps.Init(0, RenderTemplate->NormalCount);
    int32 BoundaryOffset = 0;
    for (const auto& Chunk : Geometry->Chunks)
    { ChunkBoundaryOffsets.Add(BoundaryOffset); BoundaryOffset += Chunk.BoundaryEdges.Num(); }
    for (int32 Vertex = 0; Vertex < DynamicData->Heights.Num(); ++Vertex)
        DynamicData->Heights[Vertex] = EvaluateVertexHeight(Vertex, States);
    RebuildNormals();
    SolidMesh = NewObject<UMDSolidMeshComponent>(GetOwner(), NAME_None, RF_Transient);
    SolidMesh->SetupAttachment(SurfaceMesh);
    SolidMesh->SetRelativeTransform(FTransform::Identity);
    SolidMesh->SetCullDistance(ResolvedCullDistanceCm);
    if (!SolidMesh->Configure(RenderTemplate, DynamicData, Material)) { ResetVisuals(); return false; }
    SolidMesh->RegisterComponent();
    TRACE_COUNTER_SET(MDSolidSurfaceTriangles, Geometry->Triangles.Num());
    UE_LOG(LogMDContaminationSolid, Log, TEXT("%s: %s"), *GetNameSafe(GetOwner()), *GetSummary());
    return true;
}

float UMD_ContaminationSolidVisualComponent::EvaluateVertexHeight(const int32 VertexIndex,
    FMD_ScrapeConstCellView States) const
{
    double Amount = 0.0;
    for (int32 I = Geometry->SampleOffsets[VertexIndex]; I < Geometry->SampleOffsets[VertexIndex + 1]; ++I)
    {
        const auto& Sample = Geometry->Samples[I];
        if (States.IsValidIndex(Sample.CellIndex)) Amount += Sample.Weight *
            MD_ContaminationSolidVisualPrivate::LayerAmount(States[Sample.CellIndex]);
    }
    return FMath::Min(Geometry->Vertices[VertexIndex].ThicknessCm * float(Amount), Geometry->MaximumThicknessCm * 2.0f);
}

FVector3d UMD_ContaminationSolidVisualComponent::RaisedPosition(const int32 VertexIndex) const
{
    const auto& Vertex = Geometry->Vertices[VertexIndex];
    return Vertex.Position + FVector3d(Vertex.OffsetPerCm) *
        (DynamicData->Heights[VertexIndex] - MD_ContaminationSolidVisualPrivate::WallInsetCm);
}

void UMD_ContaminationSolidVisualComponent::RebuildNormals()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_InitialNormals);
    TArray<FVector3d> Sums;
    Sums.Init(FVector3d::ZeroVector, Geometry->Vertices.Num());
    for (const auto& Triangle : Geometry->Triangles)
    {
        const FIntVector V = Triangle.Vertices;
        const FVector3d A = RaisedPosition(V.X);
        const FVector3d AreaNormal = FVector3d::CrossProduct(RaisedPosition(V.Z) - A, RaisedPosition(V.Y) - A);
        for (int32 Corner = 0; Corner < 3; ++Corner) Sums[V[Corner]] += AreaNormal;
    }
    for (int32 Vertex = 0; Vertex < Sums.Num(); ++Vertex)
        DynamicData->Normals[Vertex] = FVector3f(Sums[Vertex].GetSafeNormal(1.e-30, FVector3d(Geometry->Vertices[Vertex].Normal)));
    for (int32 Chunk = 0; Chunk < Geometry->Chunks.Num(); ++Chunk)
        for (int32 Edge = 0; Edge < Geometry->Chunks[Chunk].BoundaryEdges.Num(); ++Edge)
            UpdateSkirtNormals(Chunk, Edge, false);
}

void UMD_ContaminationSolidVisualComponent::UpdateSkirtNormals(const int32 ChunkIndex, const int32 EdgeIndex, const bool bTrackChanges)
{
    const int32 NormalIndex = Geometry->Vertices.Num() + (ChunkBoundaryOffsets[ChunkIndex] + EdgeIndex) * 2;
    if (bTrackChanges && NormalVisitStamps[NormalIndex] == UpdateStamp) return;
    if (bTrackChanges) NormalVisitStamps[NormalIndex] = UpdateStamp;
    const FIntPoint Edge = Geometry->Chunks[ChunkIndex].BoundaryEdges[EdgeIndex];
    const FVector3d TopA = RaisedPosition(Edge.X), TopB = RaisedPosition(Edge.Y);
    const auto& A = Geometry->Vertices[Edge.X];
    const auto& B = Geometry->Vertices[Edge.Y];
    const FVector3d BottomA = A.Position - FVector3d(A.OffsetPerCm) * MD_ContaminationSolidVisualPrivate::WallInsetCm;
    const FVector3d BottomB = B.Position - FVector3d(B.OffsetPerCm) * MD_ContaminationSolidVisualPrivate::WallInsetCm;
    const FVector3f Normals[2] = {
        FVector3f(FVector3d::CrossProduct(BottomA - TopB, TopA - TopB).GetSafeNormal()),
        FVector3f(FVector3d::CrossProduct(BottomB - TopB, BottomA - TopB).GetSafeNormal())};
    for (int32 I = 0; I < 2; ++I)
    {
        if (bTrackChanges && DynamicData->Normals[NormalIndex + I].Equals(Normals[I], 0.000001f)) continue;
        DynamicData->Normals[NormalIndex + I] = Normals[I];
        if (bTrackChanges) ChangedNormals.Add(NormalIndex + I);
    }
}

void UMD_ContaminationSolidVisualComponent::BeginHeightUpdate()
{
    if (++UpdateStamp == 0)
    {
        for (uint32& Stamp : VertexVisitStamps) Stamp = 0;
        for (uint32& Stamp : NormalVisitStamps) Stamp = 0;
        UpdateStamp = 1;
    }
    ChangedVertices.Reset();
    ChangedNormals.Reset();
    NormalVertices.Reset();
}

void UMD_ContaminationSolidVisualComponent::UpdateNormals()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_UpdateNormals);
    for (const int32 Vertex : ChangedVertices)
        for (int32 I = Geometry->VertexTriangleOffsets[Vertex]; I < Geometry->VertexTriangleOffsets[Vertex + 1]; ++I)
        {
            const FIntVector V = Geometry->Triangles[Geometry->VertexTriangles[I]].Vertices;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const int32 Neighbor = V[Corner];
                if (NormalVisitStamps[Neighbor] == UpdateStamp) continue;
                NormalVisitStamps[Neighbor] = UpdateStamp;
                NormalVertices.Add(Neighbor);
            }
        }
    for (const int32 Vertex : NormalVertices)
    {
        FVector3d Sum = FVector3d::ZeroVector;
        for (int32 I = Geometry->VertexTriangleOffsets[Vertex]; I < Geometry->VertexTriangleOffsets[Vertex + 1]; ++I)
        {
            const FIntVector V = Geometry->Triangles[Geometry->VertexTriangles[I]].Vertices;
            const FVector3d A = RaisedPosition(V.X);
            Sum += FVector3d::CrossProduct(RaisedPosition(V.Z) - A, RaisedPosition(V.Y) - A);
        }
        const FVector3f Normal(Sum.GetSafeNormal(1.e-30, FVector3d(Geometry->Vertices[Vertex].Normal)));
        if (DynamicData->Normals[Vertex].Equals(Normal, 0.000001f)) continue;
        DynamicData->Normals[Vertex] = Normal;
        ChangedNormals.Add(Vertex);
    }
    for (const int32 Vertex : ChangedVertices)
        for (int32 I = Geometry->VertexBoundaryOffsets[Vertex]; I < Geometry->VertexBoundaryOffsets[Vertex + 1]; ++I)
        {
            const FIntPoint Boundary = Geometry->VertexBoundaries[I];
            UpdateSkirtNormals(Boundary.X, Boundary.Y, true);
        }
    TRACE_COUNTER_SET(MDSolidUpdatedNormals, ChangedNormals.Num());
}

void UMD_ContaminationSolidVisualComponent::SubmitUpdates()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Upload);
    SolidMesh->SubmitUpdates(ChangedVertices, ChangedNormals);
    LastUpdatedChunkCount = ChangedVertices.IsEmpty() && ChangedNormals.IsEmpty() ? 0 : 1;
    TRACE_COUNTER_SET(MDSolidUpdatedChunks, LastUpdatedChunkCount);
    TRACE_COUNTER_SET(MDSolidUpdatedVertices, ChangedVertices.Num());
}

void UMD_ContaminationSolidVisualComponent::UpdateCells(TConstArrayView<int32> ChangedCells,
    FMD_ScrapeConstCellView States)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_Update);
    LastUpdatedChunkCount = 0;
    if (!Geometry || !Profile || States.Num() != Profile->Cells.Num() || ChangedCells.IsEmpty()) return;
    BeginHeightUpdate();
    for (const int32 Cell : ChangedCells)
    {
        if (Cell < 0 || Cell >= Geometry->CellVertexOffsets.Num() - 1) continue;
        for (int32 I = Geometry->CellVertexOffsets[Cell]; I < Geometry->CellVertexOffsets[Cell + 1]; ++I)
        {
            const int32 Vertex = Geometry->CellVertices[I];
            if (VertexVisitStamps[Vertex] == UpdateStamp) continue;
            VertexVisitStamps[Vertex] = UpdateStamp;
            const float Height = EvaluateVertexHeight(Vertex, States);
            if (FMath::IsNearlyEqual(DynamicData->Heights[Vertex], Height, 0.00001f)) continue;
            DynamicData->Heights[Vertex] = Height;
            ChangedVertices.Add(Vertex);
        }
    }
    UpdateNormals();
    SubmitUpdates();
}

void UMD_ContaminationSolidVisualComponent::RefreshAll(FMD_ScrapeConstCellView States)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_RefreshAll);
    LastUpdatedChunkCount = 0;
    if (!Geometry || !Profile || States.Num() != Profile->Cells.Num()) return;
    BeginHeightUpdate();
    for (int32 Vertex = 0; Vertex < DynamicData->Heights.Num(); ++Vertex)
    {
        const float Height = EvaluateVertexHeight(Vertex, States);
        if (FMath::IsNearlyEqual(DynamicData->Heights[Vertex], Height, 0.00001f)) continue;
        DynamicData->Heights[Vertex] = Height;
        ChangedVertices.Add(Vertex);
    }
    UpdateNormals();
    SubmitUpdates();
}

float UMD_ContaminationSolidVisualComponent::SampleVisualOffset(const FVector& LocalPosition,
    const FVector2D& CanonicalUV, const FVector& NormalLocal) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_Solid_SampleContact);
    if (!Geometry.IsValid() || !Profile || LocalPosition.ContainsNaN() || CanonicalUV.ContainsNaN() ||
        NormalLocal.ContainsNaN() || CanonicalUV.X < 0.0 || CanonicalUV.Y < 0.0 ||
        CanonicalUV.X >= 1.0 || CanonicalUV.Y >= 1.0) return 0.0f;
    const FIntPoint Dimensions = Profile->AtlasDimensions;
    const int32 CellIndex = FMath::FloorToInt(CanonicalUV.Y * Dimensions.Y) * Dimensions.X +
        FMath::FloorToInt(CanonicalUV.X * Dimensions.X);
    if (!Profile->Cells.IsValidIndex(CellIndex)) return 0.0f;
    const int32 Source = Profile->Cells[CellIndex].TriangleIndex;
    if (!Profile->Triangles.IsValidIndex(Source) || Geometry->CarrierOnlySources[Source]) return 0.0f;
    if (FVector::DotProduct(Profile->Triangles[Source].NormalLocal, NormalLocal) < 0.5) return 0.0f;
    // Cell -> sample vertex -> incident triangles stays local even if one large original
    // triangle was subdivided thousands of times. No source-wide scan runs while aiming.
    float ClosestHeight = 0.0f;
    double ClosestDistance = TNumericLimits<double>::Max();
    for (int32 Mapping = Geometry->CellVertexOffsets[CellIndex];
        Mapping < Geometry->CellVertexOffsets[CellIndex + 1]; ++Mapping)
    {
        const int32 Vertex = Geometry->CellVertices[Mapping];
        const double Distance = ((FVector(Geometry->Vertices[Vertex].Position) - LocalPosition) *
            ActiveScale).SquaredLength();
        if (Distance < ClosestDistance)
        {
            ClosestDistance = Distance;
            ClosestHeight = DynamicData->Heights[Vertex];
        }
        for (int32 I = Geometry->VertexTriangleOffsets[Vertex]; I < Geometry->VertexTriangleOffsets[Vertex + 1]; ++I)
        {
            const FMD_ContaminationSolidTriangle& Triangle = Geometry->Triangles[Geometry->VertexTriangles[I]];
            if (Triangle.SourceTriangle != Source) continue;
            FMD_ScrapeMeshUVTriangle Flat;
            Flat.A = FVector(Geometry->Vertices[Triangle.Vertices.X].Position);
            Flat.B = FVector(Geometry->Vertices[Triangle.Vertices.Y].Position);
            Flat.C = FVector(Geometry->Vertices[Triangle.Vertices.Z].Position);
            FVector Barycentric;
            if (FMD_ScrapeMeshUVGeometry::TryCalculateBarycentric(Flat, LocalPosition, Barycentric) &&
                Barycentric.GetMin() >= -0.001 && Barycentric.GetMax() <= 1.001)
            {
                return FMath::Max(0.0f, float(DynamicData->Heights[Triangle.Vertices.X] * Barycentric.X +
                    DynamicData->Heights[Triangle.Vertices.Y] * Barycentric.Y + DynamicData->Heights[Triangle.Vertices.Z] * Barycentric.Z) -
                    MD_ContaminationSolidVisualPrivate::WallInsetCm);
            }
        }
    }
    // A very thin clipped cell can extend outside its nearest sample vertex's one-ring.
    // The bounded same-cell fallback prevents a pointer jump without a global mesh search.
    if (ClosestDistance <= FMath::Square(Geometry->SampleSpacingCm * 2.0f))
        return FMath::Max(0.0f, ClosestHeight - MD_ContaminationSolidVisualPrivate::WallInsetCm);
    return 0.0f;
}

void UMD_ContaminationSolidVisualComponent::SetVisualsVisible(const bool bVisible)
{
    if (IsValid(SolidMesh)) SolidMesh->SetVisibility(bVisible);
}

void UMD_ContaminationSolidVisualComponent::ResetVisuals()
{
    if (IsValid(SolidMesh)) SolidMesh->DestroyComponent();
    SolidMesh = nullptr;
    Geometry.Reset();
    RenderTemplate.Reset();
    DynamicData.Reset();
    ChunkBoundaryOffsets.Empty();
    VertexVisitStamps.Empty();
    NormalVisitStamps.Empty();
    ChangedVertices.Empty();
    ChangedNormals.Empty();
    NormalVertices.Empty();
    SurfaceMesh = nullptr;
    Profile = nullptr;
    SolidMaterial = nullptr;
    UpdateStamp = 0;
    LastUpdatedChunkCount = 0;
}

void UMD_ContaminationSolidVisualComponent::OnUnregister()
{
    ResetVisuals();
    Super::OnUnregister();
}

int32 UMD_ContaminationSolidVisualComponent::GetSurfaceTriangleCount() const
{
    return Geometry ? Geometry->Triangles.Num() : 0;
}

int64 UMD_ContaminationSolidVisualComponent::GetAllocatedBytes() const
{
    return (DynamicData ? DynamicData->GetAllocatedBytes() : 0) + ChunkBoundaryOffsets.GetAllocatedSize() +
        VertexVisitStamps.GetAllocatedSize() + NormalVisitStamps.GetAllocatedSize() + NormalVertices.GetAllocatedSize() +
        ChangedVertices.GetAllocatedSize() + ChangedNormals.GetAllocatedSize();
}

FString UMD_ContaminationSolidVisualComponent::GetSummary() const
{
    if (!Geometry || !IsValid(SolidMesh)) return TEXT("solid inactive");
    const auto Memory = SolidMesh->GetSolidMemoryStats();
    return FString::Printf(
        TEXT("solid vertices=%d surface_triangles=%d skirt_triangles=%d chunks=%d spacing=%.2fcm shared=%.3fMiB instance=%.3fMiB shared_render_cpu=%.3fMiB shared_render_gpu=%.3fMiB instance_gpu=%.3fMiB pending_upload=%.3fMiB peak_upload=%.3fMiB degenerate_sources=%d carrier_aliases=%d alias_unaligned=%d alias_global_queries=%d alias_influence=0 isolated_sources=%d recovered_sources=%d renderer=GPUDisplacement template_vertices=%d reuse_unique_vertices=%d reuse_index_cpu=%.3fMiB reuse_index_gpu=%.3fMiB proxy_index_mode=%d proxy_referenced_vertices=%d (driver/proxy overhead additional)"),
        Geometry->Vertices.Num(), Geometry->Triangles.Num(), Geometry->BoundaryEdgeCount * 2, GetChunkCount(),
        Geometry->SampleSpacingCm, Geometry->GetAllocatedBytes() / 1048576.0, GetAllocatedBytes() / 1048576.0,
        Memory.SharedCPUBytes / 1048576.0, Memory.SharedGPUBytes / 1048576.0, Memory.DynamicGPUBytes / 1048576.0,
        Memory.PendingUploadBytes / 1048576.0, Memory.PeakPendingUploadBytes / 1048576.0,
        Geometry->DegenerateSourceCount, Geometry->AliasedCarrierCount, Geometry->UnalignedCarrierCount,
        Geometry->GlobalCarrierQueryCount, Geometry->IsolatedSourceCount, Geometry->RecoveredSourceCount,
        Memory.TemplateVertexCount, Memory.ReusedUniqueVertexCount,
        Memory.SharedReusedIndexCPUBytes / 1048576.0, Memory.SharedReusedIndexGPUBytes / 1048576.0,
        Memory.ProxyIndexMode, Memory.ProxyReferencedVertexCount);
}
