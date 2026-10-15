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
#include "Util/ProgressCancel.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Generators/MarchingCubes.h"
#include "MeshSimplification.h"
#include "ProjectionTargets.h"
#include "Selections/MeshConnectedComponents.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "World/Contamination/MD_ContaminationItem.h"
#include "World/Contamination/MD_SurfaceStain.h"

DEFINE_LOG_CATEGORY_STATIC(LogMDStainSurfaceFit, Log, All);

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
		{ Build(Patch.UVs,Progress); }
		void Build(const TArray<FVector2f>& UVs, FProgressCancel* Progress, double Margin=.020000000001)
		{
			Bins.SetNum(Resolution*Resolution); AllocatedBytes=Bins.GetAllocatedSize();
			// Sample accepts distance squared < .0004. Expansion includes rounding at bin borders.
			for (int32 I=0; I<UVs.Num(); I+=3)
			{
				if ((I%384==0 && Progress && Progress->Cancelled())
					|| !FMath::IsFinite(UVs[I].X) || !FMath::IsFinite(UVs[I].Y)
					|| !FMath::IsFinite(UVs[I+1].X) || !FMath::IsFinite(UVs[I+1].Y)
					|| !FMath::IsFinite(UVs[I+2].X) || !FMath::IsFinite(UVs[I+2].Y))
				{ Bins.Empty(); AllocatedBytes=0; return; }
				const double MinX=FMath::Min3(double(UVs[I].X),double(UVs[I+1].X),double(UVs[I+2].X))-Margin;
				const double MinY=FMath::Min3(double(UVs[I].Y),double(UVs[I+1].Y),double(UVs[I+2].Y))-Margin;
				const double MaxX=FMath::Max3(double(UVs[I].X),double(UVs[I+1].X),double(UVs[I+2].X))+Margin;
				const double MaxY=FMath::Max3(double(UVs[I].Y),double(UVs[I+1].Y),double(UVs[I+2].Y))+Margin;
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

	struct FSourceDimensionsEntry
	{
		uint32 Revision = 0;
		FVector Dimensions = FVector::ZeroVector;
		bool bValid = false;
	};

	bool SourceDimensions(const UMD_SurfaceStainDefinition& Source, FVector& OutDimensions)
	{
		check(IsInGameThread());
		OutDimensions = FVector::ZeroVector;
		// A fitted actor owns a snapshot, not an original shape that may be fitted again.
		if (!Source.IsAsset() || !Source.UsesSharedVolume()) return false;
		static TMap<TWeakObjectPtr<const UMD_SurfaceStainDefinition>, FSourceDimensionsEntry> Cache;
		const TWeakObjectPtr<const UMD_SurfaceStainDefinition> Key(&Source);
		const uint32 Revision = Source.GetAuthoringRevision();
		if (const FSourceDimensionsEntry* Entry = Cache.Find(Key); Entry && Entry->Revision == Revision)
		{
			OutDimensions = Entry->Dimensions;
			return Entry->bValid;
		}
		// Prune only on a cache miss; the bounded weak cache never retains source assets.
		for (auto It = Cache.CreateIterator(); It; ++It)
			if (!It.Key().IsValid()) It.RemoveCurrent();
		if (Cache.Num() >= 64 && !Cache.Contains(Key)) Cache.Reset();
		FSourceDimensionsEntry& Entry = Cache.FindOrAdd(Key);
		Entry = FSourceDimensionsEntry();
		Entry.Revision = Revision;
		if (!Source.IsUsable() || Source.VolumeStages.IsEmpty()) return false;
		FBox Bounds(ForceInit);
		for (FVector P : Source.VolumeStages[0].QueryVerticesCm)
		{
			if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z)) return false;
			P = Unbend(Source, P);
			if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z)) return false;
			Bounds += P;
		}
		if (!Bounds.IsValid) return false;
		const FVector Dimensions = Bounds.GetSize();
		if (!FMath::IsFinite(Dimensions.X) || !FMath::IsFinite(Dimensions.Y) || !FMath::IsFinite(Dimensions.Z)
			|| Dimensions.X <= UE_SMALL_NUMBER || Dimensions.Y <= UE_SMALL_NUMBER || Dimensions.Z <= UE_SMALL_NUMBER)
			return false;
		Entry.Dimensions = OutDimensions = Dimensions;
		Entry.bValid = true;
		return true;
	}

	bool MaximumPlacementSize(const UMD_SurfaceStainDefinition& Source, bool bFollowSurface,
		int32& OutMaximum, FString& Error)
	{
		OutMaximum = FMD_SurfaceStainSize::MaximumPlacement;
		if (bFollowSurface) return true;
		OutMaximum = FMath::Min(Source.MaximumPlanarInitialSize, FMD_SurfaceStainSize::MaximumPlacement);
		if (OutMaximum < FMD_SurfaceStainSize::Minimum)
		{ Error = TEXT("This original profile has no supported placement-size range."); return false; }
		switch (Source.VolumePlacementPolicy)
		{
		case EMD_SurfaceStainVolumePlacement::PlanarSupport:
			return Source.CanPlacePlanarVolume(FMD_SurfaceStainSize::Minimum, Error)
				&& Source.CanPlacePlanarVolume(OutMaximum, Error);
		case EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport:
			return Source.CanPlaceEdgeVolume(FMD_SurfaceStainSize::Minimum, Error)
				&& Source.CanPlaceEdgeVolume(OutMaximum, Error);
		default:
			Error = TEXT("This original profile requires Follow Surface for width-based placement.");
			return false;
		}
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

	bool TriangleContainsUV(FVector2D P, FVector2D A, FVector2D B, FVector2D C, FVector3d& Weights)
	{
		const double Den=(B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
		if (FMath::Abs(Den)<1.e-14) return false;
		Weights.X=((B.Y-C.Y)*(P.X-C.X)+(C.X-B.X)*(P.Y-C.Y))/Den;
		Weights.Y=((C.Y-A.Y)*(P.X-C.X)+(A.X-C.X)*(P.Y-C.Y))/Den;
		Weights.Z=1-Weights.X-Weights.Y;
		return Weights.X>=-1.e-10 && Weights.Y>=-1.e-10 && Weights.Z>=-1.e-10;
	}

	// Prove the ordinary walk's entire UV rectangle once. Triangle count alone cannot prove coverage.
	bool HasCompleteCoverageRectangle(const FMD_StainStickerPatch& Patch)
	{
		constexpr int32 Cells=32,Triangles=Cells*Cells*2;
		if (Patch.UVs.Num()!=Triangles*3) return false;
		TBitArray<> Seen(false,Triangles);
		for (int32 I=0;I<Patch.UVs.Num();I+=3)
		{
			FIntPoint P[3]; int32 IDs[3];
			for (int32 K=0;K<3;++K)
			{
				const FVector2D V=FVector2D(Patch.UVs[I+K])*Cells;
				if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y)) return false;
				P[K]=FIntPoint(FMath::RoundToInt(V.X),FMath::RoundToInt(V.Y));
				if (P[K].X<0 || P[K].Y<0 || P[K].X>Cells || P[K].Y>Cells
					|| !V.Equals(FVector2D(P[K].X,P[K].Y),1.e-6)) return false;
				IDs[K]=P[K].Y*(Cells+1)+P[K].X;
			}
			const int32 X=FMath::Min3(P[0].X,P[1].X,P[2].X),Y=FMath::Min3(P[0].Y,P[1].Y,P[2].Y);
			if (X>=Cells || Y>=Cells) return false;
			if (IDs[0]>IDs[1]) Swap(IDs[0],IDs[1]);
			if (IDs[1]>IDs[2]) Swap(IDs[1],IDs[2]);
			if (IDs[0]>IDs[1]) Swap(IDs[0],IDs[1]);
			const int32 A=Y*(Cells+1)+X,B=A+1,C=A+Cells+1,D=C+1;
			const int32 Half=IDs[0]==A && IDs[2]==D ? (IDs[1]==B?0:(IDs[1]==C?1:INDEX_NONE)) : INDEX_NONE;
			if (Half==INDEX_NONE) return false;
			const int32 Index=(Y*Cells+X)*2+Half;
			if (Seen[Index]) return false;
			Seen[Index]=true;
		}
		return true;
	}

	bool HasExactCoverageUV(const FMD_StainStickerPatch& Patch, const FUVTriangleLookup& Lookup, FVector2D UV)
	{
		const TArray<int32>* Candidates=Lookup.Find(UV);
		const int32 Count=Candidates?Candidates->Num():Patch.UVs.Num()/3;
		for (int32 K=0;K<Count;++K)
		{
			const int32 I=Candidates?(*Candidates)[K]:K*3; FVector3d Weights;
			if (TriangleContainsUV(UV,FVector2D(Patch.UVs[I]),FVector2D(Patch.UVs[I+1]),FVector2D(Patch.UVs[I+2]),Weights)) return true;
		}
		return false;
	}

	// A sloped plane needs no voxel reconstruction. Prove the full UV-to-position map, not just its bounds.
	bool HasAffineReceiver(const FMD_StainStickerPatch& Patch)
	{
		if (Patch.Positions.Num()<3 || Patch.Positions.Num()!=Patch.UVs.Num()) return false;
		const FVector2D AUV(Patch.UVs[0]),B=FVector2D(Patch.UVs[1])-AUV,C=FVector2D(Patch.UVs[2])-AUV;
		const double Den=B.X*C.Y-B.Y*C.X;
		if (FMath::Abs(Den)<1.e-14) return false;
		const FVector A(Patch.Positions[0]),AB=FVector(Patch.Positions[1])-A,AC=FVector(Patch.Positions[2])-A;
		const FVector U=(AB*C.Y-AC*B.Y)/Den,V=(AC*B.X-AB*C.X)/Den;
		const FVector PlaneNormal=FVector::CrossProduct(U,V).GetSafeNormal();
		if (PlaneNormal.IsNearlyZero()) return false;
		const bool bFitNormals=Patch.FitNormals.Num()==Patch.Positions.Num();
		const FVector Normal=bFitNormals?FVector(Patch.FitNormals[0]).GetSafeNormal():FVector::CrossProduct(AC,AB).GetSafeNormal();
		if (Normal.IsNearlyZero() || FMath::Abs(FVector::DotProduct(PlaneNormal,Normal))<.1) return false;
		for (int32 I=0;I<Patch.Positions.Num();++I)
		{
			const FVector2D UV=FVector2D(Patch.UVs[I])-AUV;
			if (FVector::DistSquared(FVector(Patch.Positions[I]),A+U*UV.X+V*UV.Y)>.005*.005) return false;
			if (bFitNormals && !FVector(Patch.FitNormals[I]).Equals(Normal,1.e-5)) return false;
			if (!bFitNormals && I%3==0)
			{
				const FVector TN=FVector::CrossProduct(FVector(Patch.Positions[I+2])-FVector(Patch.Positions[I]),
					FVector(Patch.Positions[I+1])-FVector(Patch.Positions[I])).GetSafeNormal();
				if (!TN.Equals(Normal,1.e-5)) return false;
			}
		}
		return true;
	}

	// Raised front fillets change Z only. Their closest 3D coverage point must not shift source XY membership.
	bool HasDirectFrontUV(const FMD_StainStickerPatch& Patch,FVector2D Extent)
	{
		if (Patch.FitNormals.Num()!=Patch.Positions.Num() || Patch.UVs.Num()!=Patch.Positions.Num()) return false;
		for (int32 I=0;I<Patch.Positions.Num();++I)
		{
			const FVector2D Expected=(FVector2D(Patch.UVs[I])-FVector2D(.5,.5))*(Extent*2);
			const FVector2D Actual(Patch.Positions[I].X,Patch.Positions[I].Y);
			if (!Actual.Equals(Expected,.005) || !Patch.FitNormals[I].Equals(FVector3f(0,0,1),.001f)) return false;
		}
		return true;
	}

	// Membership and thickness are independent: a missing raster sample is never a negative thickness.
	// A cell wholly contained by one projected source triangle has a cheap exact membership proof.
	class FProjectedSourceField
	{
	public:
		static constexpr int32 Resolution=192;
		TArray<double> Heights,TriangleHeights;
		TArray<uint8> Valid,InteriorCells;
		TArray<FVector2f> TriangleUVs;
		TArray<FVector2D> ExactTriangleUVs;
		FUVTriangleLookup Lookup;
		double MinimumHeight=TNumericLimits<double>::Max();

		bool Build(const FMD_StainSurfaceFitJob& Source,int32 StageIndex,FVector2D Extent,double Scale,FProgressCancel* Progress)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_SourceField);
			Heights.Init(0.,Resolution*Resolution); Valid.Init(0,Heights.Num()); InteriorCells.Init(0,Heights.Num());
			const auto& Input=Source.VolumeStages[StageIndex];
			TArray<FVector> Unbent; Unbent.Reserve(Input.QueryVerticesCm.Num());
			for (FVector P:Input.QueryVerticesCm) Unbent.Add(Unbend(Source,P));
			for (int32 I=0;I<Input.QueryTriangleIndices.Num();I+=3)
			{
				if (I%384==0 && Progress && Progress->Cancelled()) return false;
				FVector2D UV[3];
				for (int32 K=0;K<3;++K)
				{
					const FVector P=Unbent[Input.QueryTriangleIndices[I+K]];
					UV[K]=FVector2D(P.X,P.Y)*Scale/(Extent*2)+FVector2D(.5,.5);
					TriangleUVs.Add(FVector2f(UV[K])); ExactTriangleUVs.Add(UV[K]); TriangleHeights.Add(P.Z);
				}
				const FVector2D Min(FMath::Min3(UV[0].X,UV[1].X,UV[2].X),FMath::Min3(UV[0].Y,UV[1].Y,UV[2].Y));
				const FVector2D Max(FMath::Max3(UV[0].X,UV[1].X,UV[2].X),FMath::Max3(UV[0].Y,UV[1].Y,UV[2].Y));
				for (int32 Y=FMath::Max(0,FMath::FloorToInt(Min.Y*(Resolution-1)));Y<=FMath::Min(Resolution-1,FMath::CeilToInt(Max.Y*(Resolution-1)));++Y)
				for (int32 X=FMath::Max(0,FMath::FloorToInt(Min.X*(Resolution-1)));X<=FMath::Min(Resolution-1,FMath::CeilToInt(Max.X*(Resolution-1)));++X)
				{
					FVector3d W; const FVector2D P=FVector2D(X,Y)/(Resolution-1); const int32 Index=Y*Resolution+X;
					if (!TriangleContainsUV(P,UV[0],UV[1],UV[2],W)) continue;
					const double H=TriangleHeights[I]*W.X+TriangleHeights[I+1]*W.Y+TriangleHeights[I+2]*W.Z;
					Heights[Index]=Valid[Index]?FMath::Max(Heights[Index],H):H; Valid[Index]=1;
					if (X<Resolution-1 && Y<Resolution-1 && !InteriorCells[Index])
					{
						InteriorCells[Index]=TriangleContainsUV(FVector2D(X+1,Y)/(Resolution-1),UV[0],UV[1],UV[2],W)
							&& TriangleContainsUV(FVector2D(X,Y+1)/(Resolution-1),UV[0],UV[1],UV[2],W)
							&& TriangleContainsUV(FVector2D(X+1,Y+1)/(Resolution-1),UV[0],UV[1],UV[2],W);
					}
				}
			}
			Lookup.Build(TriangleUVs,Progress,1.e-7);
			for (int32 I=0;I<Heights.Num();++I) if (Valid[I]) MinimumHeight=FMath::Min(MinimumHeight,Heights[I]);
			// Include the true outline tips, which may fall between raster samples.
			for (const FVector& P:Unbent)
			{
				double H=0.;
				if (ExactHeight(FVector2D(P.X,P.Y)*Scale/(Extent*2)+FVector2D(.5,.5),H)) MinimumHeight=FMath::Min(MinimumHeight,H);
			}
			return !(Progress && Progress->Cancelled());
		}

		bool ExactHeight(FVector2D UV,double& Height) const
		{
			const TArray<int32>* Candidates=Lookup.Find(UV); bool Found=false;
			const int32 Count=Candidates?Candidates->Num():TriangleUVs.Num()/3;
			for (int32 K=0;K<Count;++K)
			{
				const int32 I=Candidates?(*Candidates)[K]:K*3; FVector3d W;
				if (!TriangleContainsUV(UV,ExactTriangleUVs[I],ExactTriangleUVs[I+1],ExactTriangleUVs[I+2],W)) continue;
				const double H=TriangleHeights[I]*W.X+TriangleHeights[I+1]*W.Y+TriangleHeights[I+2]*W.Z;
				Height=Found?FMath::Max(Height,H):H; Found=true;
			}
			return Found;
		}

		bool Height(FVector2D UV,double& OutHeight) const
		{
			if (UV.X<0 || UV.Y<0 || UV.X>1 || UV.Y>1) return false;
			const double X=UV.X*(Resolution-1),Y=UV.Y*(Resolution-1);
			const int32 IX=FMath::Min(int32(X),Resolution-2),IY=FMath::Min(int32(Y),Resolution-2),I=IY*Resolution+IX;
			double Exact=0.;
			if (!InteriorCells[I])
			{
				if (!ExactHeight(UV,Exact)) return false;
				OutHeight=Exact;
				return FMath::IsFinite(OutHeight) && OutHeight>=0;
			}
			const int32 IDs[]={I,I+1,I+Resolution,I+Resolution+1};
			const double FX=X-IX,FY=Y-IY,Weights[]={(1-FX)*(1-FY),FX*(1-FY),(1-FX)*FY,FX*FY};
			double Sum=0.,Weight=0.;
			for (int32 K=0;K<4;++K) if (Valid[IDs[K]]) { Sum+=Weights[K]*Heights[IDs[K]]; Weight+=Weights[K]; }
			OutHeight=Weight>1.e-12?Sum/Weight:Exact;
			return FMath::IsFinite(OutHeight) && OutHeight>=0;
		}

		bool NearOutline(FVector2D UV) const
		{
			const int32 X=FMath::Clamp(FMath::RoundToInt(UV.X*(Resolution-1)),0,Resolution-1);
			const int32 Y=FMath::Clamp(FMath::RoundToInt(UV.Y*(Resolution-1)),0,Resolution-1);
			for (int32 DY=-2;DY<=2;++DY) for (int32 DX=-2;DX<=2;++DX)
			{
				const int32 NX=X+DX,NY=Y+DY;
				if (NX<0 || NY<0 || NX>=Resolution || NY>=Resolution || !Valid[NY*Resolution+NX]) return true;
			}
			return false;
		}

		bool ValidateCoverage(const FMD_StainStickerPatch& Patch,const FUVTriangleLookup& CoverageLookup,
			bool bCompleteCoverage,FProgressCancel* Progress) const
		{
			if (bCompleteCoverage) return true;
			TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_InteriorSupport);
			for (int32 Y=0;Y<Resolution;++Y)
			{
				if (Progress && Progress->Cancelled()) return false;
				for (int32 X=0;X<Resolution;++X) for (int32 Center=0;Center<2;++Center)
				{
					if (Center && (X==Resolution-1 || Y==Resolution-1)) continue;
					const FVector2D UV=(FVector2D(X,Y)+FVector2D(Center*.5,Center*.5))/(Resolution-1); double H=0.;
					if (Height(UV,H) && !HasExactCoverageUV(Patch,CoverageLookup,UV)) return false;
				}
			}
			return true;
		}
	};

	// Build a single coating around the actual receiver rather than stretching triangles across
	// a folded parameter grid. The grid only controls coverage; receiver distance controls contact.
	bool MakeCoating(const FMD_StainSurfaceFitJob& Source, int32 StageIndex,
		const FMD_StainStickerPatch& Patch, FVector2D Extent, double Scale,
		TArray<FVector>& Positions, TArray<int32>& Indices, FProgressCancel* Progress, bool bPreview = false,
		const FSurfaceQueries* SharedQueries = nullptr, const FAxisAlignedBox3d* SharedBounds = nullptr,
		const FUVTriangleLookup* SharedCoverageLookup = nullptr, bool bCompleteCoverage = false, FString* OutError = nullptr,
		const bool* SharedDirectFrontUV = nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_Coating);
		auto Fail=[&](const TCHAR* Reason) { if (OutError) *OutError=Reason; return false; };
		auto Cancelled=[&]() { return Progress && Progress->Cancelled(); };
		TUniquePtr<FSurfaceQueries> LocalQueries;
		if (!SharedQueries) { LocalQueries=MakeUnique<FSurfaceQueries>(Patch); SharedQueries=LocalQueries.Get(); }
		const auto& Receiver=SharedQueries->Receiver; const auto& Coverage=SharedQueries->Coverage;
		const auto& ReceiverTree=SharedQueries->ReceiverTree; const auto& CoverageTree=SharedQueries->CoverageTree;
		const bool bDirectFrontUV=SharedDirectFrontUV?*SharedDirectFrontUV:HasDirectFrontUV(Patch,Extent);
		FProjectedSourceField Field;
		if (!Field.Build(Source,StageIndex,Extent,Scale,Progress))
			return Fail(Cancelled()?TEXT("Surface fitting was cancelled."):TEXT("Could not project the original coating shape."));
		FUVTriangleLookup LocalCoverageLookup;
		if (!SharedCoverageLookup)
		{
			LocalCoverageLookup.Build(Patch,Progress); SharedCoverageLookup=&LocalCoverageLookup;
			bCompleteCoverage=HasCompleteCoverageRectangle(Patch);
		}
		if (!Field.ValidateCoverage(Patch,*SharedCoverageLookup,bCompleteCoverage,Progress))
			return Fail(Cancelled()?TEXT("Surface fitting was cancelled."):TEXT("The original shape crosses an unsupported receiving-surface opening."));
		FAxisAlignedBox3d Bounds;
		if (SharedBounds) Bounds=*SharedBounds;
		else { for (const FVector& P : Positions) Bounds.Contain(P); Bounds.Expand(10.0); }
		const double Reach=FMath::Clamp(Extent.GetMax()/16.,2.0,5.0);
		FParallelSampleMarchingCubes MC; MC.Bounds=Bounds;
		MC.bParallelSamples=Source.bParallelSamples && !bPreview;
		// The estimate has a bounded voxel grid. Final geometry keeps the original exact settings.
		MC.CubeSize=bPreview ? FMath::Max(1.25,Bounds.MaxDim()/96.) : FMath::Max(.50,Bounds.MaxDim()/256.);
		MC.CancelF=[Progress]() { return Progress && Progress->Cancelled(); };
		MC.bParallelCompute=false; MC.bEnableValueCaching=true;
		const double MinimumShell=MC.CubeSize*.9;
		// A sub-voxel shell can fragment into closed islands. Never inflate final thickness by more than .5 cm.
		if (!bPreview && MinimumShell>Field.MinimumHeight+.15+.5)
			return Fail(TEXT("This shape is too thin for the current surface-sampling pitch within 0.5 cm. Reduce the stain width."));
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
			const FVector2d UV=bDirectFrontUV?FVector2d(Q.X,Q.Y)/(Extent*2)+FVector2d(.5,.5)
				:FVector2d(Patch.UVs[T.A])*B.X+FVector2d(Patch.UVs[T.B])*B.Y+FVector2d(Patch.UVs[T.C])*B.Z;
			double H=0.; if (!Field.Height(UV,H)) return -1.0;
			// An unsigned receiver shell also supports open furniture panels and thin trim.
			// Its buried portion is hidden by the receiver; sub-voxel thickness is bounded above by the final guard.
			const double D=FMath::Sqrt(RQ.GetSquared());
			return FMath::Min(FMath::Max(H+.15,MinimumShell)-D,Reach-FMath::Sqrt(Distance));
		};
		MC.Generate(); if (Cancelled()) return Fail(TEXT("Surface fitting was cancelled."));
		FDynamicMesh3 Coating(&MC);
		if (Coating.TriangleCount()==0) return Fail(TEXT("Surface sampling produced no coating volume."));
		FMeshConnectedComponents Components(&Coating); Components.FindConnectedTriangles();
		// Discarding islands can hide missing supported coverage. Preserve the previous actor on failure instead.
		if (Components.Components.Num()!=1)
		{
			UE_LOG(LogMDStainSurfaceFit,Verbose,TEXT("Stage %d preview=%d directFrontUV=%d: %d components, %d triangles, voxel=%.6f cm"),
				StageIndex,bPreview,bDirectFrontUV,Components.Components.Num(),Coating.TriangleCount(),MC.CubeSize);
			if (UE_LOG_ACTIVE(LogMDStainSurfaceFit,Verbose))
				for (int32 I=0;I<FMath::Min(Components.Components.Num(),4);++I)
				{
					const auto& Triangles=Components.Components[I].Indices;
					const FAxisAlignedBox3d Box=Coating.GetBoundsForTriangleSelection(Triangles);
					UE_LOG(LogMDStainSurfaceFit,Verbose,TEXT("Component %d triangles=%d min=%s max=%s"),I,Triangles.Num(),*Box.Min.ToString(),*Box.Max.ToString());
				}
			return Fail(TEXT("Surface sampling produced disconnected coating islands."));
		}
		if (!Coating.IsClosed()) return Fail(TEXT("Surface sampling could not make a closed coating."));
		FDynamicMesh3 Reference=Coating;
		FDynamicMeshAABBTree3 ReferenceTree(&Reference);
		auto Location=[&](FVector3d P,FVector3d& Q,FVector3d& N,FVector2D& UV,double& Distance)
		{
			const int32 RT=ReceiverTree.FindNearestTriangle(P,Distance,IMeshSpatial::FQueryOptions(4.0));
			if (RT<0) return false;
			auto RQ=TMeshQueries<FDynamicMesh3>::TriangleDistance(Receiver,RT,P);
			Q=RQ.ClosestTrianglePoint; N=Receiver.GetTriNormal(RT); Distance=FMath::Sqrt(RQ.GetSquared());
			double CoverageDistance;
			const int32 CT=CoverageTree.FindNearestTriangle(Q,CoverageDistance,IMeshSpatial::FQueryOptions(Reach));
			if (CT<0) return false;
			const auto CQ=TMeshQueries<FDynamicMesh3>::TriangleDistance(Coverage,CT,Q);
			const auto T=Coverage.GetTriangle(CT); const FVector3d B=CQ.TriangleBaryCoords;
			UV=bDirectFrontUV?FVector2D(Q.X,Q.Y)/(Extent*2)+FVector2D(.5,.5)
				:FVector2D(Patch.UVs[T.A])*B.X+FVector2D(Patch.UVs[T.B])*B.Y+FVector2D(Patch.UVs[T.C])*B.Z;
			return true;
		};
		// Smooth tangentially, retaining the original receiver distance and a .5 cm total displacement envelope.
		TRACE_CPUPROFILER_EVENT_SCOPE(MD_SurfaceFit_ConstrainedShape);
		for (int32 Pass=0;Pass<3;++Pass)
		{
			if (Cancelled()) return Fail(TEXT("Surface fitting was cancelled."));
			TArray<FVector3d> Next; Next.SetNum(Coating.MaxVertexID());
			for (int32 V : Coating.VertexIndicesItr())
			{
				if (V%256==0 && Cancelled()) return Fail(TEXT("Surface fitting was cancelled."));
				const FVector3d P=Coating.GetVertex(V); Next[V]=P;
				FVector3d Q,N; FVector2D UV; double D=0.,H=0.;
				if (!Location(P,Q,N,UV,D) || !Field.Height(UV,H)) continue;
				FVector3d Sum=FVector3d::Zero(); int32 Neighbors=0;
				for (int32 Neighbor : Coating.VtxVerticesItr(V)) { Sum+=Coating.GetVertex(Neighbor); ++Neighbors; }
				if (!Neighbors) continue;
				FVector3d Delta=(Sum/Neighbors-P)*.25;
				Delta-=N*FVector3d::DotProduct(Delta,N);
				const double Step=Delta.Length(); if (Step>.5/3) Delta*=((.5/3)/Step);
				FVector3d CQ,CN; FVector2D CUV; double CD=0.;
				if (!Location(P+Delta,CQ,CN,CUV,CD) || !Field.Height(CUV,H)) continue;
				if (D>FMath::Max(H+.15,MinimumShell)+MC.CubeSize*.1) continue;
				const FVector3d Candidate=CQ+CN*(FVector3d::DotProduct(P-Q,N)<0?-D:D);
				if ((Candidate-Reference.GetVertex(V)).SquaredLength()<=.25) Next[V]=Candidate;
			}
			for (int32 V : Coating.VertexIndicesItr()) Coating.SetVertex(V,Next[V]);
		}
		FQEMSimplification Simplifier(&Coating); Simplifier.Progress=Progress; Simplifier.bPreventTinyTriangles=true;
		FMeshProjectionTarget Projection(&Reference,&ReferenceTree); Simplifier.SetProjectionTarget(&Projection);
		Simplifier.GeometricErrorConstraint=FQEMSimplification::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
		Simplifier.GeometricErrorTolerance=bPreview?.5:.25;
		Simplifier.bRetainQuadricMemory=true;
		Simplifier.CustomQuadricErrorScaleF=[&](const FDynamicMesh3& Mesh,int32 V)
		{
			FVector3d Q,N; FVector2D UV; double D=0.;
			return Location(Mesh.GetVertex(V),Q,N,UV,D) && Field.NearOutline(UV)?8.0:1.0;
		};
		const int32 TriangleBudget=bPreview?1200:UMD_SurfaceStainDefinition::MaximumVolumeQueryTriangles;
		Simplifier.SimplifyToTriangleCount(TriangleBudget);
		if (Cancelled()) return Fail(TEXT("Surface fitting was cancelled."));
		if (!Coating.IsClosed()) return Fail(TEXT("Contour simplification could not preserve a closed coating."));
		if (Coating.TriangleCount()>TriangleBudget || Coating.VertexCount()>UMD_SurfaceStainDefinition::MaximumVolumeQueryVertices)
			return Fail(TEXT("The coating contour cannot fit within its geometry budget while preserving the surface shape."));
		Coating.CompactInPlace(); Positions.Reset(); Indices.Reset();
		for (int32 V : Coating.VertexIndicesItr()) Positions.Add(Coating.GetVertex(V));
		for (int32 T : Coating.TriangleIndicesItr()) { const auto V=Coating.GetTriangle(T); Indices.Append({V.A,V.B,V.C}); }
		return true;
	}

	UStaticMesh* MakeMesh(UObject& Owner, UMaterialInterface* Material, const FMD_SurfaceStainVolumeStage& Stage, int32 Index, bool bFastBuild, bool bSupportRayTracing = true)
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
		Mesh->bSupportRayTracing=bSupportRayTracing;
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

bool FMD_StainSurfaceFit::GetSourceDimensions(const UMD_SurfaceStainDefinition& Source, FVector& OutDimensions)
{
	return MD_SurfaceFit::SourceDimensions(Source, OutDimensions);
}

bool FMD_StainSurfaceFit::GetSizeDimensions(const UMD_SurfaceStainDefinition& Source, int32 Size,
	FVector& OutDimensions, bool bFollowSurface)
{
	OutDimensions = FVector::ZeroVector;
	FVector Base;
	if (Size < FMD_SurfaceStainSize::Minimum || Size > FMD_SurfaceStainSize::Maximum
		|| !GetSourceDimensions(Source, Base)) return false;
	const double Scale = FMD_SurfaceStainSize::LinearScale(Size);
	const double ThicknessScale = !bFollowSurface
		&& Source.VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport ? Scale : 1.0;
	const FVector Dimensions = Base * FVector(Scale, Scale, ThicknessScale);
	if (!FMath::IsFinite(Dimensions.X) || !FMath::IsFinite(Dimensions.Y) || !FMath::IsFinite(Dimensions.Z)) return false;
	OutDimensions = Dimensions;
	return true;
}

bool FMD_StainSurfaceFit::GetAllowedWidthRange(const UMD_SurfaceStainDefinition& Source, bool bFollowSurface,
	FVector2D& OutAllowedWidth, FString& Error)
{
	Error.Reset();
	OutAllowedWidth = FVector2D::ZeroVector;
	FVector Base;
	if (!GetSourceDimensions(Source, Base))
	{ Error = TEXT("Select an original usable volume definition with finite full-stage dimensions."); return false; }
	int32 Maximum;
	if (!MD_SurfaceFit::MaximumPlacementSize(Source, bFollowSurface, Maximum, Error)) return false;
	// Input limits use the same double-precision domain as the width-to-size formula.
	const FVector2D Range(Base.X * FMath::Sqrt(double(FMD_SurfaceStainSize::Minimum) / FMD_SurfaceStainSize::Reference),
		Base.X * FMath::Sqrt(double(Maximum) / FMD_SurfaceStainSize::Reference));
	if (!FMath::IsFinite(Range.X) || !FMath::IsFinite(Range.Y) || Range.X <= 0 || Range.Y < Range.X)
	{ Error = TEXT("The original definition cannot provide a finite width range."); return false; }
	OutAllowedWidth = Range;
	return true;
}

bool FMD_StainSurfaceFit::ResolveWidth(const UMD_SurfaceStainDefinition& Source, double RequestedWidthCm,
	bool bFollowSurface, int32& OutSize, FVector& OutAppliedDimensions,
	FVector2D& OutAllowedWidth, FString& Error)
{
	OutSize = 0;
	OutAppliedDimensions = FVector::ZeroVector;
	if (!GetAllowedWidthRange(Source, bFollowSurface, OutAllowedWidth, Error)) return false;
	if (!FMath::IsFinite(RequestedWidthCm) || RequestedWidthCm <= 0)
	{ Error = TEXT("Enter a finite positive width in centimeters."); return false; }
	if (RequestedWidthCm < OutAllowedWidth.X || RequestedWidthCm > OutAllowedWidth.Y)
	{
		Error = FString::Printf(TEXT("This original shape supports widths %.3f-%.3f cm in the selected mode."),
			OutAllowedWidth.X, OutAllowedWidth.Y);
		return false;
	}
	FVector Base;
	if (!GetSourceDimensions(Source, Base)) return false;
	const double Ratio = RequestedWidthCm / Base.X;
	const double RawSize = double(FMD_SurfaceStainSize::Reference) * Ratio * Ratio;
	// Check before converting to an integer, including extreme finite source/input values.
	if (!FMath::IsFinite(RawSize) || RawSize < 0 || RawSize > FMD_SurfaceStainSize::MaximumPlacement + 1.0)
	{ Error = TEXT("The requested width cannot be represented by a placement size."); return false; }
	const int32 Size = FMath::RoundToInt(RawSize);
	int32 Maximum;
	if (!MD_SurfaceFit::MaximumPlacementSize(Source, bFollowSurface, Maximum, Error)) return false;
	if (Size < FMD_SurfaceStainSize::Minimum || Size > Maximum
		|| !GetSizeDimensions(Source, Size, OutAppliedDimensions, bFollowSurface))
	{ Error = TEXT("The requested width cannot be represented by this original profile."); return false; }
	OutSize = Size;
	return true;
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
	Error.Reset();
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
	bCurved&=!MD_SurfaceFit::HasAffineReceiver(Surface);
	MD_SurfaceFit::FUVTriangleLookup Lookup;
	TUniquePtr<MD_SurfaceFit::FSurfaceQueries> SharedQueries;
	Lookup.Build(Surface,Progress);
	if (Progress && Progress->Cancelled()) return false;
	const bool bCompleteCoverage=MD_SurfaceFit::HasCompleteCoverageRectangle(Surface);
	const bool bDirectFrontUV=bCurved && MD_SurfaceFit::HasDirectFrontUV(Surface,Extent);
	if (bCurved) SharedQueries=MakeUnique<MD_SurfaceFit::FSurfaceQueries>(Surface);
	UE::Geometry::FAxisAlignedBox3d SharedBounds;
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
		if (!bCurved && !bCompleteCoverage)
		{
			MD_SurfaceFit::FProjectedSourceField Field;
			if (!Field.Build(Source,Index,Extent,Scale,Progress)
				|| !Field.ValidateCoverage(Surface,Lookup,false,Progress))
			{ Error=TEXT("The shape covers an unsupported opening. Move the cursor or reduce the size."); return false; }
		}
		// All removal stages use the Full grid origin/pitch rather than unrelated voxel alignments.
		if (bCurved && Index==0)
		{ for (const FVector& P:Positions) SharedBounds.Contain(P); SharedBounds.Expand(10.0); }
		if (bCurved
			&& !MD_SurfaceFit::MakeCoating(Source,Index,Surface,Extent,Scale,Positions,Stage.QueryTriangleIndices,
				Progress,false,SharedQueries.Get(),&SharedBounds,&Lookup,bCompleteCoverage,&Error,&bDirectFrontUV)) return false;
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
	bCurved&=!MD_SurfaceFit::HasAffineReceiver(Surface);
	if (!bCurved && !MD_SurfaceFit::HasCompleteCoverageRectangle(Surface))
	{
		MD_SurfaceFit::FProjectedSourceField Field; MD_SurfaceFit::FUVTriangleLookup Lookup;
		Lookup.Build(Surface,Progress);
		if (!Field.Build(Job,0,Extent,Scale,Progress) || !Field.ValidateCoverage(Surface,Lookup,false,Progress)) return false;
	}
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
	// Only the full prepared stage is displayed; final placement rebuilds every stage with ray tracing.
	for (int32 I=0;I<Result->VolumeStages.Num();++I) Result->VolumeStages[I].Mesh=MD_SurfaceFit::MakeMesh(*Result,Result->VolumeMaterial,Result->VolumeStages[I],I,true,I==0);
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
