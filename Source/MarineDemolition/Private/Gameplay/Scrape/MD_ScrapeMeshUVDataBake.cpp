#include "Gameplay/Scrape/MD_ScrapeMeshUVData.h"

#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "Gameplay/Scrape/MD_ScrapeExposedSurface.h"
#include "Gameplay/Scrape/MD_ScrapeMeshUVAdjacency.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FTriangleTile
	{
		int32 TriangleIndex = INDEX_NONE;
		FIntPoint Size = FIntPoint::ZeroValue;
		FIntPoint Origin = FIntPoint::ZeroValue;
		FVector2D PixelUV[3];
	};

	bool PackTiles(TArray<FTriangleTile>& Tiles, int32 MaximumAxis, FIntPoint& OutSize)
	{
		Tiles.Sort([](const FTriangleTile& A, const FTriangleTile& B)
		{
			if (A.Size.Y != B.Size.Y) return A.Size.Y > B.Size.Y;
			if (A.Size.X != B.Size.X) return A.Size.X > B.Size.X;
			return A.TriangleIndex < B.TriangleIndex;
		});
		int32 MinimumWidth = 32;
		for (const FTriangleTile& Tile : Tiles) MinimumWidth = FMath::Max(MinimumWidth, Tile.Size.X);
		int64 BestArea = MAX_int64;
		TArray<FIntPoint> BestOrigins;
		OutSize = FIntPoint::ZeroValue;
		for (int32 Width = FMath::RoundUpToPowerOfTwo(MinimumWidth); Width <= MaximumAxis; Width *= 2)
		{
			int32 X = 0, Y = 1, RowHeight = 0; // Row zero stays transparent for excluded caps.
			TArray<FIntPoint> Origins;
			bool bFits = true;
			for (const FTriangleTile& Tile : Tiles)
			{
				if (X + Tile.Size.X > Width)
				{
					X = 0;
					Y += RowHeight;
					RowHeight = 0;
				}
				if (Tile.Size.X > Width || Y + Tile.Size.Y > MaximumAxis)
				{
					bFits = false;
					break;
				}
				Origins.Add(FIntPoint(X, Y));
				X += Tile.Size.X;
				RowHeight = FMath::Max(RowHeight, Tile.Size.Y);
			}
			const int32 Height = Y + RowHeight;
			const int64 Area = static_cast<int64>(Width) * Height;
			if (bFits && Area < BestArea)
			{
				BestArea = Area;
				BestOrigins = MoveTemp(Origins);
				OutSize = FIntPoint(Width, Height);
			}
		}
		if (BestOrigins.Num() != Tiles.Num() || BestOrigins.IsEmpty()) return false;
		for (int32 Index = 0; Index < Tiles.Num(); ++Index) Tiles[Index].Origin = BestOrigins[Index];
		return true;
	}

	bool MeasureBarycentricCell(const FMD_ScrapeMeshUVTriangle& Triangle,
		const TArray<FVector>& Polygon, FMD_ScrapeMeshUVCell& Cell)
	{
		Cell.AreaCm2 = 0.0;
		Cell.Barycentric = FVector::ZeroVector;
		Cell.PositionLocal = FVector::ZeroVector;
		Cell.SupportRadiusCm = 0.0;
		if (Polygon.Num() < 3) return false;
		double AreaRatio = 0;
		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
		{
			const FVector E0 = Polygon[Index] - Polygon[0], E1 = Polygon[Index + 1] - Polygon[0];
			// The whole source simplex has twice-area 1 in barycentric (B,C) space.
			const double Ratio = FMath::Abs(E0.Y * E1.Z - E0.Z * E1.Y);
			AreaRatio += Ratio;
			Cell.Barycentric += (Polygon[0] + Polygon[Index] + Polygon[Index + 1]) * (Ratio / 3.0);
		}
		if (!FMath::IsFinite(AreaRatio) || AreaRatio <= 0) return false;
		Cell.Barycentric /= AreaRatio;
		const FVector EdgeB = Triangle.B - Triangle.A, EdgeC = Triangle.C - Triangle.A;
		const double TriangleArea = FVector::CrossProduct(EdgeB, EdgeC).Length() * .5;
		Cell.AreaCm2 = AreaRatio * TriangleArea;
		Cell.PositionLocal = Triangle.A + EdgeB * Cell.Barycentric.Y + EdgeC * Cell.Barycentric.Z;
		for (const FVector& Point : Polygon)
		{
			const FVector Delta = EdgeB * (Point.Y - Cell.Barycentric.Y) + EdgeC * (Point.Z - Cell.Barycentric.Z);
			Cell.SupportRadiusCm = FMath::Max(Cell.SupportRadiusCm, Delta.Length());
		}
		return FMath::IsFinite(Cell.AreaCm2) && Cell.AreaCm2 > 0
			&& !Cell.Barycentric.ContainsNaN() && !Cell.PositionLocal.ContainsNaN() && FMath::IsFinite(Cell.SupportRadiusCm);
	}

	FGuid CalculateLayoutId(const UMD_ScrapeMeshUVData& Data)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		int32 Version = 2;
		Writer << Version;
		FIntPoint Dimensions = Data.AtlasDimensions;
		Writer << Dimensions;
		for (const FMD_ScrapeMeshUVTriangle& T : Data.Triangles)
		{
			FVector A = T.A, B = T.B, C = T.C;
			FVector2D UVA = T.UVA, UVB = T.UVB, UVC = T.UVC;
			bool bCleanable = T.bCleanable;
			Writer << A << B << C << UVA << UVB << UVC << bCleanable;
			int32 NeighborCount = T.Neighbors.Num();
			Writer << NeighborCount;
			for (const FMD_ScrapeMeshUVNeighbor& Neighbor : T.Neighbors)
			{
				int32 TriangleIndex = Neighbor.TriangleIndex;
				FVector EdgeA = Neighbor.SharedEdgeA, EdgeB = Neighbor.SharedEdgeB;
				Writer << TriangleIndex << EdgeA << EdgeB;
			}
		}
		FMD5 MD5;
		MD5.Update(Bytes.GetData(), Bytes.Num());
		uint8 Digest[16];
		MD5.Final(Digest);
		auto Word = [&Digest](int32 Offset)
		{
			return (static_cast<uint32>(Digest[Offset]) << 24)
				| (static_cast<uint32>(Digest[Offset + 1]) << 16)
				| (static_cast<uint32>(Digest[Offset + 2]) << 8)
				| static_cast<uint32>(Digest[Offset + 3]);
		};
		FGuid Result(Word(0), Word(4), Word(8), Word(12));
		if (!Result.IsValid()) Result.D = 1;
		return Result;
	}
}
#endif

void UMD_ScrapeMeshUVData::BakeSourceMesh()
{
#if WITH_EDITOR
	auto Fail = [this](const FString& Error)
	{
		LastBakeReport = TEXT("Bake failed; previous baked data preserved. ") + Error;
		UE_LOG(LogTemp, Error, TEXT("%s: %s"), *GetPathName(), *LastBakeReport);
	};
	if (!SourceMesh || !FMath::IsFinite(TargetCellSizeCm) || TargetCellSizeCm < 0.1
		|| MaximumAtlasAxis < 32 || MaximumAtlasAxis > 1024
		|| !FMath::IsPowerOfTwo(MaximumAtlasAxis) || GutterCells < 1 || GutterCells > 4
		|| !FMath::IsFinite(CapNormalThreshold) || CapNormalThreshold < 0.99 || CapNormalThreshold > 1.0
		|| !FMath::IsFinite(CapHeightToleranceCm) || CapHeightToleranceCm < 0.0
		|| !FMath::IsFinite(AdjacencyWeldToleranceCm) || AdjacencyWeldToleranceCm < 0.0 || AdjacencyWeldToleranceCm > 0.01)
	{
		Fail(TEXT("Select a source mesh and finite valid bake settings (atlas maximum is a power of two)."));
		return;
	}
	const FMeshDescription* SourceDescription = SourceMesh->GetMeshDescription(0);
	if (!SourceDescription || SourceDescription->Triangles().Num() == 0)
	{
		Fail(TEXT("The source LOD0 MeshDescription is unavailable. Fallback render/collision data is not used as a substitute."));
		return;
	}
	if (SourceMesh->GetNumSourceModels() > 0
		&& !SourceMesh->GetSourceModel(0).BuildSettings.BuildScale3D.Equals(FVector::OneVector, 1.0e-8))
	{
		Fail(TEXT("This bake requires identity source BuildScale3D; component/world scale remains supported at runtime."));
		return;
	}
	// Calculate smooth normals before dedicated UV seams split vertex instances.
	FMeshDescription PreparedSource = *SourceDescription;
	EComputeNTBsFlags NormalFlags = EComputeNTBsFlags::None;
	if (SourceMesh->GetNumSourceModels() > 0)
	{
		const FMeshBuildSettings& Settings = SourceMesh->GetSourceModel(0).BuildSettings;
		if (Settings.bRecomputeNormals) NormalFlags |= EComputeNTBsFlags::Normals;
		if (Settings.bRecomputeTangents) NormalFlags |= EComputeNTBsFlags::Tangents;
		if (Settings.bUseMikkTSpace) NormalFlags |= EComputeNTBsFlags::UseMikkTSpace;
		if (Settings.bComputeWeightedNormals) NormalFlags |= EComputeNTBsFlags::WeightedNTBs;
	}
	FStaticMeshOperations::ComputeTriangleTangentsAndNormals(PreparedSource);
	FStaticMeshOperations::ComputeTangentsAndNormals(PreparedSource, NormalFlags);
	FMeshDescription ExposedSource;
	FMD_ScrapeExposedSurfaceResult Exposure;
	const FMeshDescription* Source = &PreparedSource;
	if (bOnlyExposedSurfaces)
	{
		FMD_ScrapeExposedSurfaceSettings ExposureSettings;
		ExposureSettings.IntersectionToleranceCm = IntersectionToleranceCm;
		ExposureSettings.ClassificationOffsetCm = ExposureClassificationOffsetCm;
		ExposureSettings.ContactGapToleranceCm = ContactGapToleranceCm;
		ExposureSettings.CoplanarToleranceCm = CoplanarToleranceCm;
		FString ExposureError;
		if (!FMD_ScrapeExposedSurface::Build(PreparedSource, ExposureSettings, ExposedSource, Exposure, ExposureError))
		{
			Fail(ExposureError); return;
		}
		Source = &ExposedSource;
	}
	FStaticMeshConstAttributes SourceAttributes(*Source);
	const auto Positions = SourceAttributes.GetVertexPositions();
	const auto SourceUVs = SourceAttributes.GetVertexInstanceUVs();
	const int32 SourceUVChannels = SourceUVs.GetNumChannels();
	const int32 NewUVChannel = FMath::Max(1, SourceUVChannels);
	if (NewUVChannel > 7)
	{
		Fail(TEXT("No spare UV channel: existing material UV channels are never overwritten."));
		return;
	}
	TStrongObjectPtr<UMD_ScrapeMeshUVData> WorkingGuard(NewObject<UMD_ScrapeMeshUVData>(GetTransientPackage()));
	UMD_ScrapeMeshUVData* Working = WorkingGuard.Get();
	Working->SourceMesh = SourceMesh;
	Working->SourceMeshLightingGuid = SourceMesh->GetLightingGuid();
	Working->CanonicalUVChannel = NewUVChannel;
	Working->OriginalTriangleCount = SourceDescription->Triangles().Num();
	Working->InteriorExcludedAreaCm2 = Exposure.InteriorAreaCm2;
	Working->ContactExcludedAreaCm2 = Exposure.NearContactAreaCm2;
	Working->DuplicateExcludedAreaCm2 = Exposure.DuplicateAreaCm2;
	Working->ExposureDiagnostics = bOnlyExposedSurfaces ? Exposure.Diagnostics : TEXT("Exposure preprocessing explicitly disabled.");
	double MinimumZ = TNumericLimits<double>::Max();
	double MaximumZ = TNumericLimits<double>::Lowest();
	for (FVertexID Id : Source->Vertices().GetElementIDs())
	{
		const FVector P(Positions[Id]);
		if (P.ContainsNaN()) { Fail(TEXT("Source mesh has non-finite positions.")); return; }
		MinimumZ = FMath::Min(MinimumZ, P.Z);
		MaximumZ = FMath::Max(MaximumZ, P.Z);
	}
	TArray<FTriangleTile> Tiles;
	TArray<FTriangleID> SourceTriangleIds;
	const auto MaterialSlotNames = SourceAttributes.GetPolygonGroupMaterialSlotNames();
	for (FTriangleID Id : Source->Triangles().GetElementIDs())
	{
		const TArrayView<const FVertexInstanceID> Instances = Source->GetTriangleVertexInstances(Id);
		FMD_ScrapeMeshUVTriangle T;
		T.SourceTriangleId = Id.GetValue();
		if (bOnlyExposedSurfaces)
		{
			const FMD_ScrapeSourceTriangleRef* Ref = Exposure.TriangleSourceRefs.Find(Id.GetValue());
			if (!Ref) { Fail(TEXT("Exposed fragment is missing its original source reference.")); return; }
			T.SourceTriangleId = Ref->SourceTriangleId;
			T.SourceBarycentricA = Ref->BarycentricA;
			T.SourceBarycentricB = Ref->BarycentricB;
			T.SourceBarycentricC = Ref->BarycentricC;
			T.ExposureExclusionReason = static_cast<uint8>(Ref->ExclusionReason);
		}
		T.A = FVector(Positions[Source->GetVertexInstanceVertex(Instances[0])]);
		T.B = FVector(Positions[Source->GetVertexInstanceVertex(Instances[1])]);
		T.C = FVector(Positions[Source->GetVertexInstanceVertex(Instances[2])]);
		const FVector Cross = FVector::CrossProduct(T.B - T.A, T.C - T.A);
		const double DoubleArea = Cross.Length();
		if (!FMath::IsFinite(DoubleArea) || DoubleArea <= 0.0)
		{
			Fail(FString::Printf(TEXT("Fragment %d (original source %d) has non-positive stored area; refusing to change geometry silently."), Id.GetValue(), T.SourceTriangleId));
			return;
		}
		// Unreal source triangles use clockwise front winding.
		T.NormalLocal = -Cross / DoubleArea;
		const FPolygonGroupID Group = Source->GetTrianglePolygonGroup(Id);
		T.MaterialIndex = SourceMesh->GetMaterialIndex(MaterialSlotNames[Group]);
		if (!SourceMesh->GetStaticMaterials().IsValidIndex(T.MaterialIndex))
		{
			Fail(FString::Printf(TEXT("Source polygon group %d has no matching material slot."), Group.GetValue()));
			return;
		}
		const double TriangleMinZ = FMath::Min3(T.A.Z, T.B.Z, T.C.Z);
		const double TriangleMaxZ = FMath::Max3(T.A.Z, T.B.Z, T.C.Z);
		const bool bTopCap = T.NormalLocal.Z >= CapNormalThreshold
			&& TriangleMinZ >= MaximumZ - CapHeightToleranceCm;
		const bool bBottomCap = T.NormalLocal.Z <= -CapNormalThreshold
			&& TriangleMaxZ <= MinimumZ + CapHeightToleranceCm;
		T.bCleanable = T.ExposureExclusionReason == 0
			&& !(bExcludeTopAndBottomCaps && (bTopCap || bBottomCap));
		const int32 Index = Working->Triangles.Add(T);
		SourceTriangleIds.Add(Id);
		if (!T.bCleanable) continue;
		Working->CleanableAreaCm2 += DoubleArea * 0.5;
		const FVector P[3] = { T.A, T.B, T.C };
		int32 LongestEdge = 0;
		for (int32 Edge = 1; Edge < 3; ++Edge)
		{
			if (FVector::DistSquared(P[Edge], P[(Edge + 1) % 3])
				> FVector::DistSquared(P[LongestEdge], P[(LongestEdge + 1) % 3])) LongestEdge = Edge;
		}
		const FVector Origin = P[LongestEdge];
		const double LongestLength = FVector::Distance(P[(LongestEdge + 1) % 3], Origin);
		const FVector AxisU = (P[(LongestEdge + 1) % 3] - Origin) / LongestLength;
		FTriangleTile Tile;
		Tile.TriangleIndex = Index;
		FVector2D Minimum(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Maximum(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			// Explicit base/altitude avoids subtractive cancellation of a very narrow
			// triangle's third corner against an almost-parallel projection axis.
			if (Corner == LongestEdge) Tile.PixelUV[Corner] = FVector2D::ZeroVector;
			else if (Corner == (LongestEdge + 1) % 3) Tile.PixelUV[Corner] = FVector2D(LongestLength / TargetCellSizeCm, 0);
			else Tile.PixelUV[Corner] = FVector2D(FVector::DotProduct(P[Corner] - Origin, AxisU),
				DoubleArea / LongestLength) / TargetCellSizeCm;
			Minimum.X = FMath::Min(Minimum.X, Tile.PixelUV[Corner].X);
			Minimum.Y = FMath::Min(Minimum.Y, Tile.PixelUV[Corner].Y);
			Maximum.X = FMath::Max(Maximum.X, Tile.PixelUV[Corner].X);
			Maximum.Y = FMath::Max(Maximum.Y, Tile.PixelUV[Corner].Y);
		}
		const FVector2D Extent = Maximum - Minimum;
		if (Extent.X > MaximumAtlasAxis - GutterCells * 2 || Extent.Y > MaximumAtlasAxis - GutterCells * 2)
		{
			Fail(TEXT("A triangle exceeds the atlas dimension at the requested cm/cell. Change an explicit bake setting; no automatic loss of resolution was applied."));
			return;
		}
		Tile.Size = FIntPoint(FMath::Max(1, FMath::CeilToInt(Extent.X)) + GutterCells * 2,
			FMath::Max(1, FMath::CeilToInt(Extent.Y)) + GutterCells * 2);
		for (FVector2D& UV : Tile.PixelUV)
		{
			UV -= Minimum;
			// Each chart axis spans at least one texel (finer density, never coarser), so float32 UVs
			// cannot collapse their chart when packed near the far end of a 1024px atlas.
			if (Extent.X > 0.0 && Extent.X < 1.0) UV.X *= 1.0 / Extent.X;
			if (Extent.Y > 0.0 && Extent.Y < 1.0) UV.Y *= 1.0 / Extent.Y;
		}
		Tiles.Add(Tile);
	}
	if (!PackTiles(Tiles, MaximumAtlasAxis, Working->AtlasDimensions))
	{
		Fail(FString::Printf(TEXT("%d isolated triangle charts do not fit within %dx%d at %.3f cm/cell with %d-cell gutters. No automatic coarsening was performed."),
			Tiles.Num(), MaximumAtlasAxis, MaximumAtlasAxis, TargetCellSizeCm, GutterCells));
		return;
	}
	Working->Cells.SetNum(Working->AtlasDimensions.X * Working->AtlasDimensions.Y);
	const FVector2D Divisor(Working->AtlasDimensions.X, Working->AtlasDimensions.Y);
	const FVector2D TransparentUV(FVector2f(0.5 / Divisor.X, 0.5 / Divisor.Y));
	for (FMD_ScrapeMeshUVTriangle& T : Working->Triangles)
	{
		T.UVA = T.UVB = T.UVC = TransparentUV;
	}
	for (const FTriangleTile& Tile : Tiles)
	{
		FMD_ScrapeMeshUVTriangle& T = Working->Triangles[Tile.TriangleIndex];
		const FVector2D PixelOrigin(Tile.Origin.X + GutterCells, Tile.Origin.Y + GutterCells);
		T.UVA = FVector2D(FVector2f((Tile.PixelUV[0] + PixelOrigin) / Divisor));
		T.UVB = FVector2D(FVector2f((Tile.PixelUV[1] + PixelOrigin) / Divisor));
		T.UVC = FVector2D(FVector2f((Tile.PixelUV[2] + PixelOrigin) / Divisor));
		double TriangleCellArea = 0.0;
		for (int32 Y = Tile.Origin.Y; Y < Tile.Origin.Y + Tile.Size.Y; ++Y)
		{
			for (int32 X = Tile.Origin.X; X < Tile.Origin.X + Tile.Size.X; ++X)
			{
				const int32 CellIndex = Y * Working->AtlasDimensions.X + X;
				FMD_ScrapeMeshUVCell& Cell = Working->Cells[CellIndex];
				if (Cell.TriangleIndex != INDEX_NONE) { Fail(TEXT("Atlas cell ownership collision.")); return; }
				Cell.TriangleIndex = Tile.TriangleIndex;
				TArray<FVector> Polygon;
				if (!FMD_ScrapeMeshUVGeometry::GetCellPolygonBarycentric(*Working, CellIndex, Polygon)
					|| !MeasureBarycentricCell(T, Polygon, Cell))
				{
					Cell = FMD_ScrapeMeshUVCell();
					continue;
				}
				Cell.VisualSourceCellIndex = CellIndex;
				T.CellIndices.Add(CellIndex);
				++Working->ValidCellCount;
				TriangleCellArea += Cell.AreaCm2;
			}
		}
		const double TriangleArea = FVector::CrossProduct(T.B - T.A, T.C - T.A).Length() * 0.5;
		if (T.CellIndices.IsEmpty() || FMath::Abs(TriangleCellArea - TriangleArea) > FMath::Max(1.0e-6, TriangleArea * 1.0e-8))
		{
			Fail(FString::Printf(TEXT("Triangle %d is not completely represented by its clipped atlas cells."), T.SourceTriangleId)); return;
		}
		// Dilate only inside this triangle's private rectangle. These pixels never become gameplay cells.
		for (int32 ActiveIndex : T.CellIndices)
		{
			const int32 AX = ActiveIndex % Working->AtlasDimensions.X, AY = ActiveIndex / Working->AtlasDimensions.X;
			for (int32 DY = -GutterCells; DY <= GutterCells; ++DY)
			{
				for (int32 DX = -GutterCells; DX <= GutterCells; ++DX)
				{
					const int32 X = AX + DX, Y = AY + DY;
					if (X < Tile.Origin.X || Y < Tile.Origin.Y
						|| X >= Tile.Origin.X + Tile.Size.X || Y >= Tile.Origin.Y + Tile.Size.Y) continue;
					FMD_ScrapeMeshUVCell& Pixel = Working->Cells[Y * Working->AtlasDimensions.X + X];
					if (Pixel.IsActive()) continue;
					const int32 Previous = Pixel.VisualSourceCellIndex;
					const int32 PreviousDistance = Previous == INDEX_NONE ? MAX_int32
						: FMath::Square(Previous % Working->AtlasDimensions.X - X)
							+ FMath::Square(Previous / Working->AtlasDimensions.X - Y);
					if (DX * DX + DY * DY < PreviousDistance
						|| (DX * DX + DY * DY == PreviousDistance && ActiveIndex < Previous)) Pixel.VisualSourceCellIndex = ActiveIndex;
				}
			}
		}
	}
	FString AdjacencyError;
	if (!FMD_ScrapeMeshUVAdjacency::Build(Working->Triangles, AdjacencyWeldToleranceCm,
		Working->ConnectedEdgeCount, AdjacencyError)) { Fail(AdjacencyError); return; }
	Working->LayoutId = CalculateLayoutId(*Working);

	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	auto NewPositions = Attributes.GetVertexPositions();
	auto NewUVs = Attributes.GetVertexInstanceUVs();
	NewUVs.SetNumChannels(NewUVChannel + 1);
	auto NewNormals = Attributes.GetVertexInstanceNormals();
	auto NewTangents = Attributes.GetVertexInstanceTangents();
	auto NewSigns = Attributes.GetVertexInstanceBinormalSigns();
	auto NewColors = Attributes.GetVertexInstanceColors();
	auto NewSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
	TMap<int32, FPolygonGroupID> Groups;
	for (FPolygonGroupID Id : Source->PolygonGroups().GetElementIDs())
	{
		const FPolygonGroupID NewId = Description.CreatePolygonGroup();
		NewSlotNames[NewId] = MaterialSlotNames[Id];
		Groups.Add(Id.GetValue(), NewId);
	}
	for (int32 Index = 0; Index < Working->Triangles.Num(); ++Index)
	{
		const FTriangleID SourceId = SourceTriangleIds[Index];
		const auto Instances = Source->GetTriangleVertexInstances(SourceId);
		const FMD_ScrapeMeshUVTriangle& T = Working->Triangles[Index];
		const FVector Points[3] = { T.A, T.B, T.C };
		const FVector2D UVs[3] = { T.UVA, T.UVB, T.UVC };
		TArray<FVertexInstanceID> NewInstances;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVertexID Vertex = Description.CreateVertex();
			NewPositions[Vertex] = FVector3f(Points[Corner]);
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			NewInstances.Add(Instance);
			NewNormals[Instance] = SourceAttributes.GetVertexInstanceNormals()[Instances[Corner]];
			NewTangents[Instance] = SourceAttributes.GetVertexInstanceTangents()[Instances[Corner]];
			NewSigns[Instance] = SourceAttributes.GetVertexInstanceBinormalSigns()[Instances[Corner]];
			NewColors[Instance] = SourceAttributes.GetVertexInstanceColors()[Instances[Corner]];
			for (int32 Channel = 0; Channel < SourceUVChannels; ++Channel)
			{
				NewUVs.Set(Instance, Channel, SourceUVs.Get(Instances[Corner], Channel));
			}
			NewUVs.Set(Instance, NewUVChannel, FVector2f(UVs[Corner]));
		}
		Description.CreateTriangle(Groups.FindChecked(Source->GetTrianglePolygonGroup(SourceId).GetValue()), NewInstances);
	}
	// Default subobjects inherit RF_Transient, so create the mesh persistently from the start.
	// It stays private and unreferenced by this asset until the complete bake validates.
	TStrongObjectPtr<UStaticMesh> NewMeshGuard(NewObject<UStaticMesh>(this,
		MakeUniqueObjectName(this, UStaticMesh::StaticClass(), TEXT("BakedCleanableMesh")), RF_Transactional));
	UStaticMesh* NewMesh = NewMeshGuard.Get();
	NewMesh->SetNumSourceModels(1);
	if (SourceMesh->GetNumSourceModels() > 0) NewMesh->GetSourceModel(0).BuildSettings = SourceMesh->GetSourceModel(0).BuildSettings;
	NewMesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs = true;
	NewMesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;
	NewMesh->GetSourceModel(0).BuildSettings.bRemoveDegenerates = false;
	NewMesh->GetSourceModel(0).BuildSettings.bRecomputeNormals = false;
	NewMesh->GetSourceModel(0).BuildSettings.bRecomputeTangents = false;
	NewMesh->GetSourceModel(0).BuildSettings.BuildScale3D = FVector::OneVector;
	NewMesh->GetStaticMaterials() = SourceMesh->GetStaticMaterials();
	NewMesh->GetNaniteSettings().bEnabled = false;
	NewMesh->CreateBodySetup();
	if (const UBodySetup* SourceBody = SourceMesh->GetBodySetup()) NewMesh->GetBodySetup()->AggGeom = SourceBody->AggGeom;
	NewMesh->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	NewMesh->GetBodySetup()->bDoubleSidedGeometry = false;
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bAllowCpuAccess = true;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = false;
	Params.bCommitMeshDescription = true;
	Params.bMarkPackageDirty = false;
	TArray<const FMeshDescription*> Descriptions = { &Description };
	if (!NewMesh->BuildFromMeshDescriptions(Descriptions, Params))
	{
		Fail(TEXT("Unreal could not build the separate full-source mesh.")); return;
	}
	Working->BakedRenderMesh = NewMesh;
	FString ValidationError;
	if (!Working->Validate(&ValidationError)) { Fail(ValidationError); return; }

	Modify();
	NewMesh->SetFlags(RF_Public);
	BakedRenderMesh = NewMesh;
	AtlasDimensions = Working->AtlasDimensions;
	LayoutId = Working->LayoutId;
	SourceMeshLightingGuid = Working->SourceMeshLightingGuid;
	ConnectedEdgeCount = Working->ConnectedEdgeCount;
	ValidCellCount = Working->ValidCellCount;
	CleanableAreaCm2 = Working->CleanableAreaCm2;
	CanonicalUVChannel = Working->CanonicalUVChannel;
	OriginalTriangleCount = Working->OriginalTriangleCount;
	InteriorExcludedAreaCm2 = Working->InteriorExcludedAreaCm2;
	ContactExcludedAreaCm2 = Working->ContactExcludedAreaCm2;
	DuplicateExcludedAreaCm2 = Working->DuplicateExcludedAreaCm2;
	ExposureDiagnostics = MoveTemp(Working->ExposureDiagnostics);
	Triangles = MoveTemp(Working->Triangles);
	Cells = MoveTemp(Working->Cells);
	// An identical layout can still have newly authored gutter data after a rebake.
	InvalidateRuntimeCaches();
	LastBakeReport = FString::Printf(TEXT("Baked %d source triangles as %d geometry fragments, %d cleanable cells, %.3f m2 into %dx%d at %.3f local cm/cell. UV%d is canonical; original UVs/material slots preserved; Nanite off. Layout %s. Source asset unchanged; %d shared edges (weld tolerance %.6f cm)."),
		OriginalTriangleCount, Triangles.Num(), ValidCellCount, CleanableAreaCm2 / 10000.0,
		AtlasDimensions.X, AtlasDimensions.Y, TargetCellSizeCm, CanonicalUVChannel, *LayoutId.ToString(), ConnectedEdgeCount, AdjacencyWeldToleranceCm);
	LastBakeReport += TEXT(" ") + ExposureDiagnostics;
	MarkPackageDirty();
	UE_LOG(LogTemp, Display, TEXT("%s: %s"), *GetPathName(), *LastBakeReport);
#else
	UE_LOG(LogTemp, Warning, TEXT("Scrape mesh UV baking is available only in the editor; packaged builds use the saved profile."));
#endif
}
