#pragma once

#include "CoreMinimal.h"
#include "Types/Scrape/MD_ScrapeTypes.h"
#include "MD_ContaminationPendingDrop.generated.h"

class AMD_ContaminationItem;

/** Durable physical units waiting for an actor slot; detached from the source wall. */
USTRUCT()
struct MARINEDEMOLITION_API FMD_ContaminationPendingDrop
{
    GENERATED_BODY()
    UPROPERTY() FGuid BatchId;
    UPROPERTY() uint64 EnqueueOrder = 0;
    UPROPERTY() FName SourceActorName;
    UPROPERTY() FName SourceComponentName;
    UPROPERTY() EMD_ScrapeSurfaceFace SurfaceFace = EMD_ScrapeSurfaceFace::Front;
    UPROPERTY() TSoftClassPtr<AMD_ContaminationItem> ItemClass;
    UPROPERTY() FTransform SpawnTransform = FTransform::Identity;
    UPROPERTY() FVector LinearVelocity = FVector::ZeroVector;
    UPROPERTY() FVector AngularVelocityRadians = FVector::ZeroVector;
    /** A saved live actor is one indivisible item even when its legacy raw amount is unknown. */
    UPROPERTY() bool bSinglePhysicalItem = false;
    UPROPERTY() double PendingCarrierAmount = 0.0;
    UPROPERTY() double CarrierAmountPerItem = 0.0;
    UPROPERTY() bool bSpawnConfigurationReady = false;
    UPROPERTY() TArray<FMD_ScrapeRecoveryLot> RecoveryLots;

    bool IsStructurallyValid() const;
};
