#include "MD_StainDownhillBridge.h"

#if WITH_EDITOR
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Util/ProgressCancel.h"

using namespace UE::Geometry;

namespace
{
	constexpr double ContactTolerance = 0.02;
	constexpr int32 MaximumBridges = 32;

	// On the clicked plane, these bounds are the actual sticker footprint, including its rotation.
	bool ClipEdge(FVector& A, FVector& B, const FVector2D& HalfSize)
	{
		const FVector Delta = B - A;
		const FVector Extent(HalfSize.X, HalfSize.Y, HalfSize.Length() + 1);
		double Min = 0, Max = 1;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Delta[Axis]) < 1.e-8)
			{
				if (FMath::Abs(A[Axis]) > Extent[Axis]) return false;
				continue;
			}
			const double T0 = (-Extent[Axis] - A[Axis]) / Delta[Axis], T1 = (Extent[Axis] - A[Axis]) / Delta[Axis];
			Min = FMath::Max(Min, FMath::Min(T0, T1));
			Max = FMath::Min(Max, FMath::Max(T0, T1));
		}
		if (Max <= Min) return false;
		B = A + Delta * Max; A += Delta * Min;
		return FVector::DistSquared(A, B) > 0.01;
	}

	// Split the edge only where the first surface under its diagonal sweep can change.
	// Testing the two ends of a whole tile rejects valid middle sections on tilted tiles.
	bool FindLandingCuts(const FDynamicMesh3& Surface, const FDynamicMeshAABBTree3& Tree,
		const FVector& A, const FVector& B, const FVector& Direction, double Radius,
		TArray<double>& Cuts, FProgressCancel* Progress)
	{
		const FVector Edge = B - A;
		const FVector PlaneNormal = FVector::CrossProduct(Edge, Direction).GetSafeNormal();
		const double EdgeSquared = Edge.SquaredLength(), Along = FVector::DotProduct(Edge, Direction);
		const double Determinant = EdgeSquared - Along * Along;
		if (Determinant < 1.e-8) return true;
		FAxisAlignedBox3d Bounds(A, A);
		Bounds.Contain(B); Bounds.Contain(A + Direction * Radius); Bounds.Contain(B + Direction * Radius);
		Cuts = {0, 1};
		FDynamicMeshAABBTree3::FTreeTraversal Traversal;
		Traversal.NextBoxF = [&](const FAxisAlignedBox3d& Box, int32)
		{ return Cuts.Num() <= 2048 && !(Progress && Progress->Cancelled()) && Box.Intersects(Bounds); };
		Traversal.NextTriangleF = [&](int32 Triangle)
		{
			FVector P[3]; Surface.GetTriVertices(Triangle, P[0], P[1], P[2]);
			double Side[3];
			for (int32 K = 0; K < 3; ++K) Side[K] = FVector::DotProduct(P[K] - A, PlaneNormal);
			TArray<FVector, TInlineAllocator<3>> Crossings;
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 Next = (K + 1) % 3;
				if (FMath::Abs(Side[K]) < 1.e-8) Crossings.Add(P[K]);
				if ((Side[K] < -1.e-8 && Side[Next] > 1.e-8) || (Side[K] > 1.e-8 && Side[Next] < -1.e-8))
					Crossings.Add(FMath::Lerp(P[K], P[Next], Side[K] / (Side[K] - Side[Next])));
			}
			if (Crossings.Num() != 2) return;
			FVector2D Ends[2];
			for (int32 K = 0; K < 2; ++K)
			{
				const FVector Delta = Crossings[K] - A;
				const double E = FVector::DotProduct(Delta, Edge), D = FVector::DotProduct(Delta, Direction);
				Ends[K] = FVector2D((E - Along * D) / Determinant, (EdgeSquared * D - Along * E) / Determinant);
			}
			// Clip in edge parameter / ray distance. Back faces still contribute obstruction boundaries.
			double Low = 0, High = 1;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const double Min = Axis == 0 ? 0 : ContactTolerance, Max = Axis == 0 ? 1 : Radius;
				const double Delta = Ends[1][Axis] - Ends[0][Axis];
				if (FMath::Abs(Delta) < 1.e-8)
				{ if (Ends[0][Axis] < Min || Ends[0][Axis] > Max) return; }
				else
				{
					const double T0 = (Min - Ends[0][Axis]) / Delta, T1 = (Max - Ends[0][Axis]) / Delta;
					Low = FMath::Max(Low, FMath::Min(T0, T1)); High = FMath::Min(High, FMath::Max(T0, T1));
				}
			}
			if (High <= Low) return;
			Cuts.Add(FMath::Lerp(Ends[0].X, Ends[1].X, Low));
			Cuts.Add(FMath::Lerp(Ends[0].X, Ends[1].X, High));
		};
		Tree.DoTraversal(Traversal);
		Cuts.Sort();
		return Cuts.Num() <= 2048 && !(Progress && Progress->Cancelled());
	}

	FDynamicMesh3 MakeWedge(const FVector& A, const FVector& B, const FVector& EndA, const FVector& EndB, const FVector& LandingNormal, bool bOverlapLanding)
	{
		FDynamicMesh3 Wedge;
		FVector Points[] = {A, B, EndA, EndB, FVector(A.X, A.Y, EndA.Z), FVector(B.X, B.Y, EndB.Z)};
		if (bOverlapLanding)
		{
			// Alternate operand for ambiguous edge-only contacts. The visible ramp plane is unchanged.
			Points[2] += (EndA-A).GetSafeNormal()*ContactTolerance;
			Points[3] += (EndB-B).GetSafeNormal()*ContactTolerance;
			for (int32 K : {4,5})
				Points[K].Z = EndA.Z - (LandingNormal.X*(Points[K].X-EndA.X) + LandingNormal.Y*(Points[K].Y-EndA.Y))/LandingNormal.Z - ContactTolerance;
		}
		FVector Center = FVector::ZeroVector;
		for (const FVector& P : Points) { Wedge.AppendVertex(P); Center += P / 6; }
		const FIndex3i Faces[] = {{0,1,3},{0,3,2},{4,2,3},{4,3,5},{0,4,5},{0,5,1},{0,2,4},{1,5,3}};
		for (FIndex3i Face : Faces)
		{
			const FVector Normal = FVector::CrossProduct(Points[Face.B] - Points[Face.A], Points[Face.C] - Points[Face.A]);
			if (FVector::DotProduct(Normal, Points[Face.A] - Center) > 0) Swap(Face.B, Face.C);
			Wedge.AppendTriangle(Face);
		}
		return Wedge;
	}
}

bool FMD_StainDownhillBridge::Gather(const FDynamicMesh3& Surface, const FDynamicMesh3& LandingSurface, const FVector2D& HalfSize,
	TArray<FDynamicMesh3>& Wedges, FString& Error, FProgressCancel* Progress, bool bOverlapLandings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_StainSticker_DownhillWorker);
	const double Radius = HalfSize.Length() + 1;
	TUniquePtr<FDynamicMeshAABBTree3> Tree;
	for (int32 Edge : Surface.EdgeIndicesItr())
	{
		if (Progress && Progress->Cancelled()) return false;
		const FIndex2i Triangles = Surface.GetEdgeT(Edge);
		if (Triangles.A < 0 || Triangles.B < 0) continue;
		int32 Top = Triangles.A, Side = Triangles.B;
		if (Surface.GetTriNormal(Top).Z < Surface.GetTriNormal(Side).Z) Swap(Top, Side);
		const FVector TopNormal = Surface.GetTriNormal(Top), SideNormal = Surface.GetTriNormal(Side);
		if (TopNormal.Z < 1 - 1.e-6 || FMath::Abs(SideNormal.Z) > 0.2) continue;
		FVector A, B; Surface.GetEdgeV(Edge, A, B);
		// Bridge only a departure from the clicked face. Other folded faces retain their authored topology.
		if (FMath::Max(FMath::Abs(A.Z), FMath::Abs(B.Z)) > ContactTolerance
			|| !ClipEdge(A, B, HalfSize) || FMath::ClosestPointOnSegment(FVector::ZeroVector, A, B).Length() >= Radius) continue;
		const FIndex3i SideVertices = Surface.GetTriangle(Side);
		double Lowest = TNumericLimits<double>::Max();
		for (int32 K = 0; K < 3; ++K) Lowest = FMath::Min(Lowest, Surface.GetVertex(SideVertices[K]).Z);
		if (Lowest >= FMath::Min(A.Z, B.Z) - ContactTolerance) continue;
		if (!Tree) Tree = MakeUnique<FDynamicMeshAABBTree3>(&LandingSurface);
		TArray<FDynamicMesh3> BestWedges;
		double BestScore = 0;
		// A shallow drop across a gap can hit the next tile's side at 45 degrees.
		// Choose one coherent slope for this edge, favoring the steepest equally supported descent.
		for (double Slope : {1., 0.5, 0.25})
		{
			TArray<FDynamicMesh3> CandidateWedges;
			double Score = 0;
			const FVector Direction = (FVector(SideNormal.X, SideNormal.Y, 0).GetSafeNormal() - FVector::UpVector * Slope).GetSafeNormal();
			TArray<double> Cuts;
			if (!FindLandingCuts(LandingSurface, *Tree, A, B, Direction, Radius, Cuts, Progress))
			{ Error = TEXT("Descending sticker surface exceeds its intersection budget. Reduce the sticker size."); return false; }
			FVector RunA, RunB, LandingA, LandingB, RunNormal;
			bool bRun = false;
			auto Flush = [&]()
			{
				if (!bRun) return true;
				bRun = false;
				if (CandidateWedges.Num() >= MaximumBridges)
				{ Error = TEXT("More than 32 descending steps in this sticker. Reduce the sticker size."); return false; }
				CandidateWedges.Add(MakeWedge(RunA, RunB, LandingA, LandingB, RunNormal, bOverlapLandings));
				return true;
			};
			for (int32 Index = 1; Index < Cuts.Num(); ++Index)
			{
				if (Progress && Progress->Cancelled()) return false;
				if (Cuts[Index] - Cuts[Index - 1] < 1.e-7) continue;
				const FVector StartA = FMath::Lerp(A, B, Cuts[Index - 1]), StartB = FMath::Lerp(A, B, Cuts[Index]);
				const FRay3d Ray((StartA + StartB) * 0.5 + Direction * ContactTolerance, Direction);
				double Distance; int32 Hit;
				if (!Tree->FindNearestHitTriangle(Ray, Distance, Hit, IMeshSpatial::FQueryOptions(Radius))
					|| LandingSurface.GetTriNormal(Hit).Z < 0.9)
				{ if (!Flush()) return false; continue; }
				const FVector Normal = LandingSurface.GetTriNormal(Hit), Point = LandingSurface.GetTriCentroid(Hit);
				const double Denominator = FVector::DotProduct(Normal, Direction);
				if (Denominator >= -1.e-8) { if (!Flush()) return false; continue; }
				const double DA = FVector::DotProduct(Point - StartA, Normal) / Denominator;
				const double DB = FVector::DotProduct(Point - StartB, Normal) / Denominator;
				if (FMath::Min(DA, DB) < ContactTolerance || FMath::Max(DA, DB) > Radius)
				{ if (!Flush()) return false; continue; }
				const FVector EndA = StartA + Direction * DA, EndB = StartB + Direction * DB;
				Score += Cuts[Index] - Cuts[Index - 1];
				// Adjacent triangles on the same landing form one wedge, not one Boolean per triangle.
				if (bRun && RunB.Equals(StartA, ContactTolerance) && LandingB.Equals(EndA, ContactTolerance)
					&& FVector::DotProduct(RunNormal, Normal) > 1 - 1.e-8)
				{ RunB = StartB; LandingB = EndB; }
				else
				{
					if (!Flush()) return false;
					RunA = StartA; RunB = StartB; LandingA = EndA; LandingB = EndB; RunNormal = Normal; bRun = true;
				}
			}
			if (!Flush()) return false;
			if (Score > BestScore + 1.e-6) { BestScore = Score; BestWedges = MoveTemp(CandidateWedges); }
			if (BestScore >= 1 - 1.e-6) break;
		}
		if (Wedges.Num() + BestWedges.Num() > MaximumBridges)
		{ Error = TEXT("More than 32 descending steps in this sticker. Reduce the sticker size."); return false; }
		Wedges.Append(MoveTemp(BestWedges));
	}
	return true;
}
#endif
