#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MDGameStateBase.generated.h"

UCLASS()
class MARINEDEMOLITION_API AMDGameStateBase : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMDGameStateBase();

protected:
    virtual void BeginPlay() override;

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps
    ) const override;

private:
    UPROPERTY(ReplicatedUsing = OnRep_GameStartServerTime)
    float GameStartServerTime = 0.0f;

    //(8~18)
    UPROPERTY(EditDefaultsOnly, Category = "Game Time")
    float RealDayDurationSeconds = 600.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Game Time")
    int32 StartHour = 8;

    UPROPERTY(EditDefaultsOnly, Category = "Game Time")
    int32 EndHour = 18;

    UPROPERTY(Replicated)
    bool bGameTimeStarted = false;

    UFUNCTION()
    void OnRep_GameStartServerTime();

public:
    void StartGameTime();

    UFUNCTION(BlueprintPure, Category = "Game Time")
    float GetElapsedRealSeconds() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    float GetElapsedGameSeconds() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    int32 GetCurrentHour() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    int32 GetCurrentMinute() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    bool HasReachedTime(int32 Hour, int32 Minute) const;
};