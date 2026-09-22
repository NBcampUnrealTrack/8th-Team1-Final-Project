#include "Tools/MD_PropPlacementSurface.h"

#include "Tools/MD_PlacementGeometry.h"
#include "Tools/MD_PropPlacementCandidate.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool MDPropPlacementSurface::IsSafe(UWorld* World, const FMD_PropPlacementCandidate& Candidate,
	const FMD_PropSurfacePlacement& Placement)
{
	if (!Placement.Transform.IsValid() || Candidate.Parts.IsEmpty())
	{
		return false;
	}
	if (!Candidate.Settings.bPreventOverlaps)
	{
		return true;
	}
	for (const FMD_PropPlacementPreviewPart& Part : Candidate.Parts)
	{
		if (!IsValid(Part.Mesh) || MDPlacementGeometry::HasBlockingOverlap(World, Part.Mesh->GetBoundingBox(),
			Part.LocalTransform * Placement.Transform, Candidate.Settings.OverlapTolerance,
			Placement.Surface.Get(), Placement.Floor.Get()))
		{
			return false;
		}
	}
	return true;
}

FMD_PropSurfacePlacement MDPropPlacementSurface::Solve(UWorld* World, const FMD_PropPlacementCandidate& Candidate,
	const FHitResult& Hit, uint32 CandidateIndex, const FCollisionQueryParams& Query)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_PropSmartEdge);
	FMD_PropSurfacePlacement Result;
	Result.Transform = Candidate.BuildTransform(Hit, CandidateIndex);
	Result.Surface = Hit.GetComponent();
	const FMD_PropPlacementSettings& Settings = Candidate.Settings;
	// Manual pitch/roll and non-upright surfaces retain the authored pose. No world scans,
	// physics simulation or new mesh data are created while moving the cursor.
	if (!World || !Settings.bSmartEdge || !Settings.bAlignToSurface || !Hit.GetComponent()
		|| Settings.Pitch.bRandomize || Settings.Roll.bRandomize
		|| !FMath::IsNearlyZero(Settings.Pitch.AngleDegrees) || !FMath::IsNearlyZero(Settings.Roll.AngleDegrees)
		|| Hit.ImpactNormal.Z < 0.98 || Candidate.Parts.IsEmpty() || Candidate.Parts.Num() > 8)
	{
		return Result;
	}
	FBox Bounds(ForceInit);
	for (const FMD_PropPlacementPreviewPart& Part : Candidate.Parts)
	{
		Bounds += Part.Mesh->GetBoundingBox().TransformBy(Part.LocalTransform);
	}
	const FVector Scale = Result.Transform.GetScale3D();
	const FVector Extent = Bounds.GetExtent() * Scale.GetAbs();
	int32 ThinAxis = 0;
	int32 LongAxis = 0;
	for (int32 Axis = 1; Axis < 3; ++Axis)
	{
		if (Extent[Axis] < Extent[ThinAxis])
		{
			ThinAxis = Axis;
		}
		if (Extent[Axis] > Extent[LongAxis])
		{
			LongAxis = Axis;
		}
	}
	if (ThinAxis == LongAxis || Extent[ThinAxis] <= UE_SMALL_NUMBER
		|| Extent[ThinAxis] > Extent[3 - ThinAxis - LongAxis] * 0.35)
	{
		return Result;
	}
	const double HalfLength = Extent[LongAxis];
	const double HalfThickness = Extent[ThinAxis];
	const double Radius = FMath::Min(Settings.EdgeSearchDistance, HalfLength * 0.5);
	const double Depth = HalfLength * 2.0 + HalfThickness * 2.0;
	const FVector Up = FVector::UpVector;
	FVector Hint = (Hit.ImpactPoint - Hit.GetComponent()->Bounds.Origin).GetSafeNormal2D();
	if (Hint.IsNearlyZero())
	{
		Hint = FVector::ForwardVector;
	}
	const double StartAngle = FMath::Atan2(Hint.Y, Hint.X);
	FVector Outward;
	FHitResult LowerHit;
	bool bFoundEdge = false;
	// At most eight probes. Prefer the direction away from the support's center.
	for (int32 Probe = 0; Probe < 8; ++Probe)
	{
		const int32 Offset = Probe == 0 ? 0 : (Probe % 2 ? (Probe + 1) / 2 : -Probe / 2);
		const double Angle = StartAngle + Offset * UE_PI / 4.0;
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		const FVector Point = Hit.ImpactPoint + Direction * Radius;
		FHitResult Lower;
		if (World->LineTraceSingleByChannel(Lower, Point + Up * 1.0, Point - Up * Depth, ECC_Visibility, Query)
			&& Lower.ImpactNormal.Z >= 0.98 && Hit.ImpactPoint.Z - Lower.ImpactPoint.Z > 0.5
			&& Hit.ImpactPoint.Z - Lower.ImpactPoint.Z < HalfLength * 1.9)
		{
			Outward = Direction;
			LowerHit = Lower;
			bFoundEdge = true;
			break;
		}
	}
	if (!bFoundEdge)
	{
		return Result;
	}
	// Refine the top edge instead of anchoring the new prop to an arbitrary point in the top face.
	double Inside = 0.0;
	double Outside = Radius;
	FVector Edge = Hit.ImpactPoint;
	for (int32 Step = 0; Step < 5; ++Step)
	{
		const double Distance = (Inside + Outside) * 0.5;
		const FVector Point = Hit.ImpactPoint + Outward * Distance;
		FHitResult Top;
		if (World->LineTraceSingleByChannel(Top, Point + Up, Point - Up * Depth, ECC_Visibility, Query)
			&& Top.GetComponent() == Hit.GetComponent() && FMath::Abs(Top.ImpactPoint.Z - Hit.ImpactPoint.Z) <= 0.25)
		{
			Inside = Distance;
			Edge = Top.ImpactPoint;
		}
		else
		{
			Outside = Distance;
		}
	}
	// Place just outside the refined edge to avoid sinking into its final search interval.
	Edge += Outward * (Outside - Inside);
	FVector LocalLong = FVector::ZeroVector;
	FVector LocalThin = FVector::ZeroVector;
	LocalLong[LongAxis] = FMath::Sign(Scale[LongAxis]);
	LocalThin[ThinAxis] = FMath::Sign(Scale[ThinAxis]);
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		const double Angle = FMath::DegreesToRadians(FMath::Max(15.0, Settings.LeanAngle - Attempt * 15.0));
		const double Sin = FMath::Sin(Angle);
		const double Cos = FMath::Cos(Angle);
		const FVector LongDirection = Up * Sin - Outward * Cos;
		const FVector ThinDirection = Outward * Sin + Up * Cos;
		const FQuat Align = FQuat::FindBetweenNormals(LocalLong, LongDirection);
		const FVector AlignedThin = Align.RotateVector(LocalThin);
		const double Twist = FMath::Atan2(FVector::DotProduct(LongDirection, FVector::CrossProduct(AlignedThin, ThinDirection)),
			FVector::DotProduct(AlignedThin, ThinDirection));
		const FQuat Rotation = (FQuat(LongDirection, Twist) * Align).GetNormalized();
		const double CenterZ = LowerHit.ImpactPoint.Z + HalfLength * Sin + HalfThickness * Cos;
		const double Shift = (HalfThickness + (Edge.Z - CenterZ) * Cos) / Sin;
		FVector Center = Edge + Outward * Shift;
		Center.Z = CenterZ;
		const double ContactAlongLength = FVector::DotProduct(Edge - Center, LongDirection);
		if (FMath::Abs(ContactAlongLength) >= HalfLength)
		{
			continue;
		}
		const FVector Foot = Center - LongDirection * HalfLength - ThinDirection * HalfThickness;
		FHitResult Floor;
		if (!World->LineTraceSingleByChannel(Floor, Foot + Up * 0.5, Foot - Up * 0.5, ECC_Visibility, Query)
			|| Floor.ImpactNormal.Z < 0.98 || FMath::Abs(Floor.ImpactPoint.Z - Foot.Z) > 0.1)
		{
			continue;
		}
		// Check both ends of the bottom edge, not only its center, so a wide book is not
		// balanced over a ledge where most of its lower edge has no floor.
		const FVector Side = FVector::CrossProduct(LongDirection, ThinDirection).GetSafeNormal();
		const double HalfWidth = Extent[3 - ThinAxis - LongAxis];
		bool bSupported = true;
		for (const double Sign : {-1.0, 1.0})
		{
			FHitResult SideFloor;
			const FVector SideFoot = Foot + Side * (HalfWidth * 0.9 * Sign);
			if (!World->LineTraceSingleByChannel(SideFloor, SideFoot + Up * 0.5, SideFoot - Up * 0.5, ECC_Visibility, Query)
				|| SideFloor.ImpactNormal.Z < 0.98 || FMath::Abs(SideFloor.ImpactPoint.Z - Foot.Z) > 0.1
				|| SideFloor.GetComponent() != Floor.GetComponent())
			{
				bSupported = false;
				break;
			}
		}
		if (!bSupported)
		{
			continue;
		}
		FMD_PropSurfacePlacement Leaned;
		Leaned.Surface = Hit.GetComponent();
		Leaned.Floor = Floor.GetComponent();
		Leaned.Transform = FTransform(Rotation, Center - Rotation.RotateVector(Bounds.GetCenter() * Scale)
			+ Up * Settings.SurfaceOffset, Scale);
		if (IsSafe(World, Candidate, Leaned))
		{
			return Leaned;
		}
	}
	return Result;
}
