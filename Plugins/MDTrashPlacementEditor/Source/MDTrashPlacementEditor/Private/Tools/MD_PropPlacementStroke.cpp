#include "Tools/MD_PropPlacementStroke.h"

#include "Tools/MD_PlacementGeometry.h"

void FMD_PropPlacementStrokePath::Reset()
{
	bHasPreviousHit = false;
	DistanceSinceSample = 0.0;
	PreviousHit = FHitResult();
}

void FMD_PropPlacementStrokePath::Begin(const FHitResult& Hit)
{
	PreviousHit = Hit;
	DistanceSinceSample = 0.0;
	bHasPreviousHit = true;
}

void FMD_PropPlacementStrokePath::Sample(const FHitResult& Hit, double Spacing, TArray<FHitResult>& OutSamples)
{
	OutSamples.Reset();
	Spacing = FMath::IsFinite(Spacing) ? FMath::Max(Spacing, 1.0) : 100.0;
	if (!bHasPreviousHit)
	{
		Begin(Hit);
		return;
	}
	const FVector StartNormal = MDPlacementGeometry::GetSurfaceNormal(PreviousHit);
	const FVector EndNormal = MDPlacementGeometry::GetSurfaceNormal(Hit);
	const double Length = FVector::Distance(PreviousHit.ImpactPoint, Hit.ImpactPoint);
	constexpr int32 MaxSamplesPerInput = 64;
	if (!FMath::IsFinite(Length) || Length > Spacing * MaxSamplesPerInput
		|| FVector::DotProduct(StartNormal, EndNormal) < 0.5)
	{
		// Discontinuous cursor jumps and sharp corners start a fresh segment.
		Begin(Hit);
		return;
	}
	if (Length > UE_KINDA_SMALL_NUMBER)
	{
		for (double Distance = Spacing - DistanceSinceSample;
			Distance <= Length && OutSamples.Num() < MaxSamplesPerInput; Distance += Spacing)
		{
			const double Alpha = Distance / Length;
			FHitResult& SampleHit = OutSamples.Add_GetRef(Hit);
			SampleHit.ImpactPoint = FMath::Lerp(PreviousHit.ImpactPoint, Hit.ImpactPoint, Alpha);
			SampleHit.Location = SampleHit.ImpactPoint;
			SampleHit.ImpactNormal = FMath::Lerp(StartNormal, EndNormal, Alpha).GetSafeNormal();
			SampleHit.Normal = SampleHit.ImpactNormal;
		}
		DistanceSinceSample = FMath::Fmod(DistanceSinceSample + Length, Spacing);
	}
	PreviousHit = Hit;
}
