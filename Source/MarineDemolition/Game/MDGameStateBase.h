#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MDGameStateBase.generated.h"

UENUM(BlueprintType)
enum class EMDWithdrawalPhase : uint8
{
    None      UMETA(DisplayName = "None"),
    Notice    UMETA(DisplayName = "Notice"),
    Warning   UMETA(DisplayName = "Warning"),
    Departed  UMETA(DisplayName = "Departed")
};

UCLASS()
class MARINEDEMOLITION_API AMDGameStateBase : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMDGameStateBase();

    //Timer
    void StartGameTime();

    UFUNCTION(BlueprintPure, Category = "Game Time")
    double GetElapsedRealSeconds() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    double GetElapsedGameSeconds() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    int32 GetCurrentHour() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    int32 GetCurrentMinute() const;

    UFUNCTION(BlueprintPure, Category = "Game Time")
    bool HasReachedTime(int32 Hour, int32 Minute) const;

    // Withdrawal
    void SetWithdrawalPhase(EMDWithdrawalPhase NewPhase);

    UFUNCTION(BlueprintPure, Category = "Withdrawal")
    EMDWithdrawalPhase GetWithdrawalPhase() const;

protected:
    virtual void BeginPlay() override;

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps
    ) const override;

private:
    //Timer
    UPROPERTY(Replicated)
    double GameStartServerTime = 0.0;

    UPROPERTY(EditDefaultsOnly, Category = "Game Time")
    double RealDayDurationSeconds = 600.0;

    UPROPERTY(EditDefaultsOnly, Category = "Game Time")
    int32 StartHour = 8;

    UPROPERTY(EditDefaultsOnly, Category = "Game Time")
    int32 EndHour = 18;

    UPROPERTY(Replicated)
    bool bGameTimeStarted = false;

    // Withdrawal
    UPROPERTY(ReplicatedUsing = OnRep_WithdrawalPhase)
    EMDWithdrawalPhase WithdrawalPhase =
        EMDWithdrawalPhase::None;

    UFUNCTION()
    void OnRep_WithdrawalPhase();

    void HandleWithdrawalPhaseChanged();

};