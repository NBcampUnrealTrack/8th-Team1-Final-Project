#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "World/Contamination/MD_ContaminationItem.h"

void UMD_SurfaceStainDefinition::PostLoad()
{
	Super::PostLoad();
	RebuildVolumeValidation();
}

void UMD_SurfaceStainDefinition::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	RebuildVolumeValidation();
}

#if WITH_EDITOR
void UMD_SurfaceStainDefinition::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	RebuildVolumeValidation();
}

void UMD_SurfaceStainDefinition::PostEditUndo()
{
	Super::PostEditUndo();
	RebuildVolumeValidation();
}
#endif

bool UMD_SurfaceStainDefinition::RebuildVolumeValidation()
{
	bVolumeGeometryValid = false;
#if WITH_EDITOR
	++AuthoringRevision;
	bEdgePlacementValid = false;
	PlanarPlacementSamples.Reset();
	PlacementMeshGuids.Reset();
	VolumePlacementValidationStatus = TEXT("Volume geometry must validate before automatic placement.");
#endif
	if (VolumeStages.Num() != VolumeStageCount)
	{
		VolumeValidationStatus = TEXT("Assign exactly three closed stages: full, two-thirds, one-third.");
		return false;
	}
	for (int32 StageIndex = 0; StageIndex < VolumeStages.Num(); ++StageIndex)
	{
		FMD_SurfaceStainVolumeStage& Stage = VolumeStages[StageIndex];
		Stage.QueryBounds = FBox(ForceInit);
		auto Reject = [this, StageIndex](const TCHAR* Reason)
		{
			VolumeValidationStatus = FString::Printf(TEXT("Stage %d: %s"), StageIndex, Reason);
			return false;
		};
		if (!IsValid(Stage.Mesh)) return Reject(TEXT("assign a shared mesh."));
		if (Stage.QueryVerticesCm.Num() < 4 || Stage.QueryVerticesCm.Num() > MaximumVolumeQueryVertices
			|| Stage.QueryTriangleIndices.Num() < 12 || Stage.QueryTriangleIndices.Num() > MaximumVolumeQueryTriangles * 3
			|| Stage.QueryTriangleIndices.Num() % 3 != 0)
			return Reject(TEXT("query requires 4-4096 vertices and 4-4096 indexed triangles."));
		for (const FVector& Vertex : Stage.QueryVerticesCm)
		{
			if (!FMath::IsFinite(Vertex.X) || !FMath::IsFinite(Vertex.Y) || !FMath::IsFinite(Vertex.Z))
				return Reject(TEXT("query contains a non-finite vertex."));
			Stage.QueryBounds += Vertex;
		}
		// Only authoring/loading builds this bounded map. Runtime instances share the validated arrays.
		TMap<uint64, FIntPoint> Edges;
		Edges.Reserve(Stage.QueryTriangleIndices.Num());
		double SixTimesVolume = 0.0;
		for (int32 Triangle = 0; Triangle < Stage.QueryTriangleIndices.Num(); Triangle += 3)
		{
			const int32 A = Stage.QueryTriangleIndices[Triangle];
			const int32 B = Stage.QueryTriangleIndices[Triangle + 1];
			const int32 C = Stage.QueryTriangleIndices[Triangle + 2];
			if (!Stage.QueryVerticesCm.IsValidIndex(A) || !Stage.QueryVerticesCm.IsValidIndex(B)
				|| !Stage.QueryVerticesCm.IsValidIndex(C))
				return Reject(TEXT("query triangle index is out of range."));
			const FVector& VA = Stage.QueryVerticesCm[A];
			const FVector& VB = Stage.QueryVerticesCm[B];
			const FVector& VC = Stage.QueryVerticesCm[C];
			const double AreaSquared = FVector::CrossProduct(VB - VA, VC - VA).SquaredLength();
			if (!FMath::IsFinite(AreaSquared) || AreaSquared <= 1.e-12)
				return Reject(TEXT("query contains a degenerate triangle."));
			SixTimesVolume += FVector::DotProduct(VA, FVector::CrossProduct(VB, VC));
			const int32 Indices[] = { A, B, C };
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const int32 From = Indices[Edge], To = Indices[(Edge + 1) % 3];
				const uint64 Key = (uint64(FMath::Min(From, To)) << 32) | uint32(FMath::Max(From, To));
				FIntPoint& Use = Edges.FindOrAdd(Key, FIntPoint::ZeroValue);
				++Use.X;
				Use.Y += From < To ? 1 : -1;
			}
		}
		for (const auto& Edge : Edges)
			if (Edge.Value.X != 2 || Edge.Value.Y != 0)
				return Reject(TEXT("query must be welded, closed, and consistently wound."));
		if (!FMath::IsFinite(SixTimesVolume) || FMath::Abs(SixTimesVolume) <= 1.e-6
			|| !Stage.QueryBounds.IsValid)
			return Reject(TEXT("query must enclose nonzero volume."));
	}
	bVolumeGeometryValid = true;
#if WITH_EDITOR
	RebuildPlanarPlacementCache();
#endif
	VolumeValidationStatus = TEXT("Validated three closed query stages. Mesh/query alignment and contact require visual verification.");
	return true;
}

#if WITH_EDITOR
void UMD_SurfaceStainDefinition::RebuildPlanarPlacementCache()
{
	if (VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport)
	{
		if (!FMath::IsFinite(EdgeStartCm) || EdgeStartCm < 1.0f || !FMath::IsFinite(EdgeRadiusCm)
			|| EdgeRadiusCm < 0.0f || EdgeRadiusCm > 3.0f || EdgeContacts.Num() < 8 || EdgeContacts.Num() > 512
			|| MaximumPlanarInitialSize < FMD_SurfaceStainSize::Minimum || MaximumPlanarInitialSize > 300)
		{ VolumePlacementValidationStatus = TEXT("Edge profiles require finite dimensions, 8-512 contact samples and a maximum size of 100-300."); return; }
		bool bMain = false, bSide = false;
		for (const FMD_SurfaceStainEdgeContact& Contact : EdgeContacts)
		{
			if (Contact.Position.ContainsNaN() || Contact.Normal.ContainsNaN() || !Contact.Normal.IsNormalized()
				|| Contact.Normal.Y < -0.001 || Contact.Normal.Z < -0.001 || FMath::Abs(Contact.Normal.X) > 0.001)
			{ VolumePlacementValidationStatus = TEXT("Edge contact positions/normals are invalid."); return; }
			bMain |= Contact.Normal.Z > 0.999;
			bSide |= Contact.Normal.Y > 0.999;
		}
		if (!bMain || !bSide)
		{ VolumePlacementValidationStatus = TEXT("Edge contact samples must cover both receiving faces."); return; }
		for (const FMD_SurfaceStainVolumeStage& Stage : VolumeStages) PlacementMeshGuids.Add(Stage.Mesh->GetLightingGuid());
		bEdgePlacementValid = true;
		VolumePlacementValidationStatus = FString::Printf(TEXT("Convex right-angle edge: %d cached contacts; radius %.2f cm at size 300. One receiving mesh only."), EdgeContacts.Num(), EdgeRadiusCm);
		return;
	}
	if (VolumePlacementPolicy != EMD_SurfaceStainVolumePlacement::PlanarSupport)
	{
		VolumePlacementValidationStatus = TEXT("Manual placement only. This profile is not approved for automatic planar placement.");
		return;
	}
	if (MaximumPlanarInitialSize < FMD_SurfaceStainSize::Minimum || MaximumPlanarInitialSize > FMD_SurfaceStainSize::Maximum)
	{
		VolumePlacementValidationStatus = TEXT("Maximum planar initial size must be in the existing 100-10000 range.");
		return;
	}
	TArray<FVector2D> Points;
	for (const FMD_SurfaceStainVolumeStage& Stage : VolumeStages)
	{
		// A planar profile has a flat underside within 1 mm of the authored contact anchor.
		if (Stage.QueryBounds.Min.Z < 0.0 || Stage.QueryBounds.Min.Z > 0.1)
		{
			VolumePlacementValidationStatus = TEXT("Planar stages must sit above the contact anchor within 0.1 cm; curved profiles are manual only.");
			return;
		}
		// Importers may reverse every face. Closed-query validation accepts either winding;
		// normalize its signed-volume orientation before deciding which faces point down.
		double SixTimesSignedVolume = 0.0;
		for (int32 T = 0; T < Stage.QueryTriangleIndices.Num(); T += 3)
		{
			const FVector& A = Stage.QueryVerticesCm[Stage.QueryTriangleIndices[T]];
			const FVector& B = Stage.QueryVerticesCm[Stage.QueryTriangleIndices[T + 1]];
			const FVector& C = Stage.QueryVerticesCm[Stage.QueryTriangleIndices[T + 2]];
			SixTimesSignedVolume += FVector::DotProduct(A, FVector::CrossProduct(B, C));
		}
		const double OutwardSign = SixTimesSignedVolume > 0.0 ? 1.0 : -1.0;
		for (int32 T = 0; T < Stage.QueryTriangleIndices.Num(); T += 3)
		{
			const FVector& A = Stage.QueryVerticesCm[Stage.QueryTriangleIndices[T]];
			const FVector& B = Stage.QueryVerticesCm[Stage.QueryTriangleIndices[T + 1]];
			const FVector& C = Stage.QueryVerticesCm[Stage.QueryTriangleIndices[T + 2]];
			if (FVector::CrossProduct(B - A, C - A).GetSafeNormal().Z * OutwardSign < -0.5
				&& FMath::Max3(A.Z, B.Z, C.Z) > 0.1)
			{
				VolumePlacementValidationStatus = TEXT("The downward-facing underside is not planar at the contact anchor.");
				return;
			}
		}
		for (const FVector& P : Stage.QueryVerticesCm) Points.Emplace(P.X, P.Y);
	}
	Points.Sort([](const FVector2D& A, const FVector2D& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	TArray<FVector2D> Unique;
	for (const FVector2D& P : Points)
		if (Unique.IsEmpty() || !P.Equals(Unique.Last(), 1.e-5)) Unique.Add(P);
	if (Unique.Num() < 3)
	{
		VolumePlacementValidationStatus = TEXT("Planar footprint is degenerate.");
		return;
	}
	auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{ return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X); };
	// Convex support intentionally includes concave cut-outs: no unsupported bridge is accepted there.
	TArray<FVector2D> Hull;
	for (const FVector2D& P : Unique)
	{
		while (Hull.Num() >= 2 && Cross(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0) Hull.Pop(EAllowShrinking::No);
		Hull.Add(P);
	}
	const int32 LowerCount = Hull.Num();
	for (int32 I = Unique.Num() - 2; I >= 0; --I)
	{
		while (Hull.Num() > LowerCount && Cross(Hull[Hull.Num() - 2], Hull.Last(), Unique[I]) <= 0.0) Hull.Pop(EAllowShrinking::No);
		Hull.Add(Unique[I]);
	}
	Hull.Pop(EAllowShrinking::No);
	FVector2D Center = FVector2D::ZeroVector;
	for (const FVector2D& P : Hull) Center += P;
	Center /= Hull.Num();
	const double LargestScale = FMD_SurfaceStainSize::LinearScale(MaximumPlanarInitialSize);
	// Cache at the largest allowed size: no supported placement exceeds a 6 cm grid step.
	const double Pitch = 6.0 / LargestScale;
	const double Guard = 0.5 / LargestScale;
	FBox2D Bounds(ForceInit);
	for (FVector2D& P : Hull) { P += (P - Center).GetSafeNormal() * Guard; Bounds += P; }
	const FVector2D Span = Bounds.GetSize();
	const int32 NX = FMath::CeilToInt(Span.X / Pitch), NY = FMath::CeilToInt(Span.Y / Pitch);
	if (NX < 1 || NY < 1 || NX > MaximumPlacementSamples || NY > MaximumPlacementSamples
		|| int64(NX + 1) * (NY + 1) > MaximumPlacementSamples)
	{
		VolumePlacementValidationStatus = TEXT("Footprint is too large for the 256-sample placement budget. Reduce the supported initial size.");
		return;
	}
	TArray<FVector2D> Samples;
	Samples.Add(Center);
	for (int32 Y = 0; Y <= NY; ++Y)
		for (int32 X = 0; X <= NX; ++X)
		{
			const FVector2D P = Bounds.Min + Span * FVector2D(double(X) / NX, double(Y) / NY);
			bool bInside = true;
			for (int32 I = 0; I < Hull.Num(); ++I)
				if (Cross(Hull[I], Hull[(I + 1) % Hull.Num()], P) < -1.e-6) { bInside = false; break; }
			if (bInside) Samples.Add(P);
		}
	for (int32 I = 0; I < Hull.Num(); ++I)
	{
		const FVector2D A = Hull[I], B = Hull[(I + 1) % Hull.Num()];
		const int32 Steps = FMath::Max(1, FMath::CeilToInt((B - A).Length() / Pitch));
		for (int32 J = 0; J < Steps; ++J) Samples.Add(FMath::Lerp(A, B, double(J) / Steps));
	}
	if (Samples.Num() > MaximumPlacementSamples)
	{
		VolumePlacementValidationStatus = TEXT("Footprint boundary and interior exceed the 256-sample budget. Reduce the supported initial size.");
		return;
	}
	PlanarPlacementSamples = MoveTemp(Samples);
	for (const FMD_SurfaceStainVolumeStage& Stage : VolumeStages) PlacementMeshGuids.Add(Stage.Mesh->GetLightingGuid());
	VolumePlacementValidationStatus = FString::Printf(TEXT("Planar support: %d cached samples, 6 cm maximum grid step, up to initial size %d. Gaps between samples are not guaranteed."),
		PlanarPlacementSamples.Num(), MaximumPlanarInitialSize);
}

uint32 UMD_SurfaceStainDefinition::GetAuthoringRevision() const
{
	uint32 Revision = AuthoringRevision;
	if (UsesSharedVolume())
		for (const FMD_SurfaceStainVolumeStage& Stage : VolumeStages)
			Revision = HashCombine(Revision, IsValid(Stage.Mesh) ? GetTypeHash(Stage.Mesh->GetLightingGuid()) : 0);
	return Revision;
}

bool UMD_SurfaceStainDefinition::CanPlaceEdgeVolume(int32 InitialSize, FString& Error) const
{
	if (!UsesSharedVolume() || !IsUsable() || !bEdgePlacementValid
		|| VolumePlacementPolicy != EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport)
	{ Error = VolumePlacementValidationStatus; return false; }
	if (InitialSize < FMD_SurfaceStainSize::Minimum || InitialSize > MaximumPlanarInitialSize)
	{ Error = FString::Printf(TEXT("This edge profile supports initial sizes 100-%d."), MaximumPlanarInitialSize); return false; }
	for (int32 I = 0; I < VolumeStages.Num(); ++I)
		if (!PlacementMeshGuids.IsValidIndex(I) || PlacementMeshGuids[I] != VolumeStages[I].Mesh->GetLightingGuid())
		{ Error = TEXT("The edge mesh changed. Refresh its query/contact data and rebuild the definition."); return false; }
	return true;
}

bool UMD_SurfaceStainDefinition::CanPlacePlanarVolume(int32 InitialSize, FString& Error) const
{
	if (!UsesSharedVolume() || !IsUsable() || VolumePlacementPolicy != EMD_SurfaceStainVolumePlacement::PlanarSupport
		|| PlanarPlacementSamples.IsEmpty())
	{
		Error = VolumePlacementValidationStatus;
		return false;
	}
	for (int32 I = 0; I < VolumeStages.Num(); ++I)
		if (!PlacementMeshGuids.IsValidIndex(I) || PlacementMeshGuids[I] != VolumeStages[I].Mesh->GetLightingGuid())
		{
			Error = TEXT("A shared mesh changed after contact validation. Refresh its query geometry and rebuild the definition.");
			return false;
		}
	if (InitialSize < FMD_SurfaceStainSize::Minimum || InitialSize > MaximumPlanarInitialSize)
	{
		Error = FString::Printf(TEXT("This planar profile supports initial sizes %d-%d. Choose a supported size."),
			FMD_SurfaceStainSize::Minimum, MaximumPlanarInitialSize);
		return false;
	}
	return true;
}
#endif

FVector UMD_SurfaceStainDefinition::GetVolumeScale(int32 InitialSize) const
{
	const double Scale = FMD_SurfaceStainSize::LinearScale(InitialSize);
	// Edge cross-sections must keep their angle/radius when the footprint changes size.
	return FVector(Scale, Scale, VolumePlacementPolicy == EMD_SurfaceStainVolumePlacement::ConvexRightAngleSupport ? Scale : 1.0);
}

int32 UMD_SurfaceStainDefinition::GetVolumeStageIndex(int32 RemainingSize, int32 InitialSize) const
{
	if (!UsesSharedVolume() || !IsUsable() || !FMD_SurfaceStainSize::IsValidRemaining(RemainingSize)
		|| RemainingSize == 0 || InitialSize < FMD_SurfaceStainSize::Minimum || InitialSize > FMD_SurfaceStainSize::Maximum)
		return INDEX_NONE;
	// Three discrete closed shapes deliberately trade continuous area matching for bounded shared resources.
	// Existing saves may contain more than InitialSize; preserve that budget and display the full shape.
	return int64(RemainingSize) * 3 > int64(InitialSize) * 2 ? 0
		: int64(RemainingSize) * 3 > InitialSize ? 1 : 2;
}

const FMD_SurfaceStainVolumeStage* UMD_SurfaceStainDefinition::GetVolumeStage(int32 RemainingSize, int32 InitialSize) const
{
	const int32 Index = GetVolumeStageIndex(RemainingSize, InitialSize);
	return VolumeStages.IsValidIndex(Index) ? &VolumeStages[Index] : nullptr;
}

FVector2D UMD_SurfaceStainDefinition::GetHalfSize(int32 RemainingSize) const
{
	return SizeCm * (0.5f * FMD_SurfaceStainSize::LinearScale(RemainingSize));
}

bool UMD_SurfaceStainDefinition::ContainsUV(const FVector2D& UV) const
{
	if (UV.ContainsNaN() || UV.X < 0 || UV.X > 1 || UV.Y < 0 || UV.Y > 1) return false;
	if (FootprintMask.IsEmpty()) return true; // Legacy/custom definitions use their authored rectangle.
	constexpr int32 Resolution = 64;
	if (FootprintMask.Num() != Resolution * Resolution) return false;
	const int32 X = FMath::Min(FMath::FloorToInt(UV.X * Resolution), Resolution - 1);
	const int32 Y = FMath::Min(FMath::FloorToInt(UV.Y * Resolution), Resolution - 1);
	return FootprintMask[Y * Resolution + X] != 0;
}

bool UMD_SurfaceStainDefinition::IsUsable() const
{
	if (static_cast<uint8>(Kind) > static_cast<uint8>(EMD_SurfaceStainKind::Speckles)
		|| !RemovedItemClass || RemovedItemClass->HasAnyClassFlags(CLASS_Abstract)) return false;
	if (UsesSharedVolume())
	{
		if (!bVolumeGeometryValid || VolumeStages.Num() != VolumeStageCount
			|| !IsValid(VolumeMaterial) || !VolumeMaterial->GetMaterial()
			|| VolumeMaterial->GetMaterial()->MaterialDomain != MD_Surface
			|| VolumeMaterial->GetBlendMode() != BLEND_Opaque) return false;
		for (const FMD_SurfaceStainVolumeStage& Stage : VolumeStages)
			if (!IsValid(Stage.Mesh)) return false;
		return true;
	}
	return Presentation == EMD_SurfaceStainPresentation::SurfaceSticker
		&& IsValid(DecalMaterial) && DecalMaterial->GetMaterial()
		&& DecalMaterial->GetMaterial()->MaterialDomain == MD_DeferredDecal
		&& !SizeCm.ContainsNaN() && SizeCm.X >= 1.0 && SizeCm.Y >= 1.0
		&& FMath::IsFinite(ProjectionDepthCm) && ProjectionDepthCm >= 1.0f && ProjectionDepthCm <= 1500.0f
		&& FMath::IsFinite(MinimumSurfaceCosine) && MinimumSurfaceCosine >= 0.1f && MinimumSurfaceCosine <= 1.0f
		&& (FootprintMask.IsEmpty() || FootprintMask.Num() == 64 * 64);
}
