#include "Gameplay/Contamination/MD_StainStickerBaker.h"

#if WITH_EDITOR
#include "MD_StainDownhillBridge.h"
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DynamicMeshEditor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
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
	bool IsCurrent() const
	{
		return Component.IsValid() && Component->IsRegistered() && Component->IsVisible()
			&& Component->bReceivesDecals && Component->IsCollisionEnabled()
			&& Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
			&& Component->GetStaticMesh() == Mesh.Get() && Mesh.IsValid() && Mesh->GetLightingGuid() == GeometryGuid
			&& Component->GetComponentTransform().Equals(Transform, 0.01);
	}
};

struct FMD_StainStickerSourceCache
{
	struct FTriangle { FVector3f Positions[3]; FVector3f Normal; };
	struct FGeometry { TArray<FTriangle> Triangles; bool bClosedSource = false; };
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

	bool CaptureSources(AActor& Owner, double Radius, TArray<FGeometryInstance>& Instances, FString& Error, TArray<FMD_StainSourceSnapshot>* Snapshots = nullptr, FMD_StainStickerSourceCache* Cache = nullptr)
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
			if (!Component || Cast<UInstancedStaticMeshComponent>(Component) || !Component->IsVisible() || !Component->bReceivesDecals || !Component->GetStaticMesh()
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
			TSharedPtr<FSourceGeometry, ESPMode::ThreadSafe> Geometry = GeometryByMesh.FindRef(Source);
			if (!Geometry)
			{
				const FGuid Guid = Source->GetLightingGuid();
				if (Cache)
					if (const auto* Entry = Cache->Entries.Find(Source); Entry && Entry->Guid == Guid)
						Geometry = Entry->Geometry;
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
					if (Cache)
					{
						// This tool owns a bounded cache of immutable coordinates, never source UObjects.
						if (const auto* Previous = Cache->Entries.Find(Source)) Cache->TriangleCount -= Previous->Geometry->Triangles.Num();
						if (Cache->TriangleCount + Geometry->Triangles.Num() > MaximumSnapshotTriangles)
						{ Cache->Entries.Reset(); Cache->TriangleCount = 0; }
						Cache->Entries.Add(Source, {Guid, Geometry});
						Cache->TriangleCount += Geometry->Triangles.Num();
					}
				}
				SnapshotTriangles += Geometry->Triangles.Num();
				if (SnapshotTriangles > MaximumSnapshotTriangles)
				{ Error = TEXT("Nearby source meshes exceed the 250,000-triangle snapshot budget. Reduce the sticker size."); return false; }
				GeometryByMesh.Add(Source, Geometry);
			}
			const FTransform Transform = Component->GetComponentTransform();
			if (Snapshots) Snapshots->Add({Component, Source, Transform, Source->GetLightingGuid()});
			const FVector BuildScale = Source->GetNumSourceModels() > 0 ? Source->GetSourceModel(0).BuildSettings.BuildScale3D : FVector::OneVector;
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
	UMaterialInterface* Material, FMD_StainStickerPatch& OutPatch, UStaticMesh*& OutMesh, FString& Error)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_BakeEditor);
	if (!Owner.GetWorld() || Owner.GetWorld()->IsGameWorld() || !Material || HalfSize.ContainsNaN() || HalfSize.GetMin() <= 0)
	{ Error = TEXT("A sticker material and an editor-world surface are required."); return false; }
	TArray<MD_StainStickerBake::FGeometryInstance> Instances;
	if (!MD_StainStickerBake::CaptureSources(Owner, HalfSize.Length() + 1, Instances, Error)) return false;
	TArray<MD_StainStickerBake::FSourceMesh> Sources;
	if (!MD_StainStickerBake::BuildSources(Instances, HalfSize.Length() + 1, Sources, Error)) return false;
	FDynamicMesh3 Surface;
	TArray<int32> Disconnected;
	if (!MD_StainStickerBake::MakeSurface(Sources, HalfSize, Surface, Disconnected, Error)
		|| !MD_StainStickerBake::AddDownhillBridges(Surface, Sources, Disconnected, HalfSize, Error)) return false;
	FMD_StainStickerPatch Patch; Patch.InitialSize = InitialSize;
	if (!MD_StainStickerBake::Unfold(Surface, HalfSize, Patch, Error)) return false;
	UStaticMesh* Mesh = MD_StainStickerBake::MakeMesh(Owner, Patch, Material);
	if (!Mesh) { Error = TEXT("Could not build the mesh decal."); return false; }
	OutPatch = MoveTemp(Patch); OutMesh = Mesh;
	return true;
}

struct FMD_StainStickerAsyncResult
{
	std::atomic<bool> bReady {false}, bCancelled {false};
	bool bSuccess = false;
	FMD_StainStickerPatch Patch;
	FString Error;
	TArray<FMD_StainSourceSnapshot> Sources;
};

FMD_StainStickerAsyncBake::~FMD_StainStickerAsyncBake() { Cancel(); }
bool FMD_StainStickerAsyncBake::IsReady() const { return Result->bReady.load(); }
bool FMD_StainStickerAsyncBake::IsCancelled() const { return Result->bCancelled.load(); }
void FMD_StainStickerAsyncBake::Cancel() { if (Result) Result->bCancelled.store(true); }
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
	const TSharedPtr<FMD_StainStickerSourceCache>& Cache)
{
	check(IsInGameThread());
	auto Task = MakeShared<FMD_StainStickerAsyncBake>();
	Task->Result = MakeShared<FMD_StainStickerAsyncResult, ESPMode::ThreadSafe>();
	const auto Result = Task->Result;
	TArray<MD_StainStickerBake::FGeometryInstance> Instances;
	if (!Owner.GetWorld() || Owner.GetWorld()->IsGameWorld() || HalfSize.ContainsNaN() || HalfSize.GetMin() <= 0
		|| !MD_StainStickerBake::CaptureSources(Owner, HalfSize.Length() + 1, Instances, Result->Error, &Result->Sources, Cache.Get()))
	{
		if (Result->Error.IsEmpty()) Result->Error = TEXT("No readable source surface around this placement.");
		Result->bReady.store(true);
		return Task;
	}
	// Only private geometry copies cross threads. UObject, collision, mesh upload and transactions stay on the editor thread.
	UE::Tasks::Launch(TEXT("MD Stain Sticker"), [Result, Instances = MoveTemp(Instances), HalfSize, InitialSize]() mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_WorkerBake);
		FProgressCancel Progress;
		Progress.CancelF = [Result]() { return Result->bCancelled.load(); };
		FDynamicMesh3 Surface;
		TArray<int32> Disconnected;
		TArray<MD_StainStickerBake::FSourceMesh> Sources;
		Result->Patch.InitialSize = InitialSize;
		Result->bSuccess = !Progress.Cancelled()
			&& MD_StainStickerBake::BuildSources(Instances, HalfSize.Length() + 1, Sources, Result->Error, &Progress)
			&& MD_StainStickerBake::MakeSurface(Sources, HalfSize, Surface, Disconnected, Result->Error, &Progress)
			&& MD_StainStickerBake::AddDownhillBridges(Surface, Sources, Disconnected, HalfSize, Result->Error, &Progress)
			&& !Progress.Cancelled()
			&& MD_StainStickerBake::Unfold(Surface, HalfSize, Result->Patch, Result->Error);
		if (!Result->bSuccess && Result->Error.IsEmpty()) Result->Error = TEXT("Could not unfold this connected surface.");
		Instances.Reset(); Sources.Reset(); Surface = FDynamicMesh3();
		Result->bReady.store(true);
	}, UE::Tasks::ETaskPriority::BackgroundLow);
	return Task;
}
#endif
