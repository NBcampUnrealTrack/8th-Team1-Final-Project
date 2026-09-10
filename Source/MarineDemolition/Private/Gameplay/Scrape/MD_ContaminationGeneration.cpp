#include "Gameplay/Scrape/MD_ContaminationGeneration.h"
#include "Gameplay/Scrape/MD_ContaminationNoise.h"

bool FMD_ContaminationGeneration::IsValid() const
{
    const FVector Scale = SampleTransform.GetScale3D();
    return Version >= 1 && Version <= 3 && !SampleTransform.ContainsNaN()
        && SampleTransform.GetRotation().IsNormalized()
        && SampleTransform.GetTranslation().GetAbsMax() < 1.e8
        && Scale.GetAbsMin() > UE_SMALL_NUMBER && Scale.GetAbsMax() < 1.e4
        && FMath::IsFinite(BaseAmount) && BaseAmount >= 0 && BaseAmount <= 1
        && FMath::IsFinite(NoiseScaleCm) && NoiseScaleCm >= 1
        && FMath::IsFinite(Coverage) && Coverage >= 0 && Coverage <= 1
        && FMath::IsFinite(MinimumAmount) && MinimumAmount >= 0 && MinimumAmount <= 1;
}
// Sparse clients must evaluate the identical frozen transform. Generic quaternion
// property replication reconstructs W and can change an arbitrary rotation slightly.
// Archive the full transform, including all four double quaternion components.
bool FMD_ContaminationGeneration::NetSerialize(FArchive& Ar, UPackageMap*, bool& bOutSuccess)
{
    Ar << Version << bProcedural << BaseAmount << NoiseScaleCm << Seed << Coverage << MinimumAmount;
    Ar << SampleTransform;
    bOutSuccess = !Ar.IsError() && (Version == 0 || IsValid());
    return true;
}
bool FMD_ContaminationGeneration::operator==(const FMD_ContaminationGeneration& O) const
{
    return Version == O.Version && bProcedural == O.bProcedural && BaseAmount == O.BaseAmount
        && NoiseScaleCm == O.NoiseScaleCm && Seed == O.Seed && Coverage == O.Coverage
        && MinimumAmount == O.MinimumAmount && SampleTransform.Equals(O.SampleTransform, 0.0f);
}
float FMD_ContaminationGeneration::Sample(const FVector& P) const
{
    if (!bProcedural) return BaseAmount;
    float Amount = 0;
    FMD_ContaminationNoise::TryCalculateInitialContamination(
        SampleTransform.TransformPosition(P), BaseAmount, NoiseScaleCm, Seed, Coverage, MinimumAmount, Amount);
    return Amount;
}
float FMD_ContaminationGeneration::AmountFromNoise(const float Noise) const
{
    if (!bProcedural) return BaseAmount;
    if (Coverage<=UE_SMALL_NUMBER || Noise<=1.f-Coverage) return 0;
    const float A=FMath::Clamp((Noise-(1.f-Coverage))/Coverage,0.f,1.f);
    return BaseAmount*FMath::Lerp(MinimumAmount,1.f,A*A*(3.f-2.f*A));
}

namespace
{
struct FNoiseVertex { FVector P; float N; };
float Density(const FMD_ContaminationGeneration& G, const float N)
{
    const float A = FMath::Clamp((N - (1.f - G.Coverage)) / G.Coverage, 0.f, 1.f);
    return G.BaseAmount * FMath::Lerp(G.MinimumAmount, 1.f, A * A * (3.f - 2.f * A));
}
float Integrate(const FMD_ContaminationGeneration& G, const FVector& A, const FVector& B,
    const FVector& C, const int32 Depth)
{
    const FVector Center = (A+B+C)/3.;
    const float N = FMD_ContaminationNoise::FractalNoise3D(Center, G.NoiseScaleCm, G.Seed);
    const double Radius = FMath::Max3((A-Center).Length(), (B-Center).Length(), (C-Center).Length());
    // 3 smooth value-noise octaves have a gradient norm < 4.46 / scale.
    const double Bound = 4.5 * Radius / G.NoiseScaleCm;
    const float Threshold = 1.f-G.Coverage;
    if (N + Bound <= Threshold) return 0;
    if (N - Bound > Threshold) return Density(G,N);
    if (Depth < 2 && Radius > .125)
    {
        const FVector AB=(A+B)*.5, BC=(B+C)*.5, CA=(C+A)*.5;
        return .25f*(Integrate(G,A,AB,CA,Depth+1)+Integrate(G,AB,B,BC,Depth+1)
            +Integrate(G,CA,BC,C,Depth+1)+Integrate(G,AB,BC,CA,Depth+1));
    }
    TArray<FNoiseVertex,TInlineAllocator<6>> Polygon;
    const FVector Points[]={A,B,C};
    for (const FVector& P : Points)
        Polygon.Add({P,FMD_ContaminationNoise::FractalNoise3D(P,G.NoiseScaleCm,G.Seed)});
    TArray<FNoiseVertex,TInlineAllocator<6>> Clipped;
    for (int32 I=0;I<3;++I)
    {
        const auto& P=Polygon[I]; const auto& Q=Polygon[(I+1)%3];
        const bool InP=P.N>Threshold, InQ=Q.N>Threshold;
        if (InP) Clipped.Add(P);
        if (InP!=InQ)
        {
            const double T=(Threshold-P.N)/(Q.N-P.N);
            Clipped.Add({FMath::Lerp(P.P,Q.P,T),Threshold});
        }
    }
    const double TotalArea=FVector::CrossProduct(B-A,C-A).Length();
    if (TotalArea<=UE_DOUBLE_SMALL_NUMBER || Clipped.Num()<3) return 0;
    double Sum=0;
    for (int32 I=1;I+1<Clipped.Num();++I)
    {
        const auto& P=Clipped[0];const auto& Q=Clipped[I];const auto& S=Clipped[I+1];
        const double Area=FVector::CrossProduct(Q.P-P.P,S.P-P.P).Length();
        Sum+=Area*Density(G,(P.N+Q.N+S.N)/3.f);
    }
    return float(Sum/TotalArea);
}
}
float FMD_ContaminationGeneration::IntegrateTriangle(const FVector& A, const FVector& B, const FVector& C) const
{
    if (!bProcedural) return BaseAmount;
    if (Coverage<=UE_SMALL_NUMBER || BaseAmount<=UE_SMALL_NUMBER) return 0;
    return Integrate(*this,SampleTransform.TransformPosition(A),SampleTransform.TransformPosition(B),
        SampleTransform.TransformPosition(C),0);
}
