#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Scrape/MD_ContaminationSolidGeometry.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "MD_ContaminationSolidPrewarmSubsystem.generated.h"

/** One startup batch; no steady-state ticking, collision, replication or render work. */
UCLASS()
class MARINEDEMOLITION_API UMD_ContaminationSolidPrewarmSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void OnWorldEndPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category="MD|Scrape Surface|Solid")
    FString GetPrewarmSummary() const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    void ReleasePrewarm();
    TUniquePtr<FMD_ContaminationSolidPrewarmBatch> PendingPrewarm;
    FMD_ContaminationSolidPrewarmStats LastStats;
    FTimerHandle ReleaseTimer;
    bool bPreparedForWorld = false;
};
