#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "MD_MapRegistrationSubsystem.generated.h"

class AActor;
class UActorComponent;
class UStaticMeshComponent;

// Register dense native static-mesh maps before GameMode releases actor BeginPlay.
// Components and actor identities stay unchanged; only initial registration is scheduled.
UCLASS()
class MARINEDEMOLITION_API UMD_MapRegistrationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void PostInitialize() override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    bool IsPending() const { return NextActor < Actors.Num(); }
    bool IsAwaitingHandoffFrame() const { return IsPending() && bWaitForFirstBatch; }
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    void CollectDeferredActors();
    struct FDeferredActor
    {
        TWeakObjectPtr<AActor> Actor;
        TWeakObjectPtr<UStaticMeshComponent> Mesh;
        bool bIgnoredStreaming = false;
        bool bPolicyRestored = false;
        TArray<TWeakObjectPtr<UActorComponent>> Components;
    };
    // The level owns actors/components; weak references permit cancellation and teardown.
    TArray<FDeferredActor> Actors;
    int32 NextActor = 0;
    bool bWaitForFirstBatch = true;
    FTSTicker::FDelegateHandle Ticker;
    FDelegateHandle FrameStartHandle;
    double FrameStartSeconds = 0.0;
    int32 DeferredFrameCount = 0;
    void RefreshLoadingScreen();
    void BeginRegistrationFrame();
    void StopScheduling();
    bool TickRegistration(float DeltaTime);
    void FinishRegistration();
    void RestorePhysics();
    bool bHoldingPhysics = false;
    bool bPreviousSimulatePhysics = false;
#if WITH_EDITOR
    bool bPreviousTraceCollision = false;
#endif
    static void RestoreAutoRegistration(FDeferredActor& Entry);
};
