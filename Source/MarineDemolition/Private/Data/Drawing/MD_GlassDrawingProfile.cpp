#include "Data/Drawing/MD_GlassDrawingProfile.h"

#include "Algo/BinarySearch.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_EDITOR
#include "Interfaces/Interface_CollisionDataProvider.h"
#endif

namespace
{
	bool IsValidInkColor(const FLinearColor& Color)
	{
		return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B)
			&& FMath::IsFinite(Color.A) && Color.R >= 0.0f && Color.R <= 1.0f
			&& Color.G >= 0.0f && Color.G <= 1.0f && Color.B >= 0.0f && Color.B <= 1.0f
			&& Color.A >= 0.0f && Color.A <= 1.0f;
	}
}

bool UMD_GlassDrawingProfile::IsValidForDrawing() const
{
	const FVector2D Size = LocalMax - LocalMin;
	const UBodySetup* Body = IsValid(ApprovedMesh) ? ApprovedMesh->GetBodySetup() : nullptr;
	if (InkPalette.Num() > 8 || !IsValidInkColor(InkColor)) return false;
	for (const FLinearColor& Color : InkPalette)
	{
		if (!IsValidInkColor(Color)) return false;
	}
	return IsValid(ApprovedMesh) && GlassMaterialSlot >= 0
		&& GlassMaterialSlot < ApprovedMesh->GetStaticMaterials().Num() && GlassCollisionSection >= 0
		&& IsValid(Body) && CollisionBodySetupGuid.IsValid() && Body->BodySetupGuid == CollisionBodySetupGuid
		&& CollisionMeshLOD == ApprovedMesh->LODForCollision && CollisionMaterialSlot == GlassMaterialSlot
		&& !GlassCollisionFaces.IsEmpty() && GlassCollisionFaces.Num() <= 100000
		&& !LocalMin.ContainsNaN() && !LocalMax.ContainsNaN() && Size.X > 1.0 && Size.Y > 1.0
		&& FMath::IsFinite(FrontFaceY) && FMath::IsFinite(BackFaceY) && FrontFaceY > BackFaceY
		&& FMath::IsFinite(SurfaceToleranceCm) && SurfaceToleranceCm > 0.0f && SurfaceToleranceCm <= 8.0f
		&& FMath::IsFinite(MaxSurfaceAngleDegrees) && MaxSurfaceAngleDegrees >= 0.0f && MaxSurfaceAngleDegrees <= 30.0f
		&& IsValid(InkPlaneMesh) && IsValid(InkMaterial) && !InkMaterial->IsTwoSided()
		&& InkMaterial->GetBlendMode() == BLEND_Masked
		&& TextureWidth >= 128 && TextureWidth <= 1024 && TextureHeight >= 128 && TextureHeight <= 1024
		&& FMath::IsFinite(GlowStrength) && GlowStrength >= 0.0f && GlowStrength <= 4.0f
		&& FMath::IsFinite(InkWidthCm) && InkWidthCm >= 0.1f && InkWidthCm <= 5.0f
		&& FMath::IsFinite(SurfaceOffsetCm) && SurfaceOffsetCm >= 0.01f && SurfaceOffsetCm <= 2.0f
		&& MaxSegments >= 1 && MaxSegments <= 2048 && MaxUsedSurfaces >= 1 && MaxUsedSurfaces <= 32
		&& FMath::IsFinite(MaxSegmentLengthCm) && MaxSegmentLengthCm >= 1.0f && MaxSegmentLengthCm <= 100.0f
		&& FMath::IsFinite(MaxDrawDistanceCm) && MaxDrawDistanceCm >= 20.0f && MaxDrawDistanceCm <= 300.0f
		&& FMath::IsFinite(EraserRadiusCm) && EraserRadiusCm >= 0.5f && EraserRadiusCm <= 30.0f;
}

int32 UMD_GlassDrawingProfile::GetColorCount() const
{
	return FMath::Clamp(InkPalette.Num(), 1, 8);
}

bool UMD_GlassDrawingProfile::IsValidColorIndex(uint8 ColorIndex) const
{
	return InkPalette.Num() <= 8 && ColorIndex < GetColorCount();
}

FLinearColor UMD_GlassDrawingProfile::GetInkColor(uint8 ColorIndex) const
{
	FLinearColor Color = InkPalette.IsValidIndex(ColorIndex) ? InkPalette[ColorIndex] : InkColor;
	// Palette alpha is not ink transparency: the texture's alpha records painted coverage.
	Color.A = 1.0f;
	return Color;
}

bool UMD_GlassDrawingProfile::RebuildCollisionContract()
{
#if WITH_EDITOR
	if (!IsValid(ApprovedMesh) || !ApprovedMesh->GetRenderData() || GlassMaterialSlot < 0
		|| GlassMaterialSlot >= ApprovedMesh->GetStaticMaterials().Num()) return false;
	UBodySetup* Body = ApprovedMesh->GetBodySetup();
	if (!IsValid(Body) || !Body->BodySetupGuid.IsValid()) return false;
	// A separately authored complex collision mesh needs a separate reviewed profile contract.
	if (ApprovedMesh->ComplexCollisionMesh && ApprovedMesh->ComplexCollisionMesh != ApprovedMesh) return false;
	FTriMeshCollisionData Collision;
	if (!ApprovedMesh->GetPhysicsTriMeshData(&Collision, Body->bMeshCollideAll)
		|| Collision.Indices.Num() != Collision.MaterialIndices.Num()) return false;
	TArray<int32> Faces;
	for (int32 Face = 0; Face < Collision.MaterialIndices.Num(); ++Face)
	{
		if (Collision.MaterialIndices[Face] == GlassMaterialSlot) Faces.Add(Face);
	}
	if (Faces.IsEmpty() || Faces.Num() > 100000) return false;
	Modify();
	GlassCollisionFaces = MoveTemp(Faces);
	CollisionBodySetupGuid = Body->BodySetupGuid;
	CollisionMeshLOD = ApprovedMesh->LODForCollision;
	CollisionMaterialSlot = GlassMaterialSlot;
	return true;
#else
	return false;
#endif
}

bool UMD_GlassDrawingProfile::IsValidUV(const FVector2D& UV) const
{
	return !UV.ContainsNaN() && UV.X >= 0.0 && UV.X <= 1.0 && UV.Y >= 0.0 && UV.Y <= 1.0;
}

bool UMD_GlassDrawingProfile::ResolveHit(const FHitResult& Hit, FVector2D& OutUV) const
{
	OutUV = FVector2D::ZeroVector;
	const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Hit.GetComponent());
	if (!IsValidForDrawing() || !Hit.bBlockingHit || Hit.FaceIndex < 0 || !IsValid(Mesh)
		|| Mesh->GetStaticMesh() != ApprovedMesh || !Mesh->IsVisible()
		|| !IsValid(Mesh->GetOwner()) || Mesh->GetOwner()->IsHidden()
		|| Hit.ImpactPoint.ContainsNaN() || Hit.ImpactNormal.ContainsNaN()) return false;

	if (Algo::BinarySearch(GlassCollisionFaces, Hit.FaceIndex) == INDEX_NONE
		|| !IsValid(Mesh->GetMaterial(GlassMaterialSlot))) return false;

	const FTransform& Transform = Mesh->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D();
	const FVector AbsScale = Scale.GetAbs();
	if (Transform.ContainsNaN() || AbsScale.X <= UE_SMALL_NUMBER || AbsScale.Y <= UE_SMALL_NUMBER || AbsScale.Z <= UE_SMALL_NUMBER) return false;
	const FVector LocalPoint = Transform.InverseTransformPosition(Hit.ImpactPoint);
	const FVector LocalNormal = (Transform.InverseTransformVectorNoScale(Hit.ImpactNormal) * Scale).GetSafeNormal();
	const double NormalThreshold = FMath::Cos(FMath::DegreesToRadians(double(MaxSurfaceAngleDegrees)));
	if (LocalPoint.ContainsNaN() || LocalNormal.ContainsNaN() || FMath::Abs(LocalNormal.Y) < NormalThreshold
		|| LocalPoint.X < LocalMin.X || LocalPoint.X > LocalMax.X
		|| LocalPoint.Z < LocalMin.Y || LocalPoint.Z > LocalMax.Y) return false;

	// The material section alone includes the pane's narrow sides. Admit only the two authored faces.
	const double FaceY = LocalNormal.Y > 0.0 ? FrontFaceY : BackFaceY;
	if (FMath::Abs(LocalPoint.Y - FaceY) * AbsScale.Y > SurfaceToleranceCm) return false;
	const FVector2D Size = LocalMax - LocalMin;
	OutUV = FVector2D((LocalPoint.X - LocalMin.X) / Size.X, 1.0 - (LocalPoint.Z - LocalMin.Y) / Size.Y);
	return IsValidUV(OutUV);
}

FVector UMD_GlassDrawingProfile::UVToLocalPoint(const FVector2D& UV, double FaceY) const
{
	const FVector2D Size = LocalMax - LocalMin;
	return FVector(LocalMin.X + UV.X * Size.X, FaceY, LocalMax.Y - UV.Y * Size.Y);
}

