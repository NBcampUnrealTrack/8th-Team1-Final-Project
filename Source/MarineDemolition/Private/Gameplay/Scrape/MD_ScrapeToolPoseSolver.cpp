// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Scrape/MD_ScrapeToolPoseSolver.h"

#include "Math/RotationMatrix.h"

namespace
{
bool HasInvertibleScrapeToolPoseScale(const FTransform& Transform)
{
	const FVector Scale = Transform.GetScale3D();
	return !Scale.ContainsNaN()
		&& FMath::Abs(Scale.X) > UE_SMALL_NUMBER
		&& FMath::Abs(Scale.Y) > UE_SMALL_NUMBER
		&& FMath::Abs(Scale.Z) > UE_SMALL_NUMBER;
}

bool IsFiniteScrapeToolPoseTransform(const FTransform& Transform)
{
	return !Transform.ContainsNaN()
		&& Transform.GetRotation().IsNormalized()
		&& HasInvertibleScrapeToolPoseScale(Transform);
}
}

bool FMD_ScrapeToolPoseSolver::BuildContactWorldTransform(
	const FVector& ContactWorldLocation,
	const FVector& ContactWorldNormal,
	const FVector& StrokeWorldTangent,
	const FVector& PreviousStableWorldTangent,
	const FVector& ContactWorldScale,
	FTransform& OutContactWorldTransform,
	FVector& OutStableStrokeWorldTangent)
{
	OutContactWorldTransform = FTransform::Identity;
	OutStableStrokeWorldTangent = FVector::ZeroVector;
	if (ContactWorldLocation.ContainsNaN()
		|| ContactWorldNormal.ContainsNaN()
		|| StrokeWorldTangent.ContainsNaN()
		|| ContactWorldScale.ContainsNaN())
	{
		return false;
	}

	const FVector SafeNormal = ContactWorldNormal.GetSafeNormal();
	FVector SafeTangent = FVector::VectorPlaneProject(
		StrokeWorldTangent,
		SafeNormal).GetSafeNormal();
	if (SafeNormal.IsNearlyZero() || SafeTangent.IsNearlyZero())
	{
		return false;
	}

	const FVector PreviousProjected = FVector::VectorPlaneProject(
		PreviousStableWorldTangent,
		SafeNormal).GetSafeNormal();
	if (!PreviousProjected.IsNearlyZero()
		&& FVector::DotProduct(SafeTangent, PreviousProjected) < 0.0)
	{
		SafeTangent *= -1.0;
	}

	const FVector SafeScale(
		FMath::Abs(ContactWorldScale.X),
		FMath::Abs(ContactWorldScale.Y),
		FMath::Abs(ContactWorldScale.Z));
	if (SafeScale.X <= UE_SMALL_NUMBER
		|| SafeScale.Y <= UE_SMALL_NUMBER
		|| SafeScale.Z <= UE_SMALL_NUMBER)
	{
		return false;
	}

	// Contact socket contract: local X is the outward surface normal and local
	// Z is the blade depth / stroke direction. MakeFromXZ also derives the
	// orthogonal width axis, so all three axes stay right-handed.
	const FQuat ContactRotation(
		FRotationMatrix::MakeFromXZ(SafeNormal, SafeTangent));
	OutContactWorldTransform = FTransform(
		ContactRotation.GetNormalized(),
		ContactWorldLocation,
		SafeScale);
	OutStableStrokeWorldTangent = SafeTangent;
	return IsFiniteScrapeToolPoseTransform(OutContactWorldTransform);
}

bool FMD_ScrapeToolPoseSolver::SolveHandBoneWorldTransform(
	const FTransform& BladeContactMeshLocal,
	const FTransform& ToolRelativeToAttachSocket,
	const FTransform& ToolAttachSocketParentBoneLocal,
	const FTransform& ContactWorldTransform,
	FMD_ScrapeToolPoseSolution& OutSolution)
{
	OutSolution = FMD_ScrapeToolPoseSolution();
	if (!IsFiniteScrapeToolPoseTransform(BladeContactMeshLocal)
		|| !IsFiniteScrapeToolPoseTransform(ToolRelativeToAttachSocket)
		|| !IsFiniteScrapeToolPoseTransform(ToolAttachSocketParentBoneLocal)
		|| !IsFiniteScrapeToolPoseTransform(ContactWorldTransform))
	{
		return false;
	}

	OutSolution.ContactWorldTransform = ContactWorldTransform;
	// UE composition order is local * parent/world. Each inverse removes the
	// fixed local link immediately to its left in the equations documented in
	// the header.
	OutSolution.ToolWorldTransform =
		BladeContactMeshLocal.Inverse() * ContactWorldTransform;
	OutSolution.ToolAttachSocketWorldTransform =
		ToolRelativeToAttachSocket.Inverse()
		* OutSolution.ToolWorldTransform;
	OutSolution.HandBoneWorldTransform =
		ToolAttachSocketParentBoneLocal.Inverse()
		* OutSolution.ToolAttachSocketWorldTransform;

	const FTransform ReattachedToolWorld = ToolRelativeToAttachSocket
		* ToolAttachSocketParentBoneLocal
		* OutSolution.HandBoneWorldTransform;
	return IsFiniteScrapeToolPoseTransform(OutSolution.ToolWorldTransform)
		&& IsFiniteScrapeToolPoseTransform(OutSolution.ToolAttachSocketWorldTransform)
		&& IsFiniteScrapeToolPoseTransform(OutSolution.HandBoneWorldTransform)
		&& IsContactInvariantSatisfied(
			BladeContactMeshLocal,
			OutSolution.ToolWorldTransform,
			ContactWorldTransform)
		&& IsContactInvariantSatisfied(
			BladeContactMeshLocal,
			ReattachedToolWorld,
			ContactWorldTransform);
}

bool FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
	const FVector& ShoulderComponentLocation,
	const FVector& TargetHandComponentLocation,
	const double UpperArmLength,
	const double LowerArmLength,
	const double MaximumReachRatio)
{
	if (ShoulderComponentLocation.ContainsNaN()
		|| TargetHandComponentLocation.ContainsNaN()
		|| !FMath::IsFinite(UpperArmLength)
		|| !FMath::IsFinite(LowerArmLength)
		|| !FMath::IsFinite(MaximumReachRatio)
		|| UpperArmLength <= UE_DOUBLE_SMALL_NUMBER
		|| LowerArmLength <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const double Distance = FVector::Distance(
		ShoulderComponentLocation,
		TargetHandComponentLocation);
	const double MaximumReach = (UpperArmLength + LowerArmLength)
		* FMath::Clamp(MaximumReachRatio, 0.0, 1.0);
	const double MinimumReach = FMath::Abs(UpperArmLength - LowerArmLength);
	return Distance + UE_KINDA_SMALL_NUMBER >= MinimumReach
		&& Distance <= MaximumReach + UE_KINDA_SMALL_NUMBER;
}

bool FMD_ScrapeToolPoseSolver::IsContactInvariantSatisfied(
	const FTransform& BladeContactMeshLocal,
	const FTransform& ToolWorldTransform,
	const FTransform& ExpectedContactWorldTransform,
	const double LocationToleranceCm,
	const double RotationToleranceDegrees)
{
	if (BladeContactMeshLocal.ContainsNaN()
		|| ToolWorldTransform.ContainsNaN()
		|| ExpectedContactWorldTransform.ContainsNaN())
	{
		return false;
	}

	const FTransform ReconstructedContact =
		BladeContactMeshLocal * ToolWorldTransform;
	const double LocationError = FVector::Distance(
		ReconstructedContact.GetLocation(),
		ExpectedContactWorldTransform.GetLocation());
	const double RotationDot = FMath::Clamp(
		FMath::Abs(ReconstructedContact.GetRotation().GetNormalized()
			| ExpectedContactWorldTransform.GetRotation().GetNormalized()),
		0.0,
		1.0);
	const double RotationErrorDegrees = FMath::RadiansToDegrees(
		2.0 * FMath::Acos(RotationDot));
	return LocationError <= FMath::Max(LocationToleranceCm, 0.0)
		&& RotationErrorDegrees <= FMath::Max(RotationToleranceDegrees, 0.0);
}
