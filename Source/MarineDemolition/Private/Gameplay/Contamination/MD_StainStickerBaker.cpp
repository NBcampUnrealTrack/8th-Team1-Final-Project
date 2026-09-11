#include "Gameplay/Contamination/MD_StainStickerBaker.h"

#if WITH_EDITOR
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Operations/MeshSelfUnion.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/UObjectIterator.h"

using namespace UE::Geometry;

namespace MD_StainStickerBake
{
	constexpr int32 MaximumSourceTriangles = 50000;
	constexpr int32 MaximumComponents = 32;
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

	bool Gather(AActor& Owner, double Radius, FDynamicMesh3& Combined, FString& Error)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_GatherEditor);
		const FTransform Frame = Owner.GetActorTransform();
		TArray<UStaticMeshComponent*> Components;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() != Owner.GetWorld() || !It->IsRegistered() || It->GetOwner() == &Owner
				|| !It->IsVisible() || !It->IsCollisionEnabled() || !It->bReceivesDecals || !It->GetStaticMesh()
				|| !It->Bounds.GetBox().ExpandBy(Radius).IsInsideOrOn(Frame.GetLocation())) continue;
			Components.Add(*It);
		}
		// A stable order makes touching-surface topology deterministic when rebaking.
		Components.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B) { return A.GetPathName() < B.GetPathName(); });
		if (Components.IsEmpty() || Components.Num() > MaximumComponents)
		{
			Error = TEXT("Sticker needs 1..32 nearby static-mesh surfaces. Reduce the footprint or separate the target geometry.");
			return false;
		}
		for (UStaticMeshComponent* Component : Components)
		{
			UStaticMesh* Source = Component->GetStaticMesh();
			const FMeshDescription* Description = Source->GetMeshDescription(0);
			if (!Description || Description->Triangles().Num() == 0) continue;
			if (Description->Triangles().Num() + Combined.TriangleCount() > MaximumSourceTriangles)
			{
				Error = TEXT("Sticker source exceeds the 50,000-triangle authoring limit; use a simpler surface source.");
				return false;
			}
			const FStaticMeshConstAttributes Attributes(*Description);
			const auto Positions = Attributes.GetVertexPositions();
			const auto Normals = Attributes.GetVertexInstanceNormals();
			const FTransform Transform = Component->GetComponentTransform();
			const FVector BuildScale = Source->GetNumSourceModels() > 0 ? Source->GetSourceModel(0).BuildSettings.BuildScale3D : FVector::OneVector;
			FDynamicMesh3 Part;
			TMap<FVertexID, int32> IDs;
			TMap<FIntVector, int32> Welded;
			for (const FVertexID ID : Description->Vertices().GetElementIDs())
			{
				const FVector P = Frame.InverseTransformPositionNoScale(Transform.TransformPosition(FVector(Positions[ID]) * BuildScale));
				const FIntVector Key(FMath::RoundToInt(P.X * 1000), FMath::RoundToInt(P.Y * 1000), FMath::RoundToInt(P.Z * 1000));
				int32* Existing = Welded.Find(Key);
				const int32 Vertex = Existing ? *Existing : Part.AppendVertex(P);
				if (!Existing) Welded.Add(Key, Vertex);
				IDs.Add(ID, Vertex);
			}
			bool bReverse = false, bOrientationKnown = false;
			for (const FTriangleID ID : Description->Triangles().GetElementIDs())
			{
				const auto Corners = Description->GetTriangleVertexInstances(ID);
				int32 V[3];
				for (int32 K = 0; K < 3; ++K) V[K] = IDs.FindChecked(Description->GetVertexInstanceVertex(Corners[K]));
				if (V[0] == V[1] || V[1] == V[2] || V[2] == V[0]) continue;
				if (!bOrientationKnown)
				{
					const FVector Cross = FVector::CrossProduct(Part.GetVertex(V[1]) - Part.GetVertex(V[0]), Part.GetVertex(V[2]) - Part.GetVertex(V[0]));
					const FVector Scale = Transform.GetScale3D() * BuildScale;
					const FVector N = Frame.InverseTransformVectorNoScale(Transform.GetRotation().RotateVector(FVector(Normals[Corners[0]]) / Scale));
					if (!N.ContainsNaN() && Cross.SquaredLength() > 1.e-12 && N.SquaredLength() > 1.e-12)
					{ bReverse = FVector::DotProduct(Cross, N) > 0; bOrientationKnown = true; }
				}
				Part.AppendTriangle(V[0], V[bReverse ? 2 : 1], V[bReverse ? 1 : 2]);
			}
			FMeshIndexMappings Mappings;
			FDynamicMeshEditor(&Combined).AppendMesh(&Part, Mappings);
		}
		if (Combined.TriangleCount() == 0) { Error = TEXT("No readable source surface around this placement."); return false; }
		if (Components.Num() > 1)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_UnionEditor);
			// Resolves contacts and T junctions (for example a furniture side meeting a large floor triangle).
			const FDynamicMesh3 Original(Combined);
			bool bJoined = false;
			// Imported modular tiles can differ by a few micrometres after mirrored/scaled transforms.
			// Retry from the untouched source, bounded to 0.1 mm; never use a partly failed union.
			for (const double Tolerance : {0.001, 0.01})
			{
				Combined = Original;
				FMeshSelfUnion Union(&Combined);
				// FMeshSelfUnion normalizes its input to a unit box before applying these tolerances.
				const double UnitScale = 1.0 / FMath::Clamp(Combined.GetBounds(true).MaxDim(), 0.01, 1000000.0);
				Union.SnapTolerance = Tolerance * UnitScale;
				Union.NormalOffset = Tolerance * 0.1 * UnitScale;
				Union.bTrimFlaps = false;
				Union.bWeldSharedEdges = true;
				if (Union.Compute()) { bJoined = true; break; }
			}
			if (!bJoined) { Error = TEXT("Touching surfaces could not be joined into a continuous sticker surface."); return false; }
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
				if (FVector::CrossProduct(Triangle[1].Position - Triangle[0].Position, Triangle[2].Position - Triangle[0].Position).SquaredLength() < 1.e-12) continue;
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
	FDynamicMesh3 Surface;
	if (!MD_StainStickerBake::Gather(Owner, HalfSize.Length() + 1, Surface, Error)) return false;
	FMD_StainStickerPatch Patch; Patch.InitialSize = InitialSize;
	if (!MD_StainStickerBake::Unfold(Surface, HalfSize, Patch, Error)) return false;
	UStaticMesh* Mesh = MD_StainStickerBake::MakeMesh(Owner, Patch, Material);
	if (!Mesh) { Error = TEXT("Could not build the mesh decal."); return false; }
	OutPatch = MoveTemp(Patch); OutMesh = Mesh;
	return true;
}
#endif
