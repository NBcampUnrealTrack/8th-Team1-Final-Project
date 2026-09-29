#include "Gameplay/Contamination/MD_StainSurfaceFit.h"

#if WITH_EDITOR
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "HAL/IConsoleManager.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "MeshQueries.h"
#include "Implicit/Morphology.h"
#include "Util/ProgressCancel.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Generators/MarchingCubes.h"
#include "MeshSimplification.h"
#include "Selections/MeshConnectedComponents.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "World/Contamination/MD_ContaminationItem.h"
#include "World/Contamination/MD_SurfaceStain.h"

struct FMD_StainSurfaceFitJob
{
	EMD_SurfaceStainVolumePlacement VolumePlacementPolicy;
	bool bParallelSamples = true;
	double EdgeStartCm=0,EdgeRadiusCm=0;
	TArray<FMD_SurfaceStainVolumeStage> VolumeStages, PreparedStages;
	FMD_SurfaceStainVolumeStage PreviewStage;
};

namespace MD_SurfaceFit
{
	using namespace UE::Geometry;
	// Captured on the game thread so changing the diagnostic switch never changes a running job.
	TAutoConsoleVariable<int32> ParallelSamples(TEXT("MD.Slime.ParallelSurfaceSamples"), 1,
		TEXT("Parallelize precise coating samples while preserving sequential mesh construction. 0 = reference."));

	// A fit owns these immutable meshes and trees. Tree pointers must never outlive or move their meshes.
	struct FSurfaceQueries
	{
		FDynamicMesh3 Receiver, Coverage;
		FDynamicMeshAABBTree3 ReceiverTree, CoverageTree;
		explicit FSurfaceQueries(const FMD_StainStickerPatch& Patch)
			: ReceiverTree(&Receiver, false), CoverageTree(&Coverage, false)
		{
			for (const auto& P : Patch.FitReceiverVertices) Receiver.AppendVertex(P);
			for (const auto& T : Patch.FitReceiverTriangles) Receiver.AppendTriangle(T.X,T.Y,T.Z);
			for (const auto& P : Patch.Positions) Coverage.AppendVertex(FVector3d(P));
			for (int32 I=0;I<Patch.Positions.Num();I+=3) Coverage.AppendTriangle(I,I+1,I+2);
			ReceiverTree.Build(); CoverageTree.Build();
		}
		FSurfaceQueries(const FSurfaceQueries&) = delete;
		FSurfaceQueries& operator=(const FSurfaceQueries&) = delete;
	};

	// Conservative broad phase only. Keep triangle order and all original narrow-phase arithmetic,
	// including ties/early exit, so accepted points produce exactly the same geometry.
	class FUVTriangleLookup
	{
		static constexpr int32 Resolution = 32;
		static constexpr SIZE_T MaxBytes = 16 * 1024 * 1024;
		TArray<TArray<int32>> Bins;
		SIZE_T AllocatedBytes = 0;
	public:
		void Build(const FMD_StainStickerPatch& Patch, FProgressCancel* Progress)
		{
			Bins.SetNum(Resolution*Resolution); AllocatedBytes=Bins.GetAllocatedSize();
			// Sample accepts distance squared < .0004. Expansion includes rounding at bin borders.
			constexpr double Margin = .020000000001;
			for (int32 I=0; I<Patch.UVs.Num(); I+=3)
			{
				if ((I%384==0 && Progress && Progress->Cancelled())
					|| !FMath::IsFinite(Patch.UVs[I].X) || !FMath::IsFinite(Patch.UVs[I].Y)
					|| !FMath::IsFinite(Patch.UVs[I+1].X) || !FMath::IsFinite(Patch.UVs[I+1].Y)
					|| !FMath::IsFinite(Patch.UVs[I+2].X) || !FMath::IsFinite(Patch.UVs[I+2].Y))
				{ Bins.Empty(); AllocatedBytes=0; return; }
				const double MinX=FMath::Min3(double(Patch.UVs[I].X),double(Patch.UVs[I+1].X),double(Patch.UVs[I+2].X))-Margin;
				const double MinY=FMath::Min3(double(Patch.UVs[I].Y),double(Patch.UVs[I+1].Y),double(Patch.UVs[I+2].Y))-Margin;
				const double MaxX=FMath::Max3(double(Patch.UVs[I].X),double(Patch.UVs[I+1].X),double(Patch.UVs[I+2].X))+Margin;
				const double MaxY=FMath::Max3(double(Patch.UVs[I].Y),double(Patch.UVs[I+1].Y),double(Patch.UVs[I+2].Y))+Margin;
				if (MaxX<0 || MaxY<0 || MinX>1 || MinY>1) continue;
				auto Cell=[](double V) { return FMath::Min(Resolution-1,FMath::FloorToInt(FMath::Clamp(V,0.,1.)*Resolution)); };
				for (int32 Y=Cell(MinY); Y<=Cell(MaxY); ++Y)
				for (int32 X=Cell(MinX); X<=Cell(MaxX); ++X)
				{
					auto& Bin=Bins[Y*Resolution+X];
					AllocatedBytes-=Bin.GetAllocatedSize(); Bin.Add(I); AllocatedBytes+=Bin.GetAllocatedSize();
					// Pathological UV overlap falls back to the original scan instead of growing a cache indefinitely.
					if (AllocatedBytes>MaxBytes) { Bins.Empty(); AllocatedBytes=0; return; }
				}
			}
		}
		const TArray<int32>* Find(FVector2D UV) const
		{
			if (Bins.IsEmpty() || !FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y)
				|| UV.X<0 || UV.Y<0 || UV.X>1 || UV.Y>1) return nullptr;
			const int32 X=FMath::Min(Resolution-1,FMath::FloorToInt(UV.X*Resolution));
			const int32 Y=FMath::Min(Resolution-1,FMath::FloorToInt(UV.Y*Resolution));
			return &Bins[Y*Resolution+X];
		}
	};

	class FParallelSampleMarchingCubes final : public FMarchingCubes
	{
	public:
		bool bParallelSamples = false;

		FMeshShapeGenerator& Generate() override
		{
			if (!bParallelSamples || !bEnableValueCaching || bParallelCompute
				|| RootMode != ERootfindingModes::SingleLerp)
				return FMarchingCubes::Generate();
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_OrderedMarchingCubes);
			if (!ensure(Validate())) return *this;
			SetDimensions();
			GridBounds = FAxisAlignedBox3i(FVector3i::Zero(), CellDimensions - FVector3i(1,1,1));
			BlockedCornerValuesGrid.Reset(CellDimensions.X+1, CellDimensions.Y+1, CellDimensions.Z+1, FMathf::MaxReal);
			InitHashTables();
			ResetMesh();
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_ParallelSamples);
				const FVector3i Blocks = BlockedCornerValuesGrid.GetBlockDimensions();
				const int32 Count = Blocks.X * Blocks.Y * Blocks.Z;
				constexpr int32 BlockSize = FBlockedDenseGrid3f::BlockSize;
				// Each fixed-size cache block has one writer, including its lazy allocation.
				// Join small batches so only the owner polls the non-thread-safe ProgressCancel.
				// This reuses the original float cache: no full-grid duplicate or concurrent stages.
				for (int32 First=0; First<Count; First+=4)
				{
					if (CancelF()) return *this;
					ParallelFor(FMath::Min(4,Count-First), [this,First](int32 Offset)
					{
						const FVector3i Start = BlockedCornerValuesGrid.BlockIndexToBlockIJK(First+Offset)*BlockSize;
						const FVector3i End(FMath::Min(Start.X+BlockSize,CellDimensions.X+1),
							FMath::Min(Start.Y+BlockSize,CellDimensions.Y+1),FMath::Min(Start.Z+BlockSize,CellDimensions.Z+1));
						for (int32 Z=Start.Z; Z<End.Z; ++Z)
						for (int32 Y=Start.Y; Y<End.Y; ++Y)
						for (int32 X=Start.X; X<End.X; ++X)
						{
							const FVector3i IJK(X,Y,Z);
							BlockedCornerValuesGrid.SetValue(IJK,static_cast<float>(Implicit(corner_pos(IJK))));
						}
					}, EParallelForFlags::BackgroundPriority);
				}
			}
			// Same float values and same traversal as the reference preserve QEM topology and asset hashes.
			generate_basic();
			BuildMesh();
			return *this;
		}
	};

	// Recover intrinsic coordinates of the two authored bent variants before fitting a new receiver.
	template<typename T> FVector Unbend(const T& Definition, FVector P)
	{
		if (Definition.VolumePlacementPolicy != EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport
			|| P.Y <= Definition.EdgeStartCm) return P;
		const double R = Definition.EdgeRadiusCm, Span = R > 0 ? R : 0.3;
		const double Y = P.Y - Definition.EdgeStartCm, Z = P.Z + R;
		if (Z <= 0) return FVector(P.X, Definition.EdgeStartCm + Span * UE_HALF_PI - Z / 0.45, Y - R);
		return FVector(P.X, Definition.EdgeStartCm + Span * FMath::Atan2(Y, Z), FMath::Sqrt(Y * Y + Z * Z) - R);
	}

	bool Sample(const FMD_StainStickerPatch& Patch, FVector2D UV, FVector& Position, FVector& Normal,
		const FUVTriangleLookup* Lookup = nullptr)
	{
		double Best = TNumericLimits<double>::Max();
		const TArray<int32>* Candidates=Lookup ? Lookup->Find(UV) : nullptr;
		const int32 Count=Candidates ? Candidates->Num() : Patch.UVs.Num()/3;
		for (int32 Candidate=0; Candidate<Count; ++Candidate)
		{
			const int32 I=Candidates ? (*Candidates)[Candidate] : Candidate*3;
			const FVector A(Patch.UVs[I].X, Patch.UVs[I].Y, 0), B(Patch.UVs[I+1].X, Patch.UVs[I+1].Y, 0), C(Patch.UVs[I+2].X, Patch.UVs[I+2].Y, 0);
			const FVector Point(UV.X, UV.Y, 0);
			const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Point, A, B, C);
			const double Distance = FVector::DistSquared(Point, Closest);
			if (Distance >= Best) continue;
			const double Den = (B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
			if (FMath::Abs(Den) < 1.e-14) continue;
			const double X = ((B.Y-C.Y)*(Closest.X-C.X)+(C.X-B.X)*(Closest.Y-C.Y))/Den;
			const double Y = ((C.Y-A.Y)*(Closest.X-C.X)+(A.X-C.X)*(Closest.Y-C.Y))/Den;
			const FVector PA(Patch.Positions[I]), PB(Patch.Positions[I+1]), PC(Patch.Positions[I+2]);
			Position = PA * X + PB * Y + PC * (1-X-Y);
			Normal = Patch.FitNormals.Num()==Patch.Positions.Num()
				? (FVector(Patch.FitNormals[I])*X+FVector(Patch.FitNormals[I+1])*Y+FVector(Patch.FitNormals[I+2])*(1-X-Y)).GetSafeNormal()
				: FVector::CrossProduct(PC-PA, PB-PA).GetSafeNormal();
			Best = Distance;
			if (Best < 1.e-16) break;
		}
		return Best < 0.0004 && !Normal.IsNearlyZero();
	}

	// Build a single coating around the actual receiver rather than stretching triangles across
	// a folded parameter grid. The grid only controls coverage; receiver distance controls contact.
	bool MakeCoating(const FMD_StainSurfaceFitJob& Source, int32 StageIndex,
		const FMD_StainStickerPatch& Patch, FVector2D Extent, double Scale,
		TArray<FVector>& Positions, TArray<int32>& Indices, FProgressCancel* Progress, bool bPreview = false,
		const FSurfaceQueries* SharedQueries = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_Coating);
		TUniquePtr<FSurfaceQueries> LocalQueries;
		if (!SharedQueries) { LocalQueries=MakeUnique<FSurfaceQueries>(Patch); SharedQueries=LocalQueries.Get(); }
		const auto& Receiver=SharedQueries->Receiver; const auto& Coverage=SharedQueries->Coverage;
		const auto& ReceiverTree=SharedQueries->ReceiverTree; const auto& CoverageTree=SharedQueries->CoverageTree;
		constexpr int32 Resolution=192;
		TArray<double> Heights; Heights.Init(-1.0,Resolution*Resolution);
		const auto& Input=Source.VolumeStages[StageIndex];
		TArray<FVector> Unbent;
		for (FVector P : Input.QueryVerticesCm) Unbent.Add(Unbend(Source,P));
		for (int32 I=0;I<Input.QueryTriangleIndices.Num();I+=3)
		{
			const FVector A=Unbent[Input.QueryTriangleIndices[I]], B=Unbent[Input.QueryTriangleIndices[I+1]], C=Unbent[Input.QueryTriangleIndices[I+2]];
			const double Den=(B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
			if (FMath::Abs(Den)<1.e-10) continue;
			const FVector2D Min(FMath::Min3(A.X,B.X,C.X),FMath::Min3(A.Y,B.Y,C.Y)), Max(FMath::Max3(A.X,B.X,C.X),FMath::Max3(A.Y,B.Y,C.Y));
			const FVector2D GridMin=(Min*Scale/(Extent*2)+FVector2D(.5,.5))*(Resolution-1), GridMax=(Max*Scale/(Extent*2)+FVector2D(.5,.5))*(Resolution-1);
			for (int32 Y=FMath::Max(0,FMath::FloorToInt(GridMin.Y));Y<=FMath::Min(Resolution-1,FMath::CeilToInt(GridMax.Y));++Y)
			for (int32 X=FMath::Max(0,FMath::FloorToInt(GridMin.X));X<=FMath::Min(Resolution-1,FMath::CeilToInt(GridMax.X));++X)
			{
				const FVector2D P=(FVector2D(X,Y)/(Resolution-1)-FVector2D(.5,.5))*(Extent*2)/Scale;
				const double U=((B.Y-C.Y)*(P.X-C.X)+(C.X-B.X)*(P.Y-C.Y))/Den;
				const double V=((C.Y-A.Y)*(P.X-C.X)+(A.X-C.X)*(P.Y-C.Y))/Den;
				if (U>=0 && V>=0 && U+V<=1) Heights[Y*Resolution+X]=FMath::Max(Heights[Y*Resolution+X],A.Z*U+B.Z*V+C.Z*(1-U-V));
			}
		}
		auto Height=[&](FVector2d UV)
		{
			const double X=FMath::Clamp(UV.X,0.,1.)*(Resolution-1),Y=FMath::Clamp(UV.Y,0.,1.)*(Resolution-1);
			const int32 IX=FMath::Min(int32(X),Resolution-2),IY=FMath::Min(int32(Y),Resolution-2);
			return FMath::Lerp(FMath::Lerp(Heights[IY*Resolution+IX],Heights[IY*Resolution+IX+1],X-IX),
				FMath::Lerp(Heights[(IY+1)*Resolution+IX],Heights[(IY+1)*Resolution+IX+1],X-IX),Y-IY);
		};
		FAxisAlignedBox3d Bounds;
		for (const FVector& P : Positions) Bounds.Contain(P);
		Bounds.Expand(10.0);
		const double Reach=FMath::Clamp(Extent.GetMax()/16.,2.0,5.0);
		FParallelSampleMarchingCubes MC; MC.Bounds=Bounds;
		MC.bParallelSamples=Source.bParallelSamples && !bPreview;
		// The estimate has a bounded voxel grid. Final geometry keeps the original exact settings.
		MC.CubeSize=bPreview ? FMath::Max(1.25,Bounds.MaxDim()/96.) : FMath::Max(.50,Bounds.MaxDim()/256.);
		MC.CancelF=[Progress]() { return Progress && Progress->Cancelled(); };
		MC.bParallelCompute=false; MC.bEnableValueCaching=true;
		MC.Implicit=[&](FVector3d P)
		{
			double Distance;
			const int32 RT=ReceiverTree.FindNearestTriangle(P,Distance,IMeshSpatial::FQueryOptions(4.0));
			if (RT<0) return -4.0;
			auto RQ=TMeshQueries<FDynamicMesh3>::TriangleDistance(Receiver,RT,P);
			const FVector3d Q=RQ.ClosestTrianglePoint;
			const int32 CT=CoverageTree.FindNearestTriangle(Q,Distance,IMeshSpatial::FQueryOptions(Reach));
			if (CT<0) return -4.0;
			const auto CQ=TMeshQueries<FDynamicMesh3>::TriangleDistance(Coverage,CT,Q);
			const auto T=Coverage.GetTriangle(CT); const FVector B=CQ.TriangleBaryCoords;
			const FVector2d UV=FVector2d(Patch.UVs[T.A])*B.X+FVector2d(Patch.UVs[T.B])*B.Y+FVector2d(Patch.UVs[T.C])*B.Z;
			const double H=Height(UV);
			if (H<0) return -1.0;
			// An unsigned receiver shell also supports open furniture panels and thin trim.
			// Its buried portion is hidden by the receiver; the visible surface remains H thick.
			const double D=FMath::Sqrt(RQ.GetSquared());
			return FMath::Min(H+.15-D,Reach-FMath::Sqrt(Distance));
		};
		MC.Generate(); if (Progress && Progress->Cancelled()) return false;
		FDynamicMesh3 Coating(&MC);
		if (Coating.TriangleCount()==0) return false;
		FDynamicMeshAABBTree3 CoatingTree(&Coating);
		struct FStableMorphology : TImplicitMorphology<FDynamicMesh3>
		{ FStableMorphology() { MarchingCubes.bParallelCompute=false; } } Close;
		Close.Source=&Coating; Close.SourceSpatial=&CoatingTree;
		Close.MorphologyOp=TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close;
		Close.Distance=2.5; Close.GridCellSize=MC.CubeSize; Close.MeshCellSize=MC.CubeSize;
		Close.CancelF=MC.CancelF;
		FDynamicMesh3 Rounded(&Close.Generate());
		if (Progress && Progress->Cancelled()) return false;
		Coating=MoveTemp(Rounded);
		if (Coating.TriangleCount()==0) return false;
		FMeshConnectedComponents Components(&Coating); Components.FindConnectedTriangles();
		int32 Largest=INDEX_NONE,Count=0;
		for (int32 I=0;I<Components.Components.Num();++I)
			if (Components.Components[I].Indices.Num()>Count) { Largest=I; Count=Components.Components[I].Indices.Num(); }
		for (int32 I=0;I<Components.Components.Num();++I) if (I!=Largest)
			for (int32 T : Components.Components[I].Indices) Coating.RemoveTriangle(T);
		// Sub-cell smoothing suppresses voxel stair steps without flattening the screen recess.
		for (int32 Pass=0;Pass<3;++Pass)
		{
			TArray<FVector3d> Next; Next.SetNum(Coating.MaxVertexID());
			for (int32 V : Coating.VertexIndicesItr())
			{
				FVector3d Sum=FVector3d::Zero(); int32 Neighbors=0;
				for (int32 N : Coating.VtxVerticesItr(V)) { Sum+=Coating.GetVertex(N); ++Neighbors; }
				Next[V]=Neighbors?FMath::Lerp(Coating.GetVertex(V),Sum/Neighbors,.25):Coating.GetVertex(V);
			}
			for (int32 V : Coating.VertexIndicesItr()) Coating.SetVertex(V,Next[V]);
		}
		FQEMSimplification Simplifier(&Coating); Simplifier.Progress=Progress; Simplifier.bPreventTinyTriangles=true;
		Simplifier.SimplifyToTriangleCount(bPreview ? 1200 : UMD_SurfaceStainDefinition::MaximumVolumeQueryTriangles);
		Coating.CompactInPlace(); Positions.Reset(); Indices.Reset();
		for (int32 V : Coating.VertexIndicesItr()) Positions.Add(Coating.GetVertex(V));
		for (int32 T : Coating.TriangleIndicesItr()) { const auto V=Coating.GetTriangle(T); Indices.Append({V.A,V.B,V.C}); }
		return Coating.IsClosed() && Coating.TriangleCount()<=UMD_SurfaceStainDefinition::MaximumVolumeQueryTriangles;
	}

	UStaticMesh* MakeMesh(UObject& Owner, UMaterialInterface* Material, const FMD_SurfaceStainVolumeStage& Stage, int32 Index, bool bFastBuild)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description); Attributes.Register();
		auto Positions = Attributes.GetVertexPositions();
		auto Normals = Attributes.GetVertexInstanceNormals();
		auto Tangents = Attributes.GetVertexInstanceTangents();
		auto UVs = Attributes.GetVertexInstanceUVs(); UVs.SetNumChannels(1);
		auto Signs = Attributes.GetVertexInstanceBinormalSigns();
		TArray<FVertexID> Vertices;
		TArray<FVector> Smooth; Smooth.Init(FVector::ZeroVector, Stage.QueryVerticesCm.Num());
		for (const FVector& P : Stage.QueryVerticesCm) { const FVertexID V = Description.CreateVertex(); Positions[V] = FVector3f(P); Vertices.Add(V); }
		for (int32 I=0; I<Stage.QueryTriangleIndices.Num(); I+=3)
		{
			const int32 A=Stage.QueryTriangleIndices[I], B=Stage.QueryTriangleIndices[I+1], C=Stage.QueryTriangleIndices[I+2];
			const FVector N=FVector::CrossProduct(Stage.QueryVerticesCm[C]-Stage.QueryVerticesCm[A],Stage.QueryVerticesCm[B]-Stage.QueryVerticesCm[A]);
			Smooth[A]+=N; Smooth[B]+=N; Smooth[C]+=N;
		}
		const FPolygonGroupID Group=Description.CreatePolygonGroup();
		for (int32 I=0; I<Stage.QueryTriangleIndices.Num(); I+=3)
		{
			TArray<FVertexInstanceID> Instances;
			for (int32 K=0; K<3; ++K)
			{
				const int32 V=Stage.QueryTriangleIndices[I+K]; const FVertexInstanceID Instance=Description.CreateVertexInstance(Vertices[V]);
				const FVector N=Smooth[V].GetSafeNormal(); FVector T,B; N.FindBestAxisVectors(T,B);
				Normals[Instance]=FVector3f(N); Tangents[Instance]=FVector3f(T); Signs[Instance]=1;
				UVs.Set(Instance,0,FVector2f(Stage.QueryVerticesCm[V].X/100,Stage.QueryVerticesCm[V].Y/100)); Instances.Add(Instance);
			}
			Description.CreateTriangle(Group,Instances);
		}
		UStaticMesh* Mesh=NewObject<UStaticMesh>(&Owner,Index<0 ? NAME_None : FName(*FString::Printf(TEXT("Stage_%d"),Index)),RF_Public|RF_Transactional);
		Mesh->CreateBodySetup(); Mesh->GetBodySetup()->bNeverNeedsCookedCollisionData=true;
		Mesh->GetBodySetup()->CollisionTraceFlag=CTF_UseSimpleAsComplex;
		Mesh->SetNumSourceModels(1); auto& Settings=Mesh->GetSourceModel(0).BuildSettings;
		Settings.bGenerateLightmapUVs=false; Settings.bRecomputeNormals=false; Settings.bRecomputeTangents=false;
		Settings.bRemoveDegenerates=false; Settings.bUseFullPrecisionUVs=true;
		Mesh->GetNaniteSettings().bEnabled=false; Mesh->GetStaticMaterials().Add(FStaticMaterial(Material));
		UStaticMesh::FBuildMeshDescriptionsParams Params; Params.bFastBuild=bFastBuild; Params.bBuildSimpleCollision=false;
		Params.bAllowCpuAccess=false; Params.bCommitMeshDescription=true; Params.bMarkPackageDirty=false;
		if (!Mesh->BuildFromMeshDescriptions({&Description},Params)) return nullptr;
		if (!bFastBuild) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
		return Mesh;
	}

	bool HasValidSavedMeshes(const UMD_SurfaceStainDefinition& Definition)
	{
		if (!Definition.IsUsable()) return false;
		for (const auto& Stage : Definition.VolumeStages)
		{
			const UStaticMesh* Mesh=Stage.Mesh;
			if (!Mesh || Mesh->bDoFastBuild) return false;
			const FStaticMeshRenderData* Data=Mesh->GetRenderData();
			if (!Data || Data->LODResources.IsEmpty() || Data->LODResources[0].GetNumVertices()==0
				|| Data->LODResources[0].GetNumTriangles()==0) return false;
			const FBoxSphereBounds& Bounds=Data->Bounds;
			if (Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN() || !FMath::IsFinite(Bounds.SphereRadius)
				|| Bounds.BoxExtent.GetMin()<=0 || Bounds.SphereRadius<=0) return false;
		}
		return true;
	}
}

FVector2D FMD_StainSurfaceFit::HalfSize(const UMD_SurfaceStainDefinition& Source,int32 Size)
{
	FVector2D Extent(1,1);
	for (const auto& Stage : Source.VolumeStages) for (FVector P : Stage.QueryVerticesCm)
	{ P=MD_SurfaceFit::Unbend(Source,P); Extent.X=FMath::Max(Extent.X,FMath::Abs(P.X)); Extent.Y=FMath::Max(Extent.Y,FMath::Abs(P.Y)); }
	return Extent*FMD_SurfaceStainSize::LinearScale(Size)+FVector2D(1,1);
}

TSharedPtr<FMD_StainSurfaceFitJob, ESPMode::ThreadSafe> FMD_StainSurfaceFit::Capture(const UMD_SurfaceStainDefinition& Source)
{
	check(IsInGameThread());
	if (!Source.IsUsable() || !Source.UsesSharedVolume()) return nullptr;
	auto Job=MakeShared<FMD_StainSurfaceFitJob,ESPMode::ThreadSafe>();
	Job->VolumePlacementPolicy=Source.VolumePlacementPolicy;
	Job->bParallelSamples=MD_SurfaceFit::ParallelSamples.GetValueOnGameThread()!=0;
	Job->EdgeStartCm=Source.EdgeStartCm; Job->EdgeRadiusCm=Source.EdgeRadiusCm;
	for (const auto& Stage : Source.VolumeStages)
	{
		auto& Copy=Job->VolumeStages.AddDefaulted_GetRef();
		Copy.QueryVerticesCm=Stage.QueryVerticesCm; Copy.QueryTriangleIndices=Stage.QueryTriangleIndices;
	}
	return Job;
}

bool FMD_StainSurfaceFit::Prepare(FMD_StainSurfaceFitJob& Source,const FMD_StainStickerPatch& Surface,
	int32 Size,FString& Error,FProgressCancel* Progress)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_Final);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Source.bParallelSamples ? TEXT("MD_SurfaceFit_Final_Optimized") : TEXT("MD_SurfaceFit_Final_Reference"));
	if (!Surface.IsValid() || Size<FMD_SurfaceStainSize::Minimum || Size>FMD_SurfaceStainSize::MaximumPlacement)
	{ Error=TEXT("Select a usable volume profile and receiving surface."); return false; }
	FVector2D Extent(1,1);
	for (const auto& Stage : Source.VolumeStages) for (FVector P : Stage.QueryVerticesCm)
	{ P=MD_SurfaceFit::Unbend(Source,P); Extent.X=FMath::Max(Extent.X,FMath::Abs(P.X)); Extent.Y=FMath::Max(Extent.Y,FMath::Abs(P.Y)); }
	const double Scale=FMD_SurfaceStainSize::LinearScale(Size);
	Extent=Extent*Scale+FVector2D(1,1);
	Source.PreparedStages.Reset();
	bool bCurved=Surface.LocalBounds.GetSize().Z>.5;
	for (const auto& N : Surface.FitNormals) bCurved|=!N.Equals(FVector3f(0,0,1),.01f);
	bCurved&=!Surface.FitReceiverTriangles.IsEmpty();
	MD_SurfaceFit::FUVTriangleLookup Lookup;
	TUniquePtr<MD_SurfaceFit::FSurfaceQueries> SharedQueries;
	Lookup.Build(Surface,Progress);
	if (Progress && Progress->Cancelled()) return false;
	if (bCurved) SharedQueries=MakeUnique<MD_SurfaceFit::FSurfaceQueries>(Surface);
	auto FitPoint = [&](const FVector& P,FVector& Fitted)
	{
		FVector Contact,Normal;
		if (!MD_SurfaceFit::Sample(Surface,FVector2D(P.X,P.Y)*Scale/(Extent*2)+FVector2D(.5,.5),Contact,Normal,
			&Lookup)) return false;
		Fitted=Contact+Normal*(P.Z+0.03);
		return true;
	};
	for (int32 Index=0; Index<Source.VolumeStages.Num(); ++Index)
	{
		if (Progress && Progress->Cancelled()) return false;
		auto& Stage=Source.PreparedStages.AddDefaulted_GetRef();
		Stage.QueryTriangleIndices=Source.VolumeStages[Index].QueryTriangleIndices;
		TArray<FVector> Positions;
		for (FVector P : Source.VolumeStages[Index].QueryVerticesCm)
		{
			P=MD_SurfaceFit::Unbend(Source,P); FVector Fitted;
			if (!FitPoint(P,Fitted))
			{ Error=TEXT("The outline reaches beyond the nearby receiving surface. Move the cursor or reduce the size."); return false; }
			Positions.Add(Fitted);
		}
		if (bCurved
			&& !MD_SurfaceFit::MakeCoating(Source,Index,Surface,Extent,Scale,Positions,Stage.QueryTriangleIndices,Progress,false,SharedQueries.Get()))
		{ Error=TEXT("Could not make a continuous coating on this surface."); return false; }
		// The runtime uses ordinary initial-size XY scaling. Store its inverse after fitting.
		for (const FVector& P : Positions) Stage.QueryVerticesCm.Add(FVector(P.X/Scale,P.Y/Scale,P.Z));
	}
	return !(Progress && Progress->Cancelled());
}

bool FMD_StainSurfaceFit::PreparePreview(FMD_StainSurfaceFitJob& Job,const FMD_StainStickerPatch& Surface,
	int32 Size,FProgressCancel* Progress)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_Preview);
	if (!Surface.IsValid() || Job.VolumeStages.IsEmpty()
		|| Size<FMD_SurfaceStainSize::Minimum || Size>FMD_SurfaceStainSize::MaximumPlacement) return false;
	FVector2D Extent(1,1);
	for (const auto& Stage : Job.VolumeStages) for (FVector P : Stage.QueryVerticesCm)
	{ P=MD_SurfaceFit::Unbend(Job,P); Extent.X=FMath::Max(Extent.X,FMath::Abs(P.X)); Extent.Y=FMath::Max(Extent.Y,FMath::Abs(P.Y)); }
	const double Scale=FMD_SurfaceStainSize::LinearScale(Size); Extent=Extent*Scale+FVector2D(1,1);
	TArray<FVector> Positions;
	for (FVector P : Job.VolumeStages[0].QueryVerticesCm)
	{
		if (Progress && Progress->Cancelled()) return false;
		P=MD_SurfaceFit::Unbend(Job,P); FVector Contact,Normal;
		if (!MD_SurfaceFit::Sample(Surface,FVector2D(P.X,P.Y)*Scale/(Extent*2)+FVector2D(.5,.5),Contact,Normal)) return false;
		Positions.Add(Contact+Normal*(P.Z+.03));
	}
	TArray<int32> Indices=Job.VolumeStages[0].QueryTriangleIndices;
	bool bCurved=Surface.LocalBounds.GetSize().Z>.5;
	for (const auto& N : Surface.FitNormals) bCurved|=!N.Equals(FVector3f(0,0,1),.01f);
	if (bCurved && !Surface.FitReceiverTriangles.IsEmpty()
		&& !MD_SurfaceFit::MakeCoating(Job,0,Surface,Extent,Scale,Positions,Indices,Progress,true)) return false;
	if (Progress && Progress->Cancelled()) return false;
	// Store world-centimeter local coordinates for the estimate; it is displayed at unit scale.
	Job.PreviewStage.QueryVerticesCm=MoveTemp(Positions);
	Job.PreviewStage.QueryTriangleIndices=MoveTemp(Indices);
	return true;
}

UStaticMesh* FMD_StainSurfaceFit::CreatePreview(UObject& Owner,UMaterialInterface* Material,const FMD_StainSurfaceFitJob& Job)
{
	check(IsInGameThread());
	return Job.PreviewStage.QueryVerticesCm.IsEmpty() ? nullptr : MD_SurfaceFit::MakeMesh(Owner,Material,Job.PreviewStage,-1,true);
}

UMD_SurfaceStainDefinition* FMD_StainSurfaceFit::Create(UObject& Owner,const UMD_SurfaceStainDefinition& Source,
	const FMD_StainSurfaceFitJob& Job,FString& Error)
{
	check(IsInGameThread());
	UMD_SurfaceStainDefinition* Result=NewObject<UMD_SurfaceStainDefinition>(&Owner,NAME_None,RF_Transactional);
	Result->Kind=Source.Kind; Result->Presentation=EMD_SurfaceStainPresentation::SharedVolume;
	Result->VolumeMaterial=Source.VolumeMaterial; Result->RemovedItemClass=Source.RemovedItemClass;
	Result->VolumePlacementPolicy=EMD_SurfaceStainVolumePlacement::ManualOnly; Result->SizeCm=Source.SizeCm;
	Result->VolumeStages=Job.PreparedStages;
	for (int32 I=0;I<Result->VolumeStages.Num();++I) Result->VolumeStages[I].Mesh=MD_SurfaceFit::MakeMesh(*Result,Result->VolumeMaterial,Result->VolumeStages[I],I,true);
	if (!Result->RebuildVolumeValidation() || !Result->IsUsable()) { Error=Result->VolumeValidationStatus; return nullptr; }
	return Result;
}

UMD_SurfaceStainDefinition* FMD_StainSurfaceFit::Build(UObject& Owner,const UMD_SurfaceStainDefinition& Source,
	const FMD_StainStickerPatch& Surface,int32 Size,FString& Error)
{
	auto Job=Capture(Source);
	return Job && Prepare(*Job,Surface,Size,Error) ? Create(Owner,Source,*Job,Error) : nullptr;
}

UMD_SurfaceStainDefinition* FMD_StainSurfaceFit::BuildFinal(AMD_SurfaceStain& Owner,
	const UMD_SurfaceStainDefinition& Prepared,FString& Error)
{
	check(IsInGameThread());
	if (!Prepared.UsesSharedVolume() || !IsValid(Prepared.VolumeMaterial) || !Prepared.RemovedItemClass)
	{ Error=TEXT("The prepared slime is not valid for placement."); return nullptr; }
	UMD_SurfaceStainDefinition* Saved=NewObject<UMD_SurfaceStainDefinition>(&Owner,NAME_None,RF_Transactional);
	Saved->Kind=Prepared.Kind; Saved->Presentation=Prepared.Presentation;
	Saved->VolumeMaterial=Prepared.VolumeMaterial; Saved->RemovedItemClass=Prepared.RemovedItemClass;
	Saved->VolumePlacementPolicy=Prepared.VolumePlacementPolicy; Saved->SizeCm=Prepared.SizeCm;
	Saved->VolumeStages=Prepared.VolumeStages;
	// Clipboard imports carry coordinates, not static-mesh bulk data. Validate before indexing them.
	if (!Saved->ValidateVolumeGeometry(false)) { Error=Saved->VolumeValidationStatus; return nullptr; }
	for (int32 I=0; I<Saved->VolumeStages.Num(); ++I)
	{
		auto& Stage=Saved->VolumeStages[I];
		// Never duplicate a preview mesh: its persisted bDoFastBuild flag bypasses normal bounds building on reload.
		Stage.Mesh=MD_SurfaceFit::MakeMesh(*Saved,Saved->VolumeMaterial,Stage,I,false);
		if (Stage.Mesh)
		{
			Stage.Mesh->ClearFlags(RF_Public);
			Stage.Mesh->SetFlags(RF_TextExportTransient);
		}
		Saved->FittedMeshes.Add(Stage.Mesh);
	}
	if (!Saved->RebuildVolumeValidation() || !MD_SurfaceFit::HasValidSavedMeshes(*Saved))
	{ Error=TEXT("Could not build valid render bounds for the fitted slime asset."); return nullptr; }
	return Saved;
}
#endif
