#include "Gameplay/Contamination/MD_StainStickerBaker.h"
#include "Gameplay/Contamination/MD_StainSurfaceFit.h"

#if WITH_EDITOR
#include "MD_StainDownhillBridge.h"
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Distance/DistSegment3Triangle3.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DynamicMeshEditor.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "MeshDescription.h"
#include "Operations/MeshBoolean.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "Engine/OverlapResult.h"
#include "Tasks/Task.h"
#include <atomic>

struct FMD_StainSourceSnapshot
{
	TWeakObjectPtr<UStaticMeshComponent> Component;
	TWeakObjectPtr<UStaticMesh> Mesh;
	FTransform Transform;
	FGuid GeometryGuid;
	bool bRequireDecals = true;
	bool IsCurrent() const
	{
		return Component.IsValid() && Component->IsRegistered() && Component->IsVisible()
			&& (!bRequireDecals || Component->bReceivesDecals) && Component->IsCollisionEnabled()
			&& Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
			&& Component->GetStaticMesh() == Mesh.Get() && Mesh.IsValid() && Mesh->GetLightingGuid() == GeometryGuid
			&& Component->GetComponentTransform().Equals(Transform, 0.01);
	}
};

struct FMD_StainStickerSourceCache
{
	struct FTriangle { FVector3f Positions[3]; FVector3f Normal; };
	struct FGeometry { TArray<FTriangle> Triangles; bool bClosedSource = false; bool bRendered = false; };
	struct FEntry
	{
		FGuid Guid;
		TSharedPtr<FGeometry, ESPMode::ThreadSafe> Geometry;
	};
	TMap<TWeakObjectPtr<UStaticMesh>, FEntry> Entries;
	int32 TriangleCount = 0;
};

using namespace UE::Geometry;

namespace MD_StainStickerBake
{
	constexpr int32 MaximumSourceTriangles = 50000;
	constexpr int32 MaximumComponents = 512;
	constexpr int32 MaximumSnapshotTriangles = 250000;
	using FSourceTriangle = FMD_StainStickerSourceCache::FTriangle;
	using FSourceGeometry = FMD_StainStickerSourceCache::FGeometry;
	struct FGeometryInstance
	{
		TSharedPtr<FSourceGeometry, ESPMode::ThreadSafe> Geometry;
		FTransform Frame, Transform;
		FVector BuildScale;
	};
	struct FSourceMesh
	{
		TUniquePtr<FDynamicMesh3> Mesh;
		bool bClosedSource;
	};
	struct FCorner { FVector Position; FVector2D UV; };
	struct FUnfolded { FVector2D UV[3]; };

	// Clip in intrinsic (surface-distance) coordinates, not world projection coordinates.
	TArray<FCorner> Clip(TArray<FCorner> Polygon, int32 Axis, double Limit, double Sign)
	{
		TArray<FCorner> Result;
		for (int32 I = 0; I < Polygon.Num(); ++I)
		{
			const FCorner& A = Polygon[I];
			const FCorner& B = Polygon[(I + 1) % Polygon.Num()];
			const double DA = (A.UV[Axis] - Limit) * Sign, DB = (B.UV[Axis] - Limit) * Sign;
			if (DA <= 0) Result.Add(A);
			if ((DA <= 0) != (DB <= 0))
			{
				const double T = DA / (DA - DB);
				Result.Add({FMath::Lerp(A.Position, B.Position, T), FMath::Lerp(A.UV, B.UV, T)});
			}
		}
		return Result;
	}

	TArray<FVector> ClipToBox(TArray<FVector> Polygon, double Extent)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
			for (double Sign : {-1., 1.})
			{
				TArray<FVector> Result;
				for (int32 I = 0; I < Polygon.Num(); ++I)
				{
					const FVector A = Polygon[I], B = Polygon[(I + 1) % Polygon.Num()];
					const double DA = A[Axis] * Sign - Extent, DB = B[Axis] * Sign - Extent;
					if (DA <= 0) Result.Add(A);
					if ((DA <= 0) != (DB <= 0)) Result.Add(FMath::Lerp(A, B, DA / (DA - DB)));
				}
				Polygon = MoveTemp(Result);
			}
		return Polygon;
	}

	bool ConnectSources(TArray<FSourceMesh>& Sources, double Radius, TArray<int32>& Connected, FString& Error)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_ConnectedEditor);
		int32 Seed = INDEX_NONE;
		double BestDistance = 1.0;
		for (int32 I = 0; I < Sources.Num(); ++I)
			for (int32 Triangle : Sources[I].Mesh->TriangleIndicesItr())
			{
				if (Sources[I].Mesh->GetTriNormal(Triangle).Z < 0.5) continue;
				FVector A, B, C; Sources[I].Mesh->GetTriVertices(Triangle, A, B, C);
				const double Distance = FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector, A, B, C).SquaredLength();
				if (Distance < BestDistance) { BestDistance = Distance; Seed = I; }
			}
		if (Seed == INDEX_NONE) { Error = TEXT("The hit is not on a readable outward-facing static-mesh surface."); return false; }
		TArray<TUniquePtr<FDynamicMeshAABBTree3>> Trees;
		TArray<IMeshSpatial::FQueryOptions> Queries;
		for (const auto& Source : Sources)
		{
			Trees.Add(MakeUnique<FDynamicMeshAABBTree3>(Source.Mesh.Get()));
			const FDynamicMesh3* Mesh = Source.Mesh.Get();
			Queries.Emplace(0.01, [Mesh, Radius](int32 Triangle)
			{
				FVector A, B, C; Mesh->GetTriVertices(Triangle, A, B, C);
				return FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector, A, B, C).SquaredLength() <= Radius * Radius;
			});
		}
		Connected.Add(Seed);
		for (int32 Cursor = 0; Cursor < Connected.Num(); ++Cursor)
		{
			const int32 Current = Connected[Cursor];
			for (int32 Candidate = 0; Candidate < Sources.Num(); ++Candidate)
			{
				if (Connected.Contains(Candidate)) continue;
				double Distance;
				const FIndex2i Pair = Trees[Current]->FindNearestTriangles(*Trees[Candidate], nullptr, Distance, Queries[Current], Queries[Candidate]);
				if (Pair.A >= 0 && Distance <= 0.01) Connected.Add(Candidate);
			}
		}

		return true;
	}

	bool CaptureSources(AActor& Owner, double Radius, TArray<FGeometryInstance>& Instances, FString& Error, TArray<FMD_StainSourceSnapshot>* Snapshots = nullptr, FMD_StainStickerSourceCache* Cache = nullptr, bool bUseRenderMesh = false)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_GatherEditor);
		const FTransform Frame = Owner.GetActorTransform();
		TArray<UStaticMeshComponent*> Components;
		int32 SnapshotTriangles = 0;
		TMap<UStaticMesh*, TSharedPtr<FSourceGeometry, ESPMode::ThreadSafe>> GeometryByMesh;
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MDStainStickerSurfaces), false, &Owner);
		Owner.GetWorld()->OverlapMultiByChannel(Overlaps, Frame.GetLocation(), FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(Radius), Query);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Overlap.GetComponent());
			if (!Component || Cast<UInstancedStaticMeshComponent>(Component) || !Component->IsVisible() || (!bUseRenderMesh && !Component->bReceivesDecals) || !Component->GetStaticMesh()
				|| Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block) continue;
			Components.AddUnique(Component);
		}
		// A stable order makes touching-surface topology deterministic when rebaking.
		Components.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B) { return A.GetPathName() < B.GetPathName(); });
		if (Components.IsEmpty() || Components.Num() > MaximumComponents)
		{
			Error = TEXT("No supported surface, or too many surfaces inside this sticker footprint (limit: 512).");
			return false;
		}
		for (UStaticMeshComponent* Component : Components)
		{
			UStaticMesh* Source = Component->GetStaticMesh();
			const bool bReadRender = bUseRenderMesh && !Source->GetNaniteSettings().bEnabled;
			TSharedPtr<FSourceGeometry, ESPMode::ThreadSafe> Geometry = GeometryByMesh.FindRef(Source);
			if (!Geometry)
			{
				const FGuid Guid = Source->GetLightingGuid();
				if (Cache)
					if (const auto* Entry = Cache->Entries.Find(Source); Entry && Entry->Guid == Guid && Entry->Geometry->bRendered == bReadRender)
						Geometry = Entry->Geometry;
				if (!Geometry && bReadRender)
				{
					const FStaticMeshRenderData* Data=Source->GetRenderData();
					if (!Data || Data->LODResources.IsEmpty()) continue;
					const FStaticMeshLODResources& LOD=Data->LODResources[0];
					const auto Indices=LOD.IndexBuffer.GetArrayView();
					if (Indices.Num()/3 > MaximumSnapshotTriangles)
					{ Error=TEXT("The receiving render mesh exceeds the authoring geometry budget."); return false; }
					Geometry=MakeShared<FSourceGeometry, ESPMode::ThreadSafe>(); Geometry->bRendered=true;
					for (int32 I=0;I<Indices.Num();I+=3)
					{
						FSourceTriangle& Triangle=Geometry->Triangles.AddDefaulted_GetRef();
						for (int32 K=0;K<3;++K) Triangle.Positions[K]=LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[I+K]);
						Triangle.Normal=LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Indices[I]);
					}
				}
				if (!Geometry)
				{
					const FMeshDescription* Description = Source->GetMeshDescription(0);
					if (!Description || Description->Triangles().Num() == 0) continue;
					if (SnapshotTriangles + Description->Triangles().Num() > MaximumSnapshotTriangles)
					{ Error = TEXT("Nearby source meshes exceed the 250,000-triangle snapshot budget. Reduce the sticker size or simplify the source mesh."); return false; }
					Geometry = MakeShared<FSourceGeometry, ESPMode::ThreadSafe>();
					Geometry->Triangles.Reserve(Description->Triangles().Num());
					// Record authored topology before local cropping creates artificial open boundaries.
					Geometry->bClosedSource = Description->Edges().Num() > 0;
					for (const FEdgeID Edge : Description->Edges().GetElementIDs())
						if (Description->GetNumEdgeConnectedTriangles(Edge) != 2)
						{ Geometry->bClosedSource = false; break; }
					const FStaticMeshConstAttributes Attributes(*Description);
					const auto Positions = Attributes.GetVertexPositions();
					const auto Normals = Attributes.GetVertexInstanceNormals();
					for (const FTriangleID ID : Description->Triangles().GetElementIDs())
					{
						const auto Corners = Description->GetTriangleVertexInstances(ID);
						FSourceTriangle& Triangle = Geometry->Triangles.AddDefaulted_GetRef();
						for (int32 K = 0; K < 3; ++K)
							Triangle.Positions[K] = Positions[Description->GetVertexInstanceVertex(Corners[K])];
						Triangle.Normal = Normals[Corners[0]];
					}

				}
				if (Cache)
				{
					// This tool owns a bounded cache of immutable coordinates, never source UObjects.
					if (const auto* Previous = Cache->Entries.Find(Source)) Cache->TriangleCount -= Previous->Geometry->Triangles.Num();
					if (Cache->TriangleCount + Geometry->Triangles.Num() > MaximumSnapshotTriangles)
					{ Cache->Entries.Reset(); Cache->TriangleCount = 0; }
					Cache->Entries.Add(Source, {Guid, Geometry});
					Cache->TriangleCount += Geometry->Triangles.Num();
				}
				SnapshotTriangles += Geometry->Triangles.Num();
				if (SnapshotTriangles > MaximumSnapshotTriangles)
				{ Error = TEXT("Nearby source meshes exceed the 250,000-triangle snapshot budget. Reduce the sticker size."); return false; }
				GeometryByMesh.Add(Source, Geometry);
			}
			const FTransform Transform = Component->GetComponentTransform();
			if (Snapshots) Snapshots->Add({Component, Source, Transform, Source->GetLightingGuid(), !bUseRenderMesh});
			const FVector BuildScale = !Geometry->bRendered && Source->GetNumSourceModels() > 0 ? Source->GetSourceModel(0).BuildSettings.BuildScale3D : FVector::OneVector;
			Instances.Add({Geometry, Frame, Transform, BuildScale});
		}
		if (Instances.IsEmpty()) { Error = TEXT("No readable source surface around this placement."); return false; }
		return true;
	}

	bool BuildSources(const TArray<FGeometryInstance>& Instances, double Radius,
		TArray<FSourceMesh>& Sources, FString& Error, FProgressCancel* Progress = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_ExtractWorker);
		int32 CandidateTriangles = 0;
		for (const FGeometryInstance& Instance : Instances)
		{
			if (Progress && Progress->Cancelled()) return false;
			const FTransform& Frame = Instance.Frame;
			const FTransform& Transform = Instance.Transform;
			const FVector& BuildScale = Instance.BuildScale;
			FDynamicMesh3 Part;
			TMap<FIntVector, int32> Welded;
			auto AddVertex = [&](const FVector& P)
			{
				const FIntVector Key(FMath::RoundToInt(P.X * 1000), FMath::RoundToInt(P.Y * 1000), FMath::RoundToInt(P.Z * 1000));
				const int32* Existing = Welded.Find(Key);
				const int32 Vertex = Existing ? *Existing : Part.AppendVertex(P);
				if (!Existing) Welded.Add(Key, Vertex);
				return Vertex;
			};
			bool bReverse = false, bOrientationKnown = false;
			for (const FSourceTriangle& Triangle : Instance.Geometry->Triangles)
			{
				FVector P[3];
				for (int32 K = 0; K < 3; ++K)
					P[K] = Frame.InverseTransformPositionNoScale(Transform.TransformPosition(FVector(Triangle.Positions[K]) * BuildScale));
				// Surface distance cannot be shorter than straight-line distance. Remote triangles cannot contribute.
				if (Instance.Geometry->Triangles.Num() > 128 && FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector, P[0], P[1], P[2]).SquaredLength() > Radius * Radius) continue;
				if (CandidateTriangles + Part.TriangleCount() >= MaximumSourceTriangles)
				{ Error = TEXT("This sticker footprint contains over 50,000 triangles. Reduce its size."); return false; }

				if (!bOrientationKnown)
				{
					const FVector Cross = FVector::CrossProduct(P[1] - P[0], P[2] - P[0]);
					const FVector Scale = Transform.GetScale3D() * BuildScale;
					const FVector N = Frame.InverseTransformVectorNoScale(Transform.GetRotation().RotateVector(FVector(Triangle.Normal) / Scale));
					if (!N.ContainsNaN() && Cross.SquaredLength() > 1.e-12 && N.SquaredLength() > 1.e-12)
					{ bReverse = FVector::DotProduct(Cross, N) > 0; bOrientationKnown = true; }
				}
				TArray<FVector> Polygon {P[0], P[1], P[2]};
				if (Instance.Geometry->Triangles.Num() > 128) Polygon = ClipToBox(MoveTemp(Polygon), Radius + 1);
				for (int32 K = 1; K + 1 < Polygon.Num(); ++K)
				{
					if (CandidateTriangles + Part.TriangleCount() >= MaximumSourceTriangles)
					{ Error = TEXT("This sticker footprint contains over 50,000 triangles. Reduce its size."); return false; }
					const int32 V[3] = {AddVertex(Polygon[0]), AddVertex(Polygon[K]), AddVertex(Polygon[K + 1])};
					if (V[0] != V[1] && V[1] != V[2] && V[2] != V[0])
						Part.AppendTriangle(V[0], V[bReverse ? 2 : 1], V[bReverse ? 1 : 2]);
				}
			}
			if (Part.TriangleCount() == 0) continue;

			CandidateTriangles += Part.TriangleCount();
			Sources.Add({MakeUnique<FDynamicMesh3>(MoveTemp(Part)), Instance.Geometry->bClosedSource});
		}
		if (Sources.IsEmpty()) { Error = TEXT("No readable source surface around this placement."); return false; }
		return true;
	}

	bool Unfold(const FDynamicMesh3& Mesh, const FVector2D& HalfSize, FMD_StainStickerPatch& Patch, FString& Error);

	bool TryJoin(FDynamicMesh3& Combined, const FDynamicMesh3& Next, double Radius, FProgressCancel* Progress)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_UnionEditor);
		for (const double Tolerance : {0.001, 0.01})
		{
			if (Progress && Progress->Cancelled()) return false;
			FDynamicMesh3 Joined;
			FMeshBoolean Union(&Combined, &Next, &Joined, FMeshBoolean::EBooleanOp::Union);
			Union.Progress = Progress;
			FAxisAlignedBox3d Bounds = Combined.GetBounds(true); Bounds.Contain(Next.GetBounds(true));
			Union.SnapTolerance = Tolerance / FMath::Clamp(Bounds.MaxDim(), 0.01, 1000000.0);
			bool bJoined = Union.Compute();
			if (Progress && Progress->Cancelled()) return false;
			if (!bJoined && !Union.CreatedBoundaryEdges.IsEmpty())
			{
				// Boolean cuts can leave duplicate seam edges a few microns apart on tiled floors.
				// Repair only these cut edges; authored holes and other mesh boundaries stay intact.
				TSet<int32> CutEdges(Union.CreatedBoundaryEdges);
				FMergeCoincidentMeshEdges Weld(&Joined);
				Weld.EdgesToMerge = &CutEdges;
				Weld.MergeVertexTolerance = Tolerance;
				Weld.OnlyUniquePairs = true;
				Weld.Apply();
				bJoined = true;
				for (const int32 Edge : CutEdges)
				{
					if (!Joined.IsEdge(Edge) || !Joined.IsBoundaryEdge(Edge)) continue;
					FVector A, B; Joined.GetEdgeV(Edge, A, B);
					if (FMath::ClosestPointOnSegment(FVector::ZeroVector, A, B).SquaredLength() <= Radius * Radius)
					{ bJoined = false; break; }
				}
			}
			if (bJoined) { Combined = MoveTemp(Joined); return true; }
		}
		return false;
	}

	bool MakeFootprint(const FDynamicMesh3& Surface, const FVector2D& HalfSize, FDynamicMesh3& Footprint, FString& Error)
	{
		FMD_StainStickerPatch Patch;
		Patch.InitialSize = 100; // Temporary UV topology only; no gameplay size is changed.
		if (!Unfold(Surface, HalfSize, Patch, Error)) return false;
		Footprint.Clear();
		for (int32 I = 0; I < Patch.Positions.Num(); I += 3)
		{
			const int32 A = Footprint.AppendVertex(FVector(Patch.Positions[I]));
			const int32 B = Footprint.AppendVertex(FVector(Patch.Positions[I + 1]));
			const int32 C = Footprint.AppendVertex(FVector(Patch.Positions[I + 2]));
			Footprint.AppendTriangle(A, B, C);
		}
		return true;
	}

	bool MakeSurface(TArray<FSourceMesh>& Sources, const FVector2D& HalfSize, FDynamicMesh3& Combined,
		TArray<int32>& Disconnected, FString& Error, FProgressCancel* Progress = nullptr)
	{
		const double Radius = HalfSize.Length() + 1;
		TArray<int32> Connected;
		FMinimalBoxMeshGenerator BoxGenerator;
		BoxGenerator.Box = FOrientedBox3d(FVector3d::Zero(), FVector3d(Radius + 1));
		FDynamicMesh3 ClipBox(&BoxGenerator.Generate());
		for (const auto& Source : Sources)
		{
			if (Progress && Progress->Cancelled()) return false;
			FDynamicMesh3& Part = *Source.Mesh;
			// Clip large closed floor/wall solids, including caps, outside the reachable sticker area.
			// This preserves winding classification while preventing kilometre-sized triangles in the local union.
			if (Part.IsClosed() && Part.GetBounds(true).MaxDim() > (Radius + 1) * 2)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_ClipEditor);
				FDynamicMesh3 Clipped;
				FMeshBoolean Clip(&Part, FTransformSRT3d::Identity(), &ClipBox, FTransformSRT3d::Identity(),
					&Clipped, FMeshBoolean::EBooleanOp::Intersect);
				Clip.Progress = Progress;
				Clip.SnapTolerance = 0.001 / FMath::Clamp(FMath::Max(Part.GetBounds(true).MaxDim(), (Radius + 1) * 2), 0.01, 1000000.0);
				if (Clip.Compute()) Part = MoveTemp(Clipped);
			}
		}
		if (Progress && Progress->Cancelled()) return false;
		if (!ConnectSources(Sources, Radius, Connected, Error)) return false;
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
			if (!Connected.Contains(Index)) Disconnected.Add(Index);
		Combined = *Sources[Connected[0]].Mesh;
		TArray<int32> Deferred;
		// A failed seam can become unambiguous after adjoining tiles have been joined.
		// Defer failures instead of letting an underside contact reject the receiving face.
		for (int32 Index = 1; Index < Connected.Num(); ++Index)
		{
			if (Progress && Progress->Cancelled()) return false;
			const FSourceMesh& Source = Sources[Connected[Index]];
			// Open decorations (grass cards, uncapped coral) do not define a solid interior.
			// Their failed volume union must not veto a valid receiving face. The hit source
			// is always retained as the seed, so an open mesh can still receive its own sticker.
			if (!TryJoin(Combined, *Source.Mesh, Radius, Progress) && Source.bClosedSource) Deferred.Add(Connected[Index]);
			if (Combined.TriangleCount() > MaximumSourceTriangles)
			{ Error = TEXT("Joined sticker surface exceeds 50,000 triangles. Reduce the sticker size."); return false; }
		}
		if (!Deferred.IsEmpty())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_RelevantSeamsWorker);
			FDynamicMesh3 Footprint;
			if (!MakeFootprint(Combined, HalfSize, Footprint, Error)) return false;
			FDynamicMeshAABBTree3 FootprintTree(&Footprint);
			for (int32 Cursor = 0; Cursor < Deferred.Num();)
			{
				const int32 Index = Deferred[Cursor];
				if (Progress && Progress->Cancelled()) return false;
				const FDynamicMesh3& Next = *Sources[Index].Mesh;
				FDynamicMeshAABBTree3 NextTree(&Next);
				double Distance;
				const FIndex2i Contact = FootprintTree.FindNearestTriangles(NextTree, nullptr, Distance, IMeshSpatial::FQueryOptions(0.01));
				// A nearby object touching only the hidden underside cannot affect this unfolded footprint.
				if (Contact.A < 0 || Distance > 0.01) { ++Cursor; continue; }
				if (!TryJoin(Combined, Next, Radius, Progress))
				{ Error = TEXT("Touching solid surfaces could not be connected inside the sticker footprint."); return false; }
				if (Combined.TriangleCount() > MaximumSourceTriangles)
				{ Error = TEXT("Joined sticker surface exceeds 50,000 triangles. Reduce the sticker size."); return false; }
				if (!MakeFootprint(Combined, HalfSize, Footprint, Error)) return false;
				FootprintTree.Build();
				Deferred.RemoveAt(Cursor);
				Cursor = 0; // A recovered bridge can make an earlier deferred surface reachable.
			}
		}
		return true;
	}

	bool Unfold(const FDynamicMesh3& Mesh, const FVector2D& HalfSize, FMD_StainStickerPatch& Patch, FString& Error)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_UnfoldEditor);
		int32 Seed = INDEX_NONE;
		double BestDistance = 1.0;
		FVector Center;
		for (int32 ID : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriNormal(ID).Z < 0.5) continue;
			FVector A, B, C; Mesh.GetTriVertices(ID, A, B, C);
			const FVector P = FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector, A, B, C);
			if (P.SquaredLength() < BestDistance) { BestDistance = P.SquaredLength(); Seed = ID; Center = P; }
		}
		if (Seed == INDEX_NONE) { Error = TEXT("The placement point is not on a readable outward-facing surface."); return false; }
		const FVector N = Mesh.GetTriNormal(Seed);
		const FVector U = (FVector::ForwardVector - N * N.X).GetSafeNormal();
		const FVector V = FVector::CrossProduct(N, U);
		TMap<int32, FUnfolded> Coordinates;
		FUnfolded First;
		const FIndex3i SeedVertices = Mesh.GetTriangle(Seed);
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector Delta = Mesh.GetVertex(SeedVertices[K]) - Center;
			First.UV[K] = FVector2D(FVector::DotProduct(Delta, U), FVector::DotProduct(Delta, V));
		}
		Coordinates.Add(Seed, First);
		TArray<int32> Queue { Seed };
		for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
		{
			if (Queue.Num() > MaximumSourceTriangles) { Error = TEXT("Sticker traversal exceeded its topology budget."); return false; }
			const int32 ID = Queue[Cursor];
			const FIndex3i Vertices = Mesh.GetTriangle(ID);
			const FUnfolded Current = Coordinates.FindChecked(ID);
			TArray<FCorner> Polygon;
			for (int32 K = 0; K < 3; ++K) Polygon.Add({Mesh.GetVertex(Vertices[K]), Current.UV[K]});
			Polygon = Clip(MoveTemp(Polygon), 0, HalfSize.X, 1);
			Polygon = Clip(MoveTemp(Polygon), 0, -HalfSize.X, -1);
			Polygon = Clip(MoveTemp(Polygon), 1, HalfSize.Y, 1);
			Polygon = Clip(MoveTemp(Polygon), 1, -HalfSize.Y, -1);
			if (Polygon.Num() < 3) continue;
			for (int32 K = 1; K + 1 < Polygon.Num(); ++K)
			{
				const FCorner Triangle[] = {Polygon[0], Polygon[K], Polygon[K + 1]};
				const FVector3f A(Triangle[0].Position), B(Triangle[1].Position), C(Triangle[2].Position);
				if (FVector3f::CrossProduct(B - A, C - A).SquaredLength() <= 1.e-8f
					|| (B - A).SquaredLength() <= 1.e-8f || (C - A).SquaredLength() <= 1.e-8f || (C - B).SquaredLength() <= 1.e-8f) continue;
				if (Patch.Positions.Num() + 3 > FMD_StainStickerPatch::MaximumTriangles * 3) { Error = TEXT("Sticker patch exceeds 4096 triangles."); return false; }
				for (const FCorner& Corner : Triangle)
				{
					Patch.Positions.Add(FVector3f(Corner.Position));
					Patch.UVs.Add(FVector2f(Corner.UV / (HalfSize * 2) + FVector2D(0.5)));
					Patch.LocalBounds += Corner.Position;
				}
			}
			const FIndex3i Neighbors = Mesh.GetTriNeighbourTris(ID);
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const int32 Neighbor = Neighbors[Edge];
				if (Neighbor < 0 || Coordinates.Contains(Neighbor)) continue;
				const FIndex3i NextVertices = Mesh.GetTriangle(Neighbor);
				int32 SharedOld[2], SharedNew[2], Shared = 0, NewCorner = INDEX_NONE, OldCorner = INDEX_NONE;
				for (int32 K = 0; K < 3; ++K)
				{
					const int32 Old = Vertices.IndexOf(NextVertices[K]);
					if (Old < 0) NewCorner = K;
					else if (Shared < 2) { SharedOld[Shared] = Old; SharedNew[Shared++] = K; }
				}
				if (Shared != 2 || NewCorner < 0) continue;
				for (int32 K = 0; K < 3; ++K) if (K != SharedOld[0] && K != SharedOld[1]) OldCorner = K;
				const FVector A = Mesh.GetVertex(NextVertices[SharedNew[0]]), B = Mesh.GetVertex(NextVertices[SharedNew[1]]), C = Mesh.GetVertex(NextVertices[NewCorner]);
				const double Length = FVector::Distance(A, B);
				if (Length < 1.e-6) continue;
				const double Along = (FVector::DistSquared(C, A) + Length * Length - FVector::DistSquared(C, B)) / (2 * Length);
				const double Height = FMath::Sqrt(FMath::Max(0., FVector::DistSquared(C, A) - Along * Along));
				const FVector2D UV0 = Current.UV[SharedOld[0]], UV1 = Current.UV[SharedOld[1]];
				const FVector2D Direction = (UV1 - UV0).GetSafeNormal(), Perpendicular(-Direction.Y, Direction.X);
				const double Side = FVector2D::CrossProduct(Direction, Current.UV[OldCorner] - UV0) >= 0 ? -1 : 1;
				FUnfolded Next;
				Next.UV[SharedNew[0]] = UV0; Next.UV[SharedNew[1]] = UV1;
				Next.UV[NewCorner] = UV0 + Direction * Along + Perpendicular * (Side * Height);
				Coordinates.Add(Neighbor, Next); Queue.Add(Neighbor);
			}
		}
		return Patch.IsValid();
	}

	bool TryAddDownhillBridges(FDynamicMesh3& Surface, const TArray<FSourceMesh>& Sources, const TArray<int32>& Disconnected, const FVector2D& HalfSize, FString& Error, FProgressCancel* Progress, bool bOverlapLandings, int32& UnjoinedCount)
	{
		UnjoinedCount = INDEX_NONE;
		TArray<FDynamicMesh3> Wedges;
		const double Radius = HalfSize.Length() + 1;
		// Search lower receivers across air gaps without making unrelated surfaces reachable by unfolding.
		FDynamicMesh3 LandingSurface;
		const FDynamicMesh3* Search = &Surface;
		if (!Disconnected.IsEmpty())
		{
			LandingSurface = Surface;
			FDynamicMeshEditor Editor(&LandingSurface);
			for (int32 Index : Disconnected)
			{
				if (Progress && Progress->Cancelled()) return false;
				if (LandingSurface.TriangleCount() + Sources[Index].Mesh->TriangleCount() > MaximumSourceTriangles)
				{ Error = TEXT("Descending landing search exceeds 50,000 triangles. Reduce the sticker size."); return false; }
				FMeshIndexMappings Mappings; Editor.AppendMesh(Sources[Index].Mesh.Get(), Mappings);
			}
			Search = &LandingSurface;
		}
		if (!FMD_StainDownhillBridge::Gather(Surface, *Search, HalfSize, Wedges, Error, Progress, bOverlapLandings)) return false;
		TSet<int32> JoinedLandings;
		// Broad connections resolve intersecting tile corners before their short seam fragments.
		Wedges.Sort([](const FDynamicMesh3& A, const FDynamicMesh3& B)
		{ return A.GetBounds().DiagonalLength() > B.GetBounds().DiagonalLength(); });
		TArray<int32> Pending;
		for (int32 Index = 0; Index < Wedges.Num(); ++Index) Pending.Add(Index);
		for (int32 Cursor = 0; Cursor < Pending.Num();)
		{
			if (Progress && Progress->Cancelled()) return false;
			const FDynamicMesh3& Wedge = Wedges[Pending[Cursor]];
			if (!TryJoin(Surface, Wedge, Radius, Progress)) { ++Cursor; continue; }

			// Only a receiver touched by an accepted bridge becomes part of the sticker surface.
			FDynamicMeshAABBTree3 WedgeTree(&Wedge);
			for (int32 Index : Disconnected)
			{
				if (Progress && Progress->Cancelled()) return false;
				if (JoinedLandings.Contains(Index)) continue;
				FDynamicMeshAABBTree3 ReceiverTree(Sources[Index].Mesh.Get());
				double Distance;
				const FIndex2i Contact = WedgeTree.FindNearestTriangles(ReceiverTree, nullptr, Distance, IMeshSpatial::FQueryOptions(0.01));
				if (Contact.A < 0 || Distance > 0.01) continue;
				if (!TryJoin(Surface, *Sources[Index].Mesh, Radius, Progress))
				{ Error = TEXT("The descending sticker could not connect to its lower receiver."); return false; }
				JoinedLandings.Add(Index);
			}
			if (Surface.TriangleCount() > MaximumSourceTriangles)
			{ Error = TEXT("Descending sticker surface exceeds 50,000 triangles. Reduce the sticker size."); return false; }
			Pending.RemoveAt(Cursor);
			Cursor = 0;
		}
		if (!Pending.IsEmpty())
		{
			// A short, ambiguous corner must not discard the independent descents already joined.
			if (Pending.Num() < Wedges.Num()) UnjoinedCount = Pending.Num();
			Error = TEXT("The descending sticker bridge could not be joined to its landing."); return false;
		}
		return true;
	}

	bool AddDownhillBridges(FDynamicMesh3& Surface, const TArray<FSourceMesh>& Sources, const TArray<int32>& Disconnected,
		const FVector2D& HalfSize, FString& Error, FProgressCancel* Progress = nullptr)
	{
		TUniquePtr<FDynamicMesh3> Partial;
		int32 FewestUnjoined = TNumericLimits<int32>::Max();
		// Retry ambiguous contacts from the original surface, without mixing incompatible operands.
		for (bool bOverlapLandings : {false,true})
		{
			if (Progress && Progress->Cancelled()) return false;
			FDynamicMesh3 Candidate = Surface;
			int32 Unjoined = INDEX_NONE;
			Error.Reset();
			if (TryAddDownhillBridges(Candidate,Sources,Disconnected,HalfSize,Error,Progress,bOverlapLandings,Unjoined))
			{ Surface = MoveTemp(Candidate); return true; }
			if (Unjoined >= 0 && Unjoined < FewestUnjoined)
			{ FewestUnjoined = Unjoined; Partial = MakeUnique<FDynamicMesh3>(MoveTemp(Candidate)); }
		}
		if (Progress && Progress->Cancelled()) return false;
		// Preserve original wrapping only at unresolved corners; valid bridges and receiving faces remain usable.
		if (Partial) { Surface = MoveTemp(*Partial); Error.Reset(); return true; }
		return false;
	}

	bool ClipPortal(const FVector& A, const FVector& B, const FBox& Bounds, FVector& Start, FVector& End)
	{
		const FVector D = B - A;
		double MinT = 0, MaxT = 1;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(D[Axis]) < 1.e-12)
			{
				if (A[Axis] < Bounds.Min[Axis] || A[Axis] > Bounds.Max[Axis]) return false;
				continue;
			}
			const double T0 = (Bounds.Min[Axis] - A[Axis]) / D[Axis];
			const double T1 = (Bounds.Max[Axis] - A[Axis]) / D[Axis];
			MinT = FMath::Max(MinT, FMath::Min(T0, T1));
			MaxT = FMath::Min(MaxT, FMath::Max(T0, T1));
			if (MinT > MaxT) return false;
		}
		Start = A + D * MinT; End = A + D * MaxT;
		return true;
	}

	// A planar chord needs interval coverage, not a branching path through every
	// tessellation edge. Every covered interval lies on a real convex source face;
	// a hole or a different parallel layer cannot contribute an interval.
	bool ProvePlanarChord(const FDynamicMesh3& Mesh, const FDynamicMeshAABBTree3& Tree,
		const FVector& From, const FVector& To, TSet<int32>& UsedFaces, FProgressCancel* Progress)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_PlanarContinuity);
		constexpr double PlaneTolerance = 1.e-6;
		constexpr double ParameterTolerance = 1.e-10;
		struct FInterval { double Start, End; int32 Triangle; };
		TArray<FInterval, TInlineAllocator<64>> Intervals;
		const FAxisAlignedBox3d Bounds(From.ComponentMin(To), From.ComponentMax(To));
		int32 BoxVisits = 0, FaceQueries = 0;
		bool bLimitReached = false;
		FDynamicMeshAABBTree3::FTreeTraversal Traversal;
		Traversal.NextBoxF = [&](const FAxisAlignedBox3d& Box, int)
		{
			if (++BoxVisits > 4096) bLimitReached = true;
			FVector Start, End;
			return !bLimitReached && !(Progress && Progress->Cancelled())
				&& Box.DistanceSquared(Bounds) <= PlaneTolerance * PlaneTolerance
				&& ClipPortal(From,To,FBox(Box.Min,Box.Max).ExpandBy(PlaneTolerance),Start,End);
		};
		Traversal.NextTriangleF = [&](int32 Triangle)
		{
			if (bLimitReached) return;
			if (++FaceQueries > 512) { bLimitReached = true; return; }
			FVector A, B, C; Mesh.GetTriVertices(Triangle, A, B, C);
			const FVector AB = B-A, AC = C-A, Normal = FVector::CrossProduct(AB,AC).GetSafeNormal();
			if (Normal.IsNearlyZero() || FMath::Abs(FVector::DotProduct(From-A,Normal)) > PlaneTolerance
				|| FMath::Abs(FVector::DotProduct(To-A,Normal)) > PlaneTolerance) return;
			const double D00 = AB.SquaredLength(), D01 = FVector::DotProduct(AB,AC), D11 = AC.SquaredLength();
			const double Denominator = D00*D11-D01*D01;
			if (Denominator <= 1.e-20) return;
			auto Barycentric = [&](const FVector& Point)
			{
				const double D20 = FVector::DotProduct(Point-A,AB), D21 = FVector::DotProduct(Point-A,AC);
				const double V = (D11*D20-D01*D21)/Denominator, W = (D00*D21-D01*D20)/Denominator;
				return FVector(1.-V-W,V,W);
			};
			const FVector StartBary = Barycentric(From), EndBary = Barycentric(To);
			double Start = 0., End = 1.;
			for (int32 K=0; K<3; ++K)
			{
				const double Delta = EndBary[K]-StartBary[K];
				if (FMath::Abs(Delta) <= 1.e-15)
				{
					if (StartBary[K] < -ParameterTolerance) return;
					continue;
				}
				const double Crossing = -StartBary[K]/Delta;
				if (Delta > 0.) Start = FMath::Max(Start,Crossing);
				else End = FMath::Min(End,Crossing);
				if (Start > End+ParameterTolerance) return;
			}
			Intervals.Add({FMath::Clamp(Start,0.,1.),FMath::Clamp(End,0.,1.),Triangle});
		};
		Tree.DoTraversal(Traversal);
		if (bLimitReached || (Progress && Progress->Cancelled())) return false;
		Intervals.Sort([](const FInterval& A, const FInterval& B)
		{
			if (A.Start != B.Start) return A.Start < B.Start;
			if (A.End != B.End) return A.End > B.End;
			return A.Triangle < B.Triangle;
		});
		double Covered = 0.;
		TArray<int32, TInlineAllocator<64>> Route;
		for (const FInterval& Interval : Intervals)
		{
			if (Interval.Start > Covered+ParameterTolerance) return false;
			if (Interval.End <= Covered) continue;
			Covered = Interval.End; Route.Add(Interval.Triangle);
			if (Covered >= 1.-ParameterTolerance)
			{
				for (int32 Triangle : Route) UsedFaces.Add(Triangle);
				return true;
			}
		}
		return false;
	}

	// Prove only a short discontinuous projection. Every accepted link lies on a real
	// shared edge or has an actual edge-to-face contact within 0.05 cm.
	bool ProveLocalRoute(const FDynamicMesh3& Mesh, const FDynamicMeshAABBTree3& Tree,
		int32 FromTriangle, const FVector& From, int32 ToTriangle, const FVector& To, double Step,
		TSet<int32>& UsedFaces, bool& bBudgetExceeded, FProgressCancel* Progress, FString* Failure = nullptr)
	{
		auto Reject=[&](const FString& Reason) { if (Failure) *Failure=Reason; return false; };
		constexpr double ContactTolerance = 0.05;
		const double MaximumLength = Step + 2.05;
		if (!Mesh.IsTriangle(FromTriangle) || !Mesh.IsTriangle(ToTriangle)) return Reject(TEXT("Invalid endpoint face"));
		if (FVector::Distance(From, To) > MaximumLength)
			return Reject(FString::Printf(TEXT("Endpoint distance %.9f exceeds route %.9f"),FVector::Distance(From,To),MaximumLength));
		FVector A,B,C; Mesh.GetTriVertices(FromTriangle,A,B,C);
		const FVector FromOnFace=FMath::ClosestPointOnTriangleToPoint(From,A,B,C);
		const double FromFaceDistance=FVector::Distance(From,FromOnFace);
		Mesh.GetTriVertices(ToTriangle,A,B,C);
		const FVector ToOnFace=FMath::ClosestPointOnTriangleToPoint(To,A,B,C);
		const double ToFaceDistance=FVector::Distance(To,ToOnFace);
		if (FromFaceDistance>ContactTolerance || ToFaceDistance>ContactTolerance)
			return Reject(FString::Printf(TEXT("Endpoint outside face: from %.9f, to %.9f cm"),FromFaceDistance,ToFaceDistance));
		if (FromTriangle == ToTriangle)
		{
			if (FromFaceDistance+FVector::Distance(FromOnFace,ToOnFace)+ToFaceDistance>MaximumLength)
				return Reject(TEXT("Same-face route exceeds length"));
			UsedFaces.Add(ToTriangle); return true;
		}
		const FVector FromNormal = Mesh.GetTriNormal(FromTriangle), ToNormal = Mesh.GetTriNormal(ToTriangle);
		if (FMath::Abs(FVector::DotProduct(FromNormal,ToNormal)) > 1.-1.e-10
			&& FMath::Abs(FVector::DotProduct(ToOnFace-FromOnFace,FromNormal)) <= 1.e-6
			&& FromFaceDistance+FVector::Distance(FromOnFace,ToOnFace)+ToFaceDistance <= MaximumLength
			&& ProvePlanarChord(Mesh,Tree,FromOnFace,ToOnFace,UsedFaces,Progress))
		{ UsedFaces.Add(FromTriangle); UsedFaces.Add(ToTriangle); return true; }
		FString BudgetKind;
		bool bSearchBudgetExceeded = false;
		auto Search = [&](bool bCheckContacts, int32 MaximumStates)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_LocalContinuity);
			bSearchBudgetExceeded = false; BudgetKind.Reset();
			const FBox Corridor = FBox(From.ComponentMin(To), From.ComponentMax(To)).ExpandBy(ContactTolerance);
			struct FVisit { int32 Triangle; FVector Entry; double Length; int32 Parent; };
			TArray<FVisit, TInlineAllocator<64>> Open;
			TArray<FVisit, TInlineAllocator<64>> Expanded;
			TSet<int32> ExpandedFaces;
			struct FContact { int32 Triangle; FVector EdgePoint, FacePoint; };
			// A clipped edge is fixed for this corridor. Cache its actual contacts so
			// alternate entries do not spend the query budget on the same geometry.
			TMap<int32,TArray<FContact>> ContactCache;
			Open.Add({FromTriangle, FromOnFace, FromFaceDistance, INDEX_NONE});
			int32 BoxVisits = 0, FaceQueries = 0;
			double BestEndLength=TNumericLimits<double>::Max();
			bool bFoundRoute=false;
			auto Enqueue = [&](int32 Triangle, const FVector& Entry, double Length, int32 Parent)
			{
				if (bFoundRoute || bSearchBudgetExceeded || Length+FVector::Distance(Entry,To)>MaximumLength) return;
				// A shortest valid route never needs to leave and re-enter a convex face.
				// This is path-local: another route may still enter that face elsewhere.
				for (int32 Ancestor=Parent;Ancestor>=0;Ancestor=Expanded[Ancestor].Parent)
					if (Expanded[Ancestor].Triangle==Triangle) return;
				FVector A,B,C; Mesh.GetTriVertices(Triangle,A,B,C);
				const FVector End=FMath::ClosestPointOnTriangleToPoint(To,A,B,C);
				const double EndGap=FVector::Distance(End,To);
				if (EndGap<=ContactTolerance && Length+FVector::Distance(Entry,End)+EndGap<=MaximumLength)
				{
					UsedFaces.Add(Triangle); UsedFaces.Add(ToTriangle);
					for (int32 Ancestor=Parent;Ancestor>=0;Ancestor=Expanded[Ancestor].Parent) UsedFaces.Add(Expanded[Ancestor].Triangle);
					bFoundRoute=true; return;
				}
				// A previous entry dominates only if reaching this entry through the same
				// convex face is no more expensive. A cheaper entry alone is insufficient.
				for (const FVisit& Visited : Expanded)
					if (Visited.Triangle==Triangle && Visited.Length+FVector::Distance(Visited.Entry,Entry)<=Length) return;
				for (int32 I=Open.Num()-1; I>=0; --I)
				{
					const FVisit& Pending = Open[I];
					if (Pending.Triangle != Triangle) continue;
					const double InsideFace = FVector::Distance(Pending.Entry,Entry);
					if (Pending.Length+InsideFace<=Length) return;
					if (Length+InsideFace<=Pending.Length) Open.RemoveAtSwap(I);
				}
				if (Open.Num() + Expanded.Num() >= MaximumStates)
				{ bSearchBudgetExceeded = true; BudgetKind=TEXT("route state"); return; }
				Open.Add({Triangle, Entry, Length, Parent});
			};
			auto RejectBudget = [&]
			{
				return Reject(FString::Printf(TEXT("Local continuity %s limit: states %d/%d, contacts %d/512, tree boxes %d/4096; route allowance %.3f cm."),
					*BudgetKind,Open.Num()+Expanded.Num(),MaximumStates,FaceQueries,BoxVisits,MaximumLength));
			};
			while (!Open.IsEmpty())
			{
				if (Progress && Progress->Cancelled()) return Reject(TEXT("Local continuity cancelled."));
				if (bSearchBudgetExceeded) return RejectBudget();
				int32 Best = 0;
				for (int32 I = 1; I < Open.Num(); ++I)
					if (Open[I].Length+FVector::Distance(Open[I].Entry,To)<Open[Best].Length+FVector::Distance(Open[Best].Entry,To)) Best = I;
				const FVisit Current = Open[Best]; Open.RemoveAtSwap(Best);
				const int32 CurrentIndex=Expanded.Add(Current); ExpandedFaces.Add(Current.Triangle);
				Mesh.GetTriVertices(Current.Triangle,A,B,C);
				const FVector End=FMath::ClosestPointOnTriangleToPoint(To,A,B,C);
				const double EndGap=FVector::Distance(End,To);
				const double EndLength=Current.Length+FVector::Distance(Current.Entry,End)+EndGap;
				if (EndGap<=ContactTolerance) BestEndLength=FMath::Min(BestEndLength,EndLength);
				// A ray landing exactly on a shared edge/vertex may name the other incident
				// face. Accept only the actual coincident endpoint, charging its contact gap.
				if (EndGap<=ContactTolerance && EndLength<=MaximumLength)
				{
					UsedFaces.Add(Current.Triangle);
					for (int32 Ancestor=Current.Parent; Ancestor>=0; Ancestor=Expanded[Ancestor].Parent)
						UsedFaces.Add(Expanded[Ancestor].Triangle);
					UsedFaces.Add(ToTriangle);
					return true;
				}
				const FIndex3i Edges = Mesh.GetTriEdges(Current.Triangle);
				for (int32 K = 0; K < 3; ++K)
				{
					FVector3d EdgeA, EdgeB; Mesh.GetEdgeV(Edges[K], EdgeA, EdgeB);
					FVector PortalA, PortalB;
					if (!ClipPortal(EdgeA, EdgeB, Corridor, PortalA, PortalB)) continue;
					FVector Portal = FMath::ClosestPointOnSegment(To, PortalA, PortalB);
					// Preserve the projected route through large authored triangles instead of
					// taking a lateral detour toward the final elevated/recessed point.
					const FVector D=To-Current.Entry,E=PortalB-PortalA,R=PortalA-Current.Entry;
					const double Den=D.X*E.Y-D.Y*E.X;
					if (FMath::Abs(Den)>1.e-12)
					{
						const double T=(R.X*E.Y-R.Y*E.X)/Den,U=(R.X*D.Y-R.Y*D.X)/Den;
						if (T>=-1.e-10 && T<=1+1.e-10 && U>=-1.e-10 && U<=1+1.e-10)
							Portal=PortalA+E*FMath::Clamp(U,0.,1.);
					}
					else if (E.X*E.X+E.Y*E.Y>1.e-12)
					{
						const double U=((To.X-PortalA.X)*E.X+(To.Y-PortalA.Y)*E.Y)/(E.X*E.X+E.Y*E.Y);
						Portal=PortalA+E*FMath::Clamp(U,0.,1.);
					}
					const FIndex2i Pair = Mesh.GetEdgeT(Edges[K]);
					const int32 Neighbor = Pair.A == Current.Triangle ? Pair.B : Pair.A;
					if (Neighbor >= 0) Enqueue(Neighbor, Portal, Current.Length + FVector::Distance(Current.Entry, Portal), CurrentIndex);
					if (bFoundRoute) return true;
					if (bSearchBudgetExceeded) return RejectBudget();
					if (!bCheckContacts) continue;
					auto TryContact = [&](const FContact& Contact)
					{
						FVector A,B,C; Mesh.GetTriVertices(Contact.Triangle,A,B,C);
						FVector EdgePoint=Portal, FacePoint=FMath::ClosestPointOnTriangleToPoint(Portal,A,B,C);
						if (FVector::DistSquared(EdgePoint,FacePoint)>ContactTolerance*ContactTolerance)
						{ EdgePoint=Contact.EdgePoint; FacePoint=Contact.FacePoint; }
						Enqueue(Contact.Triangle,FacePoint,Current.Length+FVector::Distance(Current.Entry,EdgePoint)
							+FVector::Distance(EdgePoint,FacePoint),CurrentIndex);
					};
					if (const TArray<FContact>* Cached=ContactCache.Find(Edges[K]))
					{
						for (const FContact& Contact : *Cached)
						{
							TryContact(Contact);
							if (bFoundRoute) return true;
							if (bSearchBudgetExceeded) return RejectBudget();
						}
						continue;
					}
					TArray<FContact>& Contacts=ContactCache.Add(Edges[K]);
					const FAxisAlignedBox3d PortalBounds(PortalA.ComponentMin(PortalB), PortalA.ComponentMax(PortalB));
					FDynamicMeshAABBTree3::FTreeTraversal Traversal;
					Traversal.NextBoxF = [&](const FAxisAlignedBox3d& Box, int)
					{
						if (bFoundRoute) return false;
						if (++BoxVisits > 4096) { bSearchBudgetExceeded = true; BudgetKind=TEXT("tree traversal"); }
						FVector Start,End;
						return !bSearchBudgetExceeded && !(Progress && Progress->Cancelled())
							&& Box.DistanceSquared(PortalBounds) <= ContactTolerance * ContactTolerance
							&& ClipPortal(PortalA,PortalB,FBox(Box.Min,Box.Max).ExpandBy(ContactTolerance),Start,End);
					};
					Traversal.NextTriangleF = [&](int32 Triangle)
					{
						if (bFoundRoute || bSearchBudgetExceeded || Triangle == Current.Triangle || Triangle == Neighbor) return;
						const FAxisAlignedBox3d FaceBounds=Mesh.GetTriBounds(Triangle);
						FVector Start,End;
						if (!ClipPortal(PortalA,PortalB,FBox(FaceBounds.Min,FaceBounds.Max).ExpandBy(ContactTolerance),Start,End)) return;
						if (++FaceQueries > 512) { bSearchBudgetExceeded = true; BudgetKind=TEXT("contact candidate"); return; }
						FTriangle3d Face; Mesh.GetTriVertices(Triangle, Face.V[0], Face.V[1], Face.V[2]);
						FContact Contact{Triangle,PortalA,FMath::ClosestPointOnTriangleToPoint(PortalA,Face.V[0],Face.V[1],Face.V[2])};
						if (FVector::DistSquared(PortalA,PortalB)>=1.e-18)
						{
							FDistSegment3Triangle3d Distance(FSegment3d(PortalA,PortalB),Face);
							if (Distance.GetSquared()>ContactTolerance*ContactTolerance) return;
							Contact.EdgePoint=Distance.SegmentClosest; Contact.FacePoint=Distance.TriangleClosest;
						}
						if (FVector::DistSquared(Contact.EdgePoint,Contact.FacePoint)>ContactTolerance*ContactTolerance) return;
						Contacts.Add(Contact); TryContact(Contact);
					};
					Tree.DoTraversal(Traversal);
					if (bFoundRoute) return true;
					if (bSearchBudgetExceeded) return RejectBudget();
				}
			}
			return Reject(FString::Printf(TEXT("No local route: states %d, faces %d, tree boxes %d, contacts %d, best endpoint route %.9f, limit %.9f, from-face error %.9f, to-face error %.9f"),
				Expanded.Num(),ExpandedFaces.Num(),BoxVisits,FaceQueries,BestEndLength==TNumericLimits<double>::Max()?-1.:BestEndLength,MaximumLength,FromFaceDistance,ToFaceDistance));
		};
		// First prove ordinary mesh adjacency without expanding unrelated contact
		// candidates. Only ambiguous seams need the more expensive bounded fallback.
		if (Search(false,64)) return true;
		if (Progress && Progress->Cancelled()) return false;
		if (Search(true,256)) return true;
		bBudgetExceeded = bSearchBudgetExceeded;
		return false;
	}

	bool WalkSurface(const TArray<FSourceMesh>& Sources, const FVector2D& HalfSize,
		FMD_StainStickerPatch& Patch, FString& Error, bool bLimitNeighborRise, FProgressCancel* Progress = nullptr,
		FString* OutDiagnostics = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_Walk);
		int32 InvalidSamples=0,UnprovenProjections=0,FailedInteriorRays=0,FailedInteriorRoutes=0,RejectedFrontTriangles=0;
		struct FFailureExample { int32 FromID,ToID; FVector From,To; FString Text; };
		TArray<FFailureExample,TInlineAllocator<3>> Failures;
		auto RememberFailure=[&](const TCHAR* Phase,int32 FromID,const FVector& From,int32 ToID,const FVector& To,double Step,const FString& Reason)
		{
			if (Failures.Num()>=3) return;
			for (const FFailureExample& Example:Failures)
				if (Example.FromID==FromID && Example.ToID==ToID && Example.From.Equals(From,1.e-6) && Example.To.Equals(To,1.e-6)) return;
			Failures.Add({FromID,ToID,From,To,FString::Printf(
				TEXT("%s: From=(%.9f,%.9f,%.9f) face=%d; To=(%.9f,%.9f,%.9f) face=%d; step=%.9f; %s"),
				Phase,From.X,From.Y,From.Z,FromID,To.X,To.Y,To.Z,ToID,Step,*Reason)});
		};
		ON_SCOPE_EXIT
		{
			if (OutDiagnostics)
			{
				*OutDiagnostics=FString::Printf(
					TEXT("Surface checks: %d invalid samples, %d unproven projected transitions, %d missing interior hits, %d unproven interior routes, %d rejected front triangles."),
					InvalidSamples,UnprovenProjections,FailedInteriorRays,FailedInteriorRoutes,RejectedFrontTriangles);
				for (const FFailureExample& Example:Failures) *OutDiagnostics+=TEXT("\n")+Example.Text;
			}
		};
		FDynamicMesh3 Mesh;
		FDynamicMeshEditor Editor(&Mesh);
		const FSourceMesh* Primary=nullptr; double SeedDistance=6.0;
		for (const auto& Source : Sources)
		{
			FDynamicMeshAABBTree3 SourceTree(Source.Mesh.Get());
			double T; FVector3d Bary; int32 ID=INDEX_NONE;
			if (SourceTree.FindNearestHitTriangle(FWatertightRay3d(FVector3d(0,0,6),FVector3d(0,0,-1)),T,ID,Bary)
				&& T<=12 && FMath::Abs(T-6)<SeedDistance)
			{ Primary=&Source; SeedDistance=FMath::Abs(T-6); }
		}
		TMap<FIntVector, int32> ClippedVertices;
		auto AddClippedVertex = [&](const FVector& P)
		{
			const FIntVector Key(FMath::RoundToInt(P.X * 1000), FMath::RoundToInt(P.Y * 1000), FMath::RoundToInt(P.Z * 1000));
			if (const int32* Existing = ClippedVertices.Find(Key)) return *Existing;
			const int32 Vertex = Mesh.AppendVertex(P); ClippedVertices.Add(Key, Vertex); return Vertex;
		};
		for (const auto& Source : Sources)
		{
			if (!bLimitNeighborRise || &Source==Primary)
			{ FMeshIndexMappings Mapping; Editor.AppendMesh(Source.Mesh.Get(),Mapping); continue; }
			ClippedVertices.Reset(); // Keep touching authored parts separate; prove their contact locally.
			// A floor spill may cover adjacent low steps and chair feet, but must not climb
			// an unrelated piece of furniture all the way to its seat. Direct furniture hits
			// keep their full size-dependent wrapping; downward continuation is unchanged.
			for (int32 Triangle : Source.Mesh->TriangleIndicesItr())
			{
				FVector A,B,C; Source.Mesh->GetTriVertices(Triangle,A,B,C);
				const TArray<FVector> Input={A,B,C}; TArray<FVector> Clipped;
				for (int32 I=0;I<3;++I)
				{
					const FVector P=Input[I],Q=Input[(I+1)%3];
					const double DP=P.Z-6.0,DQ=Q.Z-6.0;
					if (DP<=0) Clipped.Add(P);
					if ((DP<=0)!=(DQ<=0)) Clipped.Add(FMath::Lerp(P,Q,DP/(DP-DQ)));
				}
				for (int32 I=1;I+1<Clipped.Num();++I)
				{
					if (FVector::CrossProduct(Clipped[I]-Clipped[0],Clipped[I+1]-Clipped[0]).SquaredLength()<1.e-12) continue;
					const int32 A0 = AddClippedVertex(Clipped[0]), A1 = AddClippedVertex(Clipped[I]), A2 = AddClippedVertex(Clipped[I+1]);
					if (A0 != A1 && A1 != A2 && A2 != A0) Mesh.AppendTriangle(A0, A1, A2);
				}
			}
		}
		FDynamicMeshAABBTree3 Tree(&Mesh);
		auto Trace = [&](FVector A,FVector B,FVector& P,FVector& N,int32* OutTriangle = nullptr)
		{
			if (OutTriangle) *OutTriangle = INDEX_NONE;
			const FVector D=B-A; const double Length=D.Length(); if (Length<1.e-6) return false;
			const FWatertightRay3d Ray(A,D/Length);
			double T; FVector3d Bary;
			int32 ID=INDEX_NONE;
			if (!Tree.FindNearestHitTriangle(Ray,T,ID,Bary,IMeshSpatial::FQueryOptions(Length,[&](int32 Triangle) { return FVector::DotProduct(Mesh.GetTriNormal(Triangle),D)<-1.e-6; })) || ID<0 || T>Length) return false;
			P=A+D*(T/Length); N=Mesh.GetTriNormal(ID);
			if (OutTriangle) *OutTriangle = ID;
			return FVector::DotProduct(N,D)<-1.e-6;
		};
		FVector Seed,SeedNormal; int32 SeedTriangle = INDEX_NONE;
		if (!Trace(FVector(0,0,6),FVector(0,0,-6),Seed,SeedNormal,&SeedTriangle))
		{ Error=TEXT("No readable surface at the cursor."); return false; }
		TSet<int32> UsedReceiverFaces; UsedReceiverFaces.Add(SeedTriangle);
		bool bRouteBudgetExceeded = false;
		FString RouteBudgetDetail;
		auto ProveRoute = [&](int32 FromTriangle, const FVector& From, int32 ToTriangle, const FVector& To, double Step, FString* Failure = nullptr)
		{
			FString Detail;
			const bool bProven = ProveLocalRoute(Mesh,Tree,FromTriangle,From,ToTriangle,To,Step,
				UsedReceiverFaces,bRouteBudgetExceeded,Progress,&Detail);
			if (Failure) *Failure=Detail;
			if (bRouteBudgetExceeded) RouteBudgetDetail=Detail;
			return bProven;
		};
		auto SetRouteBudgetError = [&]
		{
			Error=TEXT("This surface exceeds the bounded local continuity check. ")+RouteBudgetDetail
				+TEXT(" Reduce the stain width or simplify the receiving mesh.");
		};
		auto Walk = [&](FVector2D Target,FVector& Result,FVector& ThicknessNormal,int32& ResultTriangle,double StepLimit)
		{
			ResultTriangle = INDEX_NONE;
			FVector P=Seed,N=SeedNormal;
			int32 CurrentTriangle = SeedTriangle;
			const FVector PlanarDirection=FVector(Target.X,Target.Y,0).GetSafeNormal();
			const double Radius=Target.Length();
			const double Reach=FMath::Clamp(HalfSize.GetMax()*.5,6.0,40.0);
			double Projected=0;
			// Keep an inset screen and its raised front bezel in one height field. Unfold only
			// beyond the outer lip; traversing every concave crease creates radial folds.
			while (Projected<Radius-.001)
			{
				if (Progress && Progress->Cancelled()) return false;
				const double NextDistance=FMath::Min(Radius,Projected+.75);
				const FVector Center=PlanarDirection*NextDistance;
				FVector Next,NextNormal; int32 NextTriangle;
				if (!Trace(Center+FVector::UpVector*Reach,Center-FVector::UpVector*Reach,Next,NextNormal,&NextTriangle)) break;
				const double ProjectionStep = NextDistance - Projected;
				FString Failure;
				if (FVector::Distance(P,Next)>ProjectionStep+.05
					&& !ProveRoute(CurrentTriangle,P,NextTriangle,Next,ProjectionStep,&Failure))
				{
					++UnprovenProjections; RememberFailure(TEXT("Projection"),CurrentTriangle,P,NextTriangle,Next,ProjectionStep,Failure);
					return false; // A rear-layer hit is not an outer lip to wrap around.
				}
				P=Next; N=NextNormal; CurrentTriangle=NextTriangle; UsedReceiverFaces.Add(CurrentTriangle); Projected=NextDistance;
			}
			if (Projected>=Radius-.001) { Result=P; ThicknessNormal=FVector::UpVector; ResultTriangle=CurrentTriangle; return true; }
			FVector D=FQuat::FindBetweenNormals(FVector::UpVector,N).RotateVector(PlanarDirection);
			double Remaining=Radius-Projected; const double WrappingDistance=Remaining; int32 Steps=0;
			while (Remaining>0.01)
			{
				if (++Steps>8192 || (Progress && Progress->Cancelled())) return false;
				const double Step=FMath::Min(Remaining,StepLimit);
				const FVector Predicted=P+D*Step;
				FVector Next,NextNormal; int32 NextTriangle = INDEX_NONE;
				// An inset screen meets the inner bezel before a normal projection can see its top.
				bool Found=Trace(P+N*.03,Predicted+N*.03,Next,NextNormal,&NextTriangle)
					&& FVector::Distance(P,Next)>.02 && FVector::DotProduct(N,NextNormal)>-.95;
				if (!Found) Found=Trace(Predicted+N*2.0,Predicted-N*2.0,Next,NextNormal,&NextTriangle)
					&& FVector::Distance(P,Next)<3.0 && FVector::DotProduct(N,NextNormal)>-.95;
				if (!Found || FVector::DotProduct(Next-P,N)<-Step*.5)
				{
					// Cross a convex lip by looking back toward its side; no mesh-edge adjacency required.
					FVector Side,SideNormal; int32 SideTriangle;
					if (Trace(P+D*Step+N*.02,P-D*.15-N*Step,Side,SideNormal,&SideTriangle)
						&& FVector::Distance(P,Side)<2.0 && FVector::DotProduct(N,SideNormal)>-.95)
						{ Next=Side; NextNormal=SideNormal; NextTriangle=SideTriangle; Found=true; }
				}
				if (!Found) { Result=P; return false; }
				const double Travel=FVector::Distance(P,Next);
				if (Travel<0.0001) return false;
				FString Failure;
				if (Travel>Step+.05 && !ProveRoute(CurrentTriangle,P,NextTriangle,Next,Step,&Failure))
				{
					++UnprovenProjections; RememberFailure(TEXT("Wrapping"),CurrentTriangle,P,NextTriangle,Next,Step,Failure);
					return false;
				}
				if (Travel>Remaining) { Next=FMath::Lerp(P,Next,Remaining/Travel); Remaining=0; }
				else Remaining-=Travel;
				D=FQuat::FindBetweenNormals(N,NextNormal).RotateVector(D).GetSafeNormal(); P=Next; N=NextNormal;
				CurrentTriangle=NextTriangle; UsedReceiverFaces.Add(CurrentTriangle);
			}
			Result=P; ResultTriangle=CurrentTriangle;
			ThicknessNormal=FMath::Lerp(FVector::UpVector,N,FMath::Clamp(WrappingDistance/3.0,0.0,1.0)).GetSafeNormal(); return true;
		};
		constexpr int32 Resolution=33;
		TArray<FVector> Points,ThicknessNormals; Points.SetNum(Resolution*Resolution); ThicknessNormals.SetNum(Points.Num());
		TArray<bool> Valid; Valid.SetNumZeroed(Points.Num());
		TArray<int32> PointTriangles; PointTriangles.Init(INDEX_NONE,Points.Num());
		for (int32 Y=0;Y<Resolution;++Y) for (int32 X=0;X<Resolution;++X)
		{
			if (Progress && Progress->Cancelled()) return false;
			const int32 I=Y*Resolution+X;
			const FVector2D Target=(FVector2D(X,Y)/(Resolution-1)-FVector2D(.5,.5))*(HalfSize*2);
			Valid[I]=Walk(Target,Points[I],ThicknessNormals[I],PointTriangles[I],0.25);
			if (!Valid[I] && !bRouteBudgetExceeded) Valid[I]=Walk(Target,Points[I],ThicknessNormals[I],PointTriangles[I],0.10);
			if (!Valid[I] && !bRouteBudgetExceeded) Valid[I]=Walk(Target,Points[I],ThicknessNormals[I],PointTriangles[I],0.05);
			if (!Valid[I]) ++InvalidSamples;
			if (bRouteBudgetExceeded) { SetRouteBudgetError(); return false; }
		}
		const TArray<FVector> SupportPoints = Points;
		TArray<bool> OnOriginalFace; OnOriginalFace.Init(false,Points.Num());
		for (int32 I=0;I<Points.Num();++I) if (Valid[I] && Mesh.IsTriangle(PointTriangles[I]))
		{
			FVector A,B,C; Mesh.GetTriVertices(PointTriangles[I],A,B,C);
			OnOriginalFace[I]=FVector::DistSquared(SupportPoints[I],FMath::ClosestPointOnTriangleToPoint(SupportPoints[I],A,B,C))<=1.e-10;
		}
		// A bounded fluid fillet rises before a sharp bezel instead of pinching into its crease.
		// Only the front height field changes; the flat interior and side-wrapping coordinates stay fixed.
		TArray<FVector> Rounded=Points;
		for (int32 Y=0;Y<Resolution;++Y) for (int32 X=0;X<Resolution;++X)
		{
			const int32 I=Y*Resolution+X;
			if (!Valid[I] || !ThicknessNormals[I].Equals(FVector::UpVector,.001)) continue;
			for (int32 DY=-3;DY<=3;++DY) for (int32 DX=-3;DX<=3;++DX)
			{
				const int32 NX=X+DX,NY=Y+DY;
				if (NX<0 || NX>=Resolution || NY<0 || NY>=Resolution) continue;
				const int32 J=NY*Resolution+NX;
				if (!Valid[J] || !ThicknessNormals[J].Equals(FVector::UpVector,.001)) continue;
				const double Distance=FVector2D(Points[I].X-Points[J].X,Points[I].Y-Points[J].Y).Length();
				if (Distance>3.0) continue;
				Rounded[I].Z=FMath::Max(Rounded[I].Z,Points[J].Z-Distance);
			}
		}
		Points=MoveTemp(Rounded);
		TArray<bool> Front; Front.SetNum(Points.Num());
		for (int32 I=0;I<Points.Num();++I) Front[I]=ThicknessNormals[I].Equals(FVector::UpVector,.001);
		// Relax side paths across small vents and tessellation changes. A viscous coating bridges
		// these recesses; transporting every authored face normal would create sharp spikes.
		for (int32 Pass=0;Pass<10;++Pass)
		{
			TArray<FVector> NextPoints=Points,NextNormals=ThicknessNormals;
			for (int32 Y=0;Y<Resolution;++Y) for (int32 X=0;X<Resolution;++X)
			{
				const int32 I=Y*Resolution+X; if (!Valid[I] || Front[I]) continue;
				FVector Sum=FVector::ZeroVector,NormalSum=ThicknessNormals[I]*2; int32 Count=0;
				const FIntPoint Deltas[]={FIntPoint(-1,0),FIntPoint(1,0),FIntPoint(0,-1),FIntPoint(0,1)};
				for (FIntPoint Delta:Deltas)
				{
					const int32 NX=X+Delta.X,NY=Y+Delta.Y;
					if (NX<0 || NX>=Resolution || NY<0 || NY>=Resolution) continue;
					const int32 J=NY*Resolution+NX;
					if (!Valid[J] || FVector::DistSquared(Points[I],Points[J])>64.0) continue;
					Sum+=Points[J]; NormalSum+=ThicknessNormals[J]; ++Count;
				}
				if (Count<2) continue;
				const FVector Normal=NormalSum.GetSafeNormal(); if (Normal.IsNearlyZero()) continue;
				FVector Point=FMath::Lerp(Points[I],Sum/Count,.35),Contact,ContactNormal;
				if (Trace(Point+Normal*3,Point-Normal*3,Contact,ContactNormal) && FVector::DotProduct(Contact-Point,Normal)>0)
					Point=Contact+Normal*.03;
				NextPoints[I]=Point; NextNormals[I]=Normal;
			}
			Points=MoveTemp(NextPoints); ThicknessNormals=MoveTemp(NextNormals);
		}
		auto SupportedInterior = [&](const FVector& Expected, const int32* CellCorners)
		{
			FVector Contact, Normal; int32 Triangle;
			if (!Trace(Expected+FVector::UpVector*3,Expected-FVector::UpVector*3,Contact,Normal,&Triangle))
			{ ++FailedInteriorRays; return false; }
			// A different original corner can supply the actual short route at a crease.
			// Try exact-face corners first, then nearest corners, without relaxing any proof.
			int32 Choices[4]; for (int32 K=0;K<4;++K) Choices[K]=CellCorners[K];
			auto Priority=[&](int32 Choice)
			{
				return !Valid[Choice]?3:(OnOriginalFace[Choice]?(PointTriangles[Choice]==Triangle?0:1):2);
			};
			for (int32 A=0;A<4;++A) for (int32 B=A+1;B<4;++B)
				if (Priority(Choices[B])<Priority(Choices[A]) || (Priority(Choices[B])==Priority(Choices[A])
					&& FVector::DistSquared(SupportPoints[Choices[B]],Contact)<FVector::DistSquared(SupportPoints[Choices[A]],Contact))) Swap(Choices[A],Choices[B]);
			FString FirstFailure; double FirstStep=0.; int32 FirstAnchor=Choices[0];
			for (int32 Anchor:Choices) if (Valid[Anchor])
			{
				const FVector Offset=Expected-SupportPoints[Anchor];
				const double Step=FVector2D(Offset.X,Offset.Y).Length(); FString Failure;
				if (ProveRoute(PointTriangles[Anchor],SupportPoints[Anchor],Triangle,Contact,Step,&Failure))
				{ UsedReceiverFaces.Add(Triangle); return true; }
				if (FirstFailure.IsEmpty()) { FirstFailure=Failure; FirstStep=Step; FirstAnchor=Anchor; }
				if (bRouteBudgetExceeded || (Progress && Progress->Cancelled())) break;
			}
			++FailedInteriorRoutes; RememberFailure(TEXT("Interior"),PointTriangles[FirstAnchor],SupportPoints[FirstAnchor],Triangle,Contact,FirstStep,FirstFailure);
			return false;
		};
		TMap<int32, bool> DiagonalSupport;
		for (int32 Y=0;Y<Resolution-1;++Y) for (int32 X=0;X<Resolution-1;++X)
		{
			if (Progress && Progress->Cancelled()) return false;
			const int32 A=Y*Resolution+X,B=A+1,C=A+Resolution,D=C+1;
			const int32 CellCorners[]={A,B,C,D};
			const int32 Indices[]={A,D,B,A,C,D};
			for (int32 I=0;I<6;I+=3)
			{
				if (!Valid[Indices[I]] || !Valid[Indices[I+1]] || !Valid[Indices[I+2]]) continue;
				const int32 T0=Indices[I],T1=Indices[I+1],T2=Indices[I+2];
				// Three points on one convex authored face cannot enclose a receiver aperture.
				// Across faces, verify the shared diagonal and the interior before emitting it.
				if (Front[T0] && Front[T1] && Front[T2]
					&& (PointTriangles[T0]!=PointTriangles[T1] || PointTriangles[T0]!=PointTriangles[T2]
						|| !OnOriginalFace[T0] || !OnOriginalFace[T1] || !OnOriginalFace[T2]
						|| !Points[T0].Equals(SupportPoints[T0],1.e-6) || !Points[T1].Equals(SupportPoints[T1],1.e-6)
						|| !Points[T2].Equals(SupportPoints[T2],1.e-6)))
				{
					bool* Diagonal = DiagonalSupport.Find(A);
					if (!Diagonal)
						Diagonal = &DiagonalSupport.Add(A,SupportedInterior((SupportPoints[A]+SupportPoints[D])*.5,CellCorners));
					const bool bCenter = *Diagonal && SupportedInterior((SupportPoints[T0]+SupportPoints[T1]+SupportPoints[T2])/3,CellCorners);
					if (bRouteBudgetExceeded) { SetRouteBudgetError(); return false; }
					if (!bCenter) { ++RejectedFrontTriangles; continue; }
				}
				if (FVector::CrossProduct(Points[Indices[I+1]]-Points[Indices[I]],Points[Indices[I+2]]-Points[Indices[I]]).SquaredLength()<1.e-8) continue;
				for (int32 K=0;K<3;++K)
				{
					const int32 ID=Indices[I+K]; Patch.Positions.Add(FVector3f(Points[ID])); Patch.FitNormals.Add(FVector3f(ThicknessNormals[ID]));
					Patch.UVs.Add(FVector2f(float(ID%Resolution)/(Resolution-1),float(ID/Resolution)/(Resolution-1)));
					Patch.LocalBounds+=Points[ID];
				}
			}
		}
		if (!Patch.LocalBounds.IsValid) { Error=TEXT("This surface could not support a connected slime patch."); return false; }
		// Coating queries reach at most 5 cm beyond coverage. Preserve whole source faces,
		// but follow only real adjacency through portals inside that local neighborhood.
		const FBox ReceiverBounds=Patch.LocalBounds.ExpandBy(5.0);
		const FAxisAlignedBox3d ReceiverBox(ReceiverBounds.Min,ReceiverBounds.Max);
		TSet<int32> ReceiverFaces;
		TArray<int32> ReceiverQueue;
		auto IncludeReceiver = [&](int32 Triangle)
		{
			if (Triangle<0 || ReceiverFaces.Contains(Triangle) || Mesh.GetTriBounds(Triangle).DistanceSquared(ReceiverBox)>0) return true;
			if (ReceiverFaces.Num()>=FMD_StainStickerPatch::MaximumTriangles)
			{
				Error=TEXT("This placement needs over 4,096 local receiver triangles. Reduce the stain width or simplify the receiving mesh.");
				return false;
			}
			ReceiverFaces.Add(Triangle); ReceiverQueue.Add(Triangle); return true;
		};
		TArray<int32> Seeds=UsedReceiverFaces.Array(); Seeds.Sort();
		for (int32 Triangle : Seeds) if (!IncludeReceiver(Triangle)) return false;
		for (int32 Cursor=0;Cursor<ReceiverQueue.Num();++Cursor)
		{
			if (Progress && Progress->Cancelled()) return false;
			const int32 Triangle=ReceiverQueue[Cursor]; const FIndex3i Edges=Mesh.GetTriEdges(Triangle);
			for (int32 K=0;K<3;++K)
			{
				FVector3d EdgeA,EdgeB; Mesh.GetEdgeV(Edges[K],EdgeA,EdgeB);
				FVector PortalA,PortalB; if (!ClipPortal(EdgeA,EdgeB,ReceiverBounds,PortalA,PortalB)) continue;
				const FIndex2i Pair=Mesh.GetEdgeT(Edges[K]);
				if (!IncludeReceiver(Pair.A==Triangle?Pair.B:Pair.A)) return false;
			}
		}
		ReceiverQueue.Sort();
		TMap<int32,int32> ReceiverIDs;
		for (int32 Triangle : ReceiverQueue)
		{
			const FIndex3i V=Mesh.GetTriangle(Triangle); FIntVector Output;
			for (int32 K=0;K<3;++K)
			{
				int32* ID=ReceiverIDs.Find(V[K]);
				if (!ID) ID=&ReceiverIDs.Add(V[K],Patch.FitReceiverVertices.Add(Mesh.GetVertex(V[K])));
				Output[K]=*ID;
			}
			Patch.FitReceiverTriangles.Add(Output);
		}
		if (!Patch.IsValid()) { Error=TEXT("This surface could not support a connected slime patch."); return false; }
		return true;
	}

	UStaticMesh* MakeMesh(AActor& Owner, const FMD_StainStickerPatch& Patch, UMaterialInterface* Material)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description); Attributes.Register();
		auto Positions = Attributes.GetVertexPositions();
		auto UVs = Attributes.GetVertexInstanceUVs(); UVs.SetNumChannels(1);
		auto Normals = Attributes.GetVertexInstanceNormals();
		auto Tangents = Attributes.GetVertexInstanceTangents();
		auto Signs = Attributes.GetVertexInstanceBinormalSigns();
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		for (int32 Index = 0; Index < Patch.Positions.Num(); Index += 3)
		{
			const FVector A(Patch.Positions[Index]), B(Patch.Positions[Index + 1]), C(Patch.Positions[Index + 2]);
			const FVector Normal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
			const FVector Tangent = (B - A).GetSafeNormal();
			TArray<FVertexInstanceID> Instances;
			// Keep UE/GeometryCore clockwise-front winding and authored outward normals.
			for (int32 K : {0, 1, 2})
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = Patch.Positions[Index + K] + FVector3f(Normal * 0.02);
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				UVs.Set(Instance, 0, Patch.UVs[Index + K]); Normals[Instance] = FVector3f(Normal);
				Tangents[Instance] = FVector3f(Tangent); Signs[Instance] = 1;
				Instances.Add(Instance);
			}
			Description.CreateTriangle(Group, Instances);
		}
		UStaticMesh* Mesh = NewObject<UStaticMesh>(&Owner, NAME_None, RF_Transactional | RF_Public);
		// The sticker is visual only; query hits use the stain actor's separate surface box.
		Mesh->CreateBodySetup();
		Mesh->GetBodySetup()->bNeverNeedsCookedCollisionData = true;
		Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Mesh->SetNumSourceModels(1);
		auto& Settings = Mesh->GetSourceModel(0).BuildSettings;
		Settings.bGenerateLightmapUVs = false; Settings.bRecomputeNormals = false; Settings.bRecomputeTangents = false;
		Settings.bUseFullPrecisionUVs = true; Settings.bRemoveDegenerates = false;
		Mesh->GetNaniteSettings().bEnabled = false;
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Material));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true; Params.bBuildSimpleCollision = false; Params.bAllowCpuAccess = false;
		Params.bCommitMeshDescription = true; Params.bMarkPackageDirty = false;
		const TArray<const FMeshDescription*> Descriptions { &Description };
		return Mesh->BuildFromMeshDescriptions(Descriptions, Params) ? Mesh : nullptr;
	}
}

bool FMD_StainStickerBaker::Build(AActor& Owner, const FVector2D& HalfSize, int32 InitialSize,
	UMaterialInterface* Material, FMD_StainStickerPatch& OutPatch, UStaticMesh*& OutMesh, FString& Error, bool bSurfaceWalk)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_BakeEditor);
	if (!Owner.GetWorld() || Owner.GetWorld()->IsGameWorld() || !Material || HalfSize.ContainsNaN() || HalfSize.GetMin() <= 0)
	{ Error = TEXT("A sticker material and an editor-world surface are required."); return false; }
	TArray<MD_StainStickerBake::FGeometryInstance> Instances;
	if (!MD_StainStickerBake::CaptureSources(Owner, HalfSize.Length() + 1, Instances, Error, nullptr, nullptr, bSurfaceWalk)) return false;
	TArray<MD_StainStickerBake::FSourceMesh> Sources;
	if (!MD_StainStickerBake::BuildSources(Instances, HalfSize.Length() + 1, Sources, Error)) return false;
	FDynamicMesh3 Surface;
	TArray<int32> Disconnected;
	FMD_StainStickerPatch Patch; Patch.InitialSize = InitialSize;
	if (bSurfaceWalk)
	{
		if (!MD_StainStickerBake::WalkSurface(Sources, HalfSize, Patch, Error, Owner.GetActorUpVector().Z>.75)) return false;
		OutPatch=MoveTemp(Patch); OutMesh=nullptr; return true;
	}
	else if (!MD_StainStickerBake::MakeSurface(Sources, HalfSize, Surface, Disconnected, Error)
		|| !MD_StainStickerBake::AddDownhillBridges(Surface, Sources, Disconnected, HalfSize, Error)
		|| !MD_StainStickerBake::Unfold(Surface, HalfSize, Patch, Error)) return false;
	UStaticMesh* Mesh = MD_StainStickerBake::MakeMesh(Owner, Patch, Material);
	if (!Mesh) { Error = TEXT("Could not build the mesh decal."); return false; }
	OutPatch = MoveTemp(Patch); OutMesh = Mesh;
	return true;
}

struct FMD_StainStickerAsyncResult
{
	std::atomic<bool> bReady {false}, bCancelled {false}, bPreviewReady {false};
	bool bSuccess = false;
	FMD_StainStickerPatch Patch;
	TSharedPtr<FMD_StainSurfaceFitJob,ESPMode::ThreadSafe> FitJob;
	FString Error;
	TArray<FMD_StainSourceSnapshot> Sources;
};

FMD_StainStickerAsyncBake::~FMD_StainStickerAsyncBake() { Cancel(); }
bool FMD_StainStickerAsyncBake::IsReady() const { return Result->bReady.load(); }
bool FMD_StainStickerAsyncBake::IsPreviewReady() const { return Result->bPreviewReady.load(); }
UStaticMesh* FMD_StainStickerAsyncBake::FinishPreview(AActor& Owner,UMaterialInterface* Material)
{
	check(IsInGameThread());
	if (!IsPreviewReady() || IsCancelled() || !SourcesAreCurrent()) return nullptr;
	return FMD_StainSurfaceFit::CreatePreview(Owner,Material,*Result->FitJob);
}
bool FMD_StainStickerAsyncBake::IsCancelled() const { return Result->bCancelled.load(); }
void FMD_StainStickerAsyncBake::Cancel() { if (Result) Result->bCancelled.store(true); }
bool FMD_StainStickerAsyncBake::SourcesAreCurrent() const
{
	for (const auto& Source : Result->Sources) if (!Source.IsCurrent()) return false;
	return true;
}
UMD_SurfaceStainDefinition* FMD_StainStickerAsyncBake::FinishVolume(AActor& Owner,const UMD_SurfaceStainDefinition& Definition,FString& Error)
{
	check(IsInGameThread());
	if (!IsReady() || IsCancelled()) return nullptr;
	if (!Result->bSuccess) { Error=Result->Error; return nullptr; }
	if (!SourcesAreCurrent()) { Error=TEXT("A receiving surface changed. Place this stain again."); return nullptr; }
	return Result->FitJob ? FMD_StainSurfaceFit::Create(Owner,Definition,*Result->FitJob,Error) : nullptr;
}

bool FMD_StainStickerAsyncBake::Finish(AActor& Owner, UMaterialInterface* Material,
	FMD_StainStickerPatch& OutPatch, UStaticMesh*& OutMesh, FString& Error)
{
	check(IsInGameThread());
	if (!IsReady() || Result->bCancelled.load()) return false;
	if (!Result->bSuccess) { Error = Result->Error; return false; }
	for (const auto& Source : Result->Sources)
		if (!Source.IsCurrent()) { Error = TEXT("A nearby surface changed while preparing this sticker. Place it again."); return false; }
	UStaticMesh* Mesh = MD_StainStickerBake::MakeMesh(Owner, Result->Patch, Material);
	if (!Mesh) { Error = TEXT("Could not build the prepared mesh decal."); return false; }
	OutPatch = MoveTemp(Result->Patch); OutMesh = Mesh;
	return true;
}

UStaticMesh* FMD_StainStickerBaker::CreateMesh(AActor& Owner, const FMD_StainStickerPatch& Patch, UMaterialInterface* Material)
{
	check(IsInGameThread());
	return Patch.IsValid() && Material ? MD_StainStickerBake::MakeMesh(Owner, Patch, Material) : nullptr;
}

TSharedPtr<FMD_StainStickerSourceCache> FMD_StainStickerBaker::CreateSourceCache()
{
	return MakeShared<FMD_StainStickerSourceCache>();
}

TSharedPtr<FMD_StainStickerAsyncBake> FMD_StainStickerBaker::StartAsync(AActor& Owner, const FVector2D& HalfSize, int32 InitialSize,
	const TSharedPtr<FMD_StainStickerSourceCache>& Cache, const UMD_SurfaceStainDefinition* FitDefinition, bool bPreparePreview)
{
	check(IsInGameThread());
	auto Task = MakeShared<FMD_StainStickerAsyncBake>();
	Task->Result = MakeShared<FMD_StainStickerAsyncResult, ESPMode::ThreadSafe>();
	const auto Result = Task->Result;
	const bool bSurfaceWalk=FitDefinition!=nullptr;
	const bool bLimitNeighborRise=Owner.GetActorUpVector().Z>.75;
	if (FitDefinition) Result->FitJob=FMD_StainSurfaceFit::Capture(*FitDefinition);
	TArray<MD_StainStickerBake::FGeometryInstance> Instances;
	if (!Owner.GetWorld() || Owner.GetWorld()->IsGameWorld() || HalfSize.ContainsNaN() || HalfSize.GetMin() <= 0
		|| !MD_StainStickerBake::CaptureSources(Owner, HalfSize.Length() + 1, Instances, Result->Error, &Result->Sources, Cache.Get(), bSurfaceWalk))
	{
		if (Result->Error.IsEmpty()) Result->Error = TEXT("No readable source surface around this placement.");
		Result->bReady.store(true);
		return Task;
	}
	// Only private geometry copies cross threads. UObject, collision, mesh upload and transactions stay on the editor thread.
	UE::Tasks::Launch(TEXT("MD Stain Sticker"), [Result, Instances = MoveTemp(Instances), HalfSize, InitialSize, bSurfaceWalk, bLimitNeighborRise, bPreparePreview]() mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_WorkerBake);
		FProgressCancel Progress;
		Progress.CancelF = [Result]() { return Result->bCancelled.load(); };
		FDynamicMesh3 Surface;
		TArray<int32> Disconnected;
		TArray<MD_StainStickerBake::FSourceMesh> Sources;
		FString WalkDiagnostics;
		Result->Patch.InitialSize = InitialSize;
		Result->bSuccess = !Progress.Cancelled()
			&& MD_StainStickerBake::BuildSources(Instances, HalfSize.Length() + 1, Sources, Result->Error, &Progress)
			&& (bSurfaceWalk ? MD_StainStickerBake::WalkSurface(Sources, HalfSize, Result->Patch, Result->Error, bLimitNeighborRise, &Progress, &WalkDiagnostics)
				: (MD_StainStickerBake::MakeSurface(Sources, HalfSize, Surface, Disconnected, Result->Error, &Progress)
				&& MD_StainStickerBake::AddDownhillBridges(Surface, Sources, Disconnected, HalfSize, Result->Error, &Progress)
				&& !Progress.Cancelled()
				&& MD_StainStickerBake::Unfold(Surface, HalfSize, Result->Patch, Result->Error)));
		if (Result->bSuccess && Result->FitJob && bSurfaceWalk && bPreparePreview)
		{
			// Publish an immutable estimate before doing the expensive three final removal stages.
			// Release/acquire through the atomic flag keeps UObject creation on the editor thread.
			if (FMD_StainSurfaceFit::PreparePreview(*Result->FitJob,Result->Patch,InitialSize,&Progress))
				Result->bPreviewReady.store(true);
		}
		if (Result->bSuccess && bSurfaceWalk)
			Result->bSuccess=Result->FitJob.IsValid() && FMD_StainSurfaceFit::Prepare(*Result->FitJob,Result->Patch,InitialSize,Result->Error,&Progress);
		if (!Result->bSuccess && Result->Error.IsEmpty()) Result->Error = TEXT("Could not unfold this connected surface.");
		if (!Result->bSuccess && !Progress.Cancelled() && !WalkDiagnostics.IsEmpty())
			UE_LOG(LogTemp,Verbose,TEXT("Stain fitting failed: %s\n%s"),*Result->Error,*WalkDiagnostics);
		Instances.Reset(); Sources.Reset(); Surface = FDynamicMesh3();
		Result->bReady.store(true);
	}, UE::Tasks::ETaskPriority::BackgroundLow);
	return Task;
}
#endif
