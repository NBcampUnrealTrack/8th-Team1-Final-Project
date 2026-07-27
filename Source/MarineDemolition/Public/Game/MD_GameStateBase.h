#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MD_GameStateBase.generated.h"

UENUM(BlueprintType)
enum class EMD_WithdrawalPhase : uint8
{
    None      UMETA(DisplayName = "None"),
    Notice    UMETA(DisplayName = "Notice"),
    Warning   UMETA(DisplayName = "Warning"),
    Departed  UMETA(DisplayName = "Departed")
};

UCLASS()
class MARINEDEMOLITION_API AMD_GameStateBase : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMD_GameStateBase();

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

    /** 0..1 progress across the playable day window (StartHour..EndHour). */
    UFUNCTION(BlueprintPure, Category = "Game Time")
    float GetDayCycleProgress() const;

    /** Current in-game time as hours (fractional), e.g. 14.5 = 14:30. */
    UFUNCTION(BlueprintPure, Category = "Game Time")
    float GetTimeOfDayHours() const;

    // Withdrawal
    void SetWithdrawalPhase(EMD_WithdrawalPhase NewPhase);

    UFUNCTION(BlueprintPure, Category = "Withdrawal")
    EMD_WithdrawalPhase GetWithdrawalPhase() const;

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
    EMD_WithdrawalPhase WithdrawalPhase =
        EMD_WithdrawalPhase::None;

    UFUNCTION()
    void OnRep_WithdrawalPhase();

    void HandleWithdrawalPhaseChanged();

};