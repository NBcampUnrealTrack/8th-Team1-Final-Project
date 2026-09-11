#include "Gameplay/Contamination/MD_StainStickerPatch.h"
#include "Gameplay/Contamination/MD_SurfaceStainSize.h"

bool FMD_StainStickerPatch::IsValid() const
{
	return InitialSize >= FMD_SurfaceStainSize::Minimum && InitialSize <= FMD_SurfaceStainSize::Maximum
		&& Positions.Num() >= 3 && Positions.Num() <= MaximumTriangles * 3
		&& Positions.Num() % 3 == 0 && UVs.Num() == Positions.Num() && LocalBounds.IsValid;
}

float FMD_StainStickerPatch::UVScale(int32 RemainingSize, int32 BakedSize)
{
	return FMath::Sqrt(float(FMath::Max(1, BakedSize)) / FMath::Max(1, RemainingSize));
}

bool FMD_StainStickerPatch::FindUV(const FVector& Point, const FVector& SurfaceNormal, FVector2D& OutUV) const
{
	if (!IsValid() || Point.ContainsNaN() || SurfaceNormal.ContainsNaN()
		|| !LocalBounds.ExpandBy(0.2).IsInsideOrOn(Point)) return false;
	double Nearest = 0.2;
	bool bFound = false;
	for (int32 Index = 0; Index < Positions.Num(); Index += 3)
	{
		const FVector A(Positions[Index]), E0 = FVector(Positions[Index + 1]) - A, E1 = FVector(Positions[Index + 2]) - A;
		const FVector Cross = FVector::CrossProduct(E0, E1);
		const double AreaSquared = Cross.SquaredLength();
		if (AreaSquared <= 1.e-16) continue;
		const FVector Normal = -Cross / FMath::Sqrt(AreaSquared);
		if (FVector::DotProduct(Normal, SurfaceNormal) < 0.5) continue;
		const FVector D = Point - A;
		const double Distance = FMath::Abs(FVector::DotProduct(D, Normal));
		if (Distance > Nearest) continue;
		const double B = FVector::DotProduct(FVector::CrossProduct(D, E1), Cross) / AreaSquared;
		const double C = FVector::DotProduct(FVector::CrossProduct(E0, D), Cross) / AreaSquared;
		if (B < -1.e-5 || C < -1.e-5 || B + C > 1.00001) continue;
		Nearest = Distance;
		OutUV = FVector2D(UVs[Index]) * (1 - B - C) + FVector2D(UVs[Index + 1]) * B + FVector2D(UVs[Index + 2]) * C;
		bFound = true;
	}
	return bFound;
}
