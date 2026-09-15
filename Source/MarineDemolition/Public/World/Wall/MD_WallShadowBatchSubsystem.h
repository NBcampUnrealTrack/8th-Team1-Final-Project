#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MD_WallShadowBatchSubsystem.generated.h"

/** Nearby opaque meshes share raster shadow instances; source geometry and main rendering are retained. */
UCLASS()
class MARINEDEMOLITION_API UMD_WallShadowBatchSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    void InitializeWorldBatches();
    bool IsInitializationPending() const { return !PendingInitialSources.IsEmpty(); }
    bool RegisterWall(UStaticMeshComponent& Source);
    void UnregisterWall(UStaticMeshComponent& Source);
    void UpdateWallTransform(UStaticMeshComponent& Source);
private:
    struct FEntry
    {
        TWeakObjectPtr<UInstancedStaticMeshComponent> Batch;
        FPrimitiveInstanceId Id;
        FDelegateHandle TransformHandle;
        TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
        bool bMaterialsEligible = false;
        bool bOwnsDynamicShadowFlag = false;
    };
    TArray<TWeakObjectPtr<UStaticMeshComponent>> PendingInitialSources;
    int32 NextInitialSource = 0;
    void TickInitialization();
    void RefreshEntry(UStaticMeshComponent& Source, FEntry& Entry);
    void Detach(UStaticMeshComponent* Source, FEntry& Entry);
    void PostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
    FDelegateHandle TickHandle;
    bool bInitialScanComplete = false;
    bool bRendererEligible = false;
    TMap<TWeakObjectPtr<UStaticMeshComponent>, FEntry> Entries;
    UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
};
