#pragma once
#include "CoreMinimal.h"
#include "MD_ContaminationGeneration.generated.h"

/** Frozen initial state: sparse saves must not reconstruct untouched cells from today's defaults. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ContaminationGeneration
{
    GENERATED_BODY()
    /** 0: absent in legacy save; 1: original center sampling; 2: continuous contour; 3: continuous absolute density. */
    UPROPERTY() uint8 Version = 0;
    UPROPERTY() bool bProcedural = false;
    UPROPERTY() float BaseAmount = 1.0f;
    UPROPERTY() float NoiseScaleCm = 75.0f;
    UPROPERTY() int32 Seed = 1337;
    UPROPERTY() float Coverage = 0.55f;
    UPROPERTY() float MinimumAmount = 0.25f;
    UPROPERTY() FTransform SampleTransform = FTransform::Identity;

    bool IsValid() const;
    bool NetSerialize(FArchive& Archive, class UPackageMap* Map, bool& bOutSuccess);
    bool operator==(const FMD_ContaminationGeneration& Other) const;
    bool operator!=(const FMD_ContaminationGeneration& Other) const { return !(*this == Other); }
    float Sample(const FVector& LocalPosition) const;
    float AmountFromNoise(float Noise) const;
    /** Area mean, with bounded subdivision of only noise-threshold boundary triangles. */
    float IntegrateTriangle(const FVector& LocalA, const FVector& LocalB, const FVector& LocalC) const;
};

template<> struct TStructOpsTypeTraits<FMD_ContaminationGeneration> : TStructOpsTypeTraitsBase2<FMD_ContaminationGeneration>
{
    enum { WithNetSerializer = true, WithNetSharedSerialization = true };
};
