#include "Tools/MD_StainEdgePlacement.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Gameplay/Contamination/MD_SurfaceStainDefinition.h"

bool FMD_StainEdgePlacement::Validate(UWorld& World, UMD_SurfaceStainDefinition& Definition,
	UPrimitiveComponent& Surface, const FTransform& Contact, int32 InitialSize, FString& Error)
{
	if (!Definition.CanPlaceEdgeVolume(InitialSize, Error)) return false;
	const double Scale = Definition.GetVolumeScale(InitialSize).X;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDSlimeEdgeContact), true);
	const double Reach = FMath::Max(10.0, Definition.VolumeStages[0].QueryBounds.GetSize().Z * Scale + 2.0);
	for (const FMD_SurfaceStainEdgeContact& Sample : Definition.EdgeContacts)
	{
		const FVector Point = Contact.TransformPosition(Sample.Position * Scale);
		const FVector Normal = Contact.TransformVectorNoScale(Sample.Normal);
		FHitResult Hit;
		if (!World.LineTraceSingleByChannel(Hit, Point + Normal * Reach, Point - Normal * 0.25,
			ECC_Visibility, Params) || Hit.bStartPenetrating || Hit.GetComponent() != &Surface)
		{ Error = TEXT("The body and short flow need the same receiving mesh; a gap, thin edge or another object interrupts contact."); return false; }
		if (FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - Point, Normal)) > 0.1
			|| FVector::DotProduct(Hit.ImpactNormal, Normal) < 0.97)
		{ Error = TEXT("The edge angle or rounding does not fit this profile. Select the matching edge profile or use another surface."); return false; }
	}
	return true;
}

bool FMD_StainEdgePlacement::Resolve(UWorld& World, UMD_SurfaceStainDefinition& Definition,
	UPrimitiveComponent& Surface, const FTransform& Hint, int32 InitialSize, FTransform& OutContact, FString& Error)
{
	if (!Definition.CanPlaceEdgeVolume(InitialSize, Error)) return false;
	const double Scale = Definition.GetVolumeScale(InitialSize).X;
	const FVector Origin = Hint.GetLocation(), Normal = Hint.GetUnitAxis(EAxis::Z);
	const FVector X = Hint.GetUnitAxis(EAxis::X), Y = Hint.GetUnitAxis(EAxis::Y);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MDSlimeEdgeSearch), true);
	// The cursor must be on the main planar face, not on the bend itself.
	auto OnMain = [&](const FVector& Point)
	{
		FHitResult Hit;
		return World.LineTraceSingleByChannel(Hit, Point + Normal * 10.0, Point - Normal * 1.0,
			ECC_Visibility, Params) && !Hit.bStartPenetrating && Hit.GetComponent() == &Surface
			&& FVector::DotProduct(Hit.ImpactNormal, Normal) > 0.995
			&& FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - Point, Normal)) < 0.1;
	};
	if (!OnMain(Origin)) { Error = TEXT("Point at the main flat face just inside the edge."); return false; }
	TArray<FVector, TInlineAllocator<16>> TriedNormals;
	double BestDistance = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 DirectionIndex = 0; DirectionIndex < 16; ++DirectionIndex)
	{
		const double Angle = 2.0 * PI * DirectionIndex / 16.0;
		const FVector Direction = X * FMath::Cos(Angle) + Y * FMath::Sin(Angle);
		for (int32 Step = 1; Step <= 6; ++Step)
		{
			const double Distance = Step * 4.0 * Scale;
			if (OnMain(Origin + Direction * Distance)) continue;
			// Probe the outward-facing side below the main face. Concave edges face the wrong way.
			const FVector Below = Origin - Normal * ((Definition.EdgeRadiusCm + 3.0) * Scale);
			FHitResult Side;
			if (!World.LineTraceSingleByChannel(Side, Below + Direction * (Distance + 10.0 * Scale),
				Below - Direction * 4.0 * Scale, ECC_Visibility, Params)
				|| Side.bStartPenetrating || Side.GetComponent() != &Surface
				|| FMath::Abs(FVector::DotProduct(Side.ImpactNormal, Normal)) > 0.035
				|| FVector::DotProduct(Side.ImpactNormal, Direction) < 0.35) break;
			const FVector Outward = FVector::VectorPlaneProject(Side.ImpactNormal, Normal).GetSafeNormal();
			bool bTried = false;
			for (const FVector& Previous : TriedNormals) bTried |= FVector::DotProduct(Previous, Outward) > 0.999;
			if (bTried) break;
			TriedNormals.Add(Outward);
			const double EdgeDistance = FVector::DotProduct(Side.ImpactPoint - Origin, Outward);
			if (EdgeDistance < 0.0 || EdgeDistance > 24.0 * Scale) break;
			const FVector Anchor = Origin + Outward * (EdgeDistance - (Definition.EdgeStartCm + Definition.EdgeRadiusCm) * Scale);
			const FTransform Candidate(FRotationMatrix::MakeFromZY(Normal, Outward).ToQuat(), Anchor);
			FString CandidateError;
			if (Validate(World, Definition, Surface, Candidate, InitialSize, CandidateError))
			{
				const double SnapDistance = FVector::DistSquared(Anchor, Origin);
				if (SnapDistance < BestDistance) { OutContact = Candidate; BestDistance = SnapDistance; bFound = true; }
			}
			else Error = CandidateError;
			break;
		}
	}
	if (!bFound && Error.IsEmpty()) Error = TEXT("No supported convex right-angle edge nearby. Move onto its main face within 24 cm at size 300.");
	if (bFound) Error.Reset();
	return bFound;
}
