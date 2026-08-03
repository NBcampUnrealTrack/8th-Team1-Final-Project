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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnScoreChanged, int32, NewScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnDayChanged, int32, NewDay);

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
    
    // 평가 점수 증감. 서버에서만 호출 (붕괴 -200 등)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void AddScoreDelta(int32 Delta);

    UFUNCTION(BlueprintPure, Category = "Score")
    int32 GetScore() const { return Score; }

    UPROPERTY(BlueprintAssignable, Category = "Score")
    FMD_OnScoreChanged OnScoreChanged;

    // 현재 Day (1~4). 서버에서만 변경
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Day")
    void SetCurrentDay(int32 NewDay);

    UFUNCTION(BlueprintPure, Category = "Day")
    int32 GetCurrentDay() const { return CurrentDay; }

    UPROPERTY(BlueprintAssignable, Category = "Day")
    FMD_OnDayChanged OnDayChanged;
    
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

    UPROPERTY(ReplicatedUsing = OnRep_Score)
    int32 Score = 0;
    
    UPROPERTY(ReplicatedUsing = OnRep_CurrentDay)
    int32 CurrentDay = 1;

    UFUNCTION()
    void OnRep_CurrentDay();
    
    UFUNCTION()
    void OnRep_WithdrawalPhase();

    void HandleWithdrawalPhaseChanged();

    UFUNCTION()
    void OnRep_Score();
};