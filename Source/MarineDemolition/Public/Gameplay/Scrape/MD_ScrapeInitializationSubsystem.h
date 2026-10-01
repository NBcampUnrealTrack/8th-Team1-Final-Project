#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Async/Future.h"
#include "UObject/StrongObjectPtr.h"
#include "Gameplay/Scrape/MD_ScrapeInitialState.h"
#include "Types/Persistence/MD_DaySnapshotTypes.h"
#include "MD_ScrapeInitializationSubsystem.generated.h"

class UMD_ScrapeSurfaceComponent;
class UMD_ScrapeMeshUVData;

/** Bounded startup preparation; no actor or render resource is mutated by a worker. */
UCLASS()
class MARINEDEMOLITION_API UMD_ScrapeInitializationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    bool Enqueue(UMD_ScrapeSurfaceComponent& Surface);
    bool ConsumePreparedSavedState(UMD_ScrapeSurfaceComponent& Surface, const FMD_WallContaminationRecord& Record);
    bool IsPending() const { return !Queue.IsEmpty() || !Jobs.IsEmpty(); }
private:
    struct FJob
    {
        TWeakObjectPtr<UMD_ScrapeSurfaceComponent> Surface;
        TStrongObjectPtr<UMD_ScrapeMeshUVData> Data;
        TSharedPtr<const FMD_ScrapeCellLayout> Layout;
        TSharedPtr<const FMD_ScrapeCarrierWeights> Weights;
        FMD_ContaminationGeneration Generation;
        uint64 Revision = 0, Epoch = 0;
        TWeakObjectPtr<class UStaticMeshComponent> Mesh;
        TWeakObjectPtr<class UStaticMesh> StaticMesh;
        FVector Scale;
        float Tolerance = 0.f;
        TFuture<FMD_ScrapeInitialState> Result;
        TOptional<FMD_ScrapeInitialState> Prepared;
        TOptional<FMD_WallContaminationRecord> SavedRecord;
        TOptional<FMD_WallContaminationRecord> ExpandedRecord;
        int32 RestoredCells = 0;
        bool bCommitted = false;
    };
    TArray<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>> Queue;
    TArray<TUniquePtr<FJob>> Jobs;
    TMap<TWeakObjectPtr<UMD_ScrapeSurfaceComponent>, FMD_WallContaminationRecord> PreparedSavedStates;
    bool bStopping = false;
};
