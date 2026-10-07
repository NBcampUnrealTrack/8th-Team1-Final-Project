#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "MD_TravelPreloadSubsystem.generated.h"

struct FStreamableHandle;

/** Retains one destination across travel; cancelled/replaced requests cannot grow a map cache. */
UCLASS(Config=Game)
class MARINEDEMOLITION_API UMD_TravelPreloadSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void QueueDestination(const TSoftObjectPtr<UWorld>& Destination);
private:
    bool TickPreparation(float DeltaTime);
    void HandlePostLoadMap(UWorld* World);
    void Release();
    UPROPERTY(Config)
    FSoftObjectPath WarmupMap;
    UPROPERTY(Config)
    TArray<FSoftObjectPath> WarmupAssets;
    FSoftObjectPath DestinationPath;
    TWeakObjectPtr<UWorld> SourceWorld;
    TSharedPtr<FStreamableHandle> Handle;
    FTSTicker::FDelegateHandle Ticker;
    double ReadySince = 0.0;
};
