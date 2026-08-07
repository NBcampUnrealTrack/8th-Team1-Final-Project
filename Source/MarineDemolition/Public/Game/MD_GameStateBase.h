#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagContainer.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_GameStateBase.generated.h"

class UMD_ScoreTableData;
class UMD_MerchantComponent;

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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnInspectionReport, const FMD_InspectionReport&, Report);

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

    // 점수 이벤트 발생. 단가는 ScoreTable에서 조회, Count는 건수 (미복귀 2인 등)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void AddScoreEvent(FGameplayTag EventTag, int32 Count = 1);

    // 샘플 점수 이벤트 발생 (등급×상태 단가)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void AddSampleScoreEvent(EMD_SampleGrade Grade, EMD_SampleScoreType Type);

    // 해당 Day 점검 기준. 경고 상태면 배율 반영 (HUD 표시 공용)
    UFUNCTION(BlueprintPure, Category = "Score")
    int32 GetInspectionThreshold(int32 Day) const;

    // 점검 경고 상태. 서버에서만 변경, 스냅샷으로 Day 간 유지
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void SetInspectionWarned(bool bWarned);

    UFUNCTION(BlueprintPure, Category = "Score")
    bool IsInspectionWarned() const { return bInspectionWarned; }

    UFUNCTION(BlueprintPure, Category = "Score")
    const UMD_ScoreTableData* GetScoreTable() const { return ScoreTable; }

    // 점검 결과 기록 + 전 클라이언트 통지. 서버 EvaluateInspection이 호출
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void SetInspectionReport(const FMD_InspectionReport& Report);

    UFUNCTION(BlueprintPure, Category = "Score")
    const FMD_InspectionReport& GetInspectionReport() const { return LastInspectionReport; }

    UPROPERTY(BlueprintAssignable, Category = "Score")
    FMD_OnInspectionReport OnInspectionReport;
    
    // 현재 Day (1~4). 서버에서만 변경
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Day")
    void SetCurrentDay(int32 NewDay);

    UFUNCTION(BlueprintPure, Category = "Day")
    int32 GetCurrentDay() const { return CurrentDay; }

    UPROPERTY(BlueprintAssignable, Category = "Day")
    FMD_OnDayChanged OnDayChanged;

    // 계약 시작 시점에 고정되는 인원수 (점검 기준 인원 배율용)
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Day")
    void SetContractCrewCount(int32 Crew);

    UFUNCTION(BlueprintPure, Category = "Day")
    int32 GetContractCrewCount() const { return ContractCrewCount; }

    // 비상 4일차 진행 여부. Day 간 스냅샷으로 유지
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Day")
    void SetEmergencyDay(bool bEmergency);

    UFUNCTION(BlueprintPure, Category = "Day")
    bool IsEmergencyDay() const { return bEmergencyDay; }

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

    // 단가표. BP GameState 기본값에서 지정
    UPROPERTY(EditDefaultsOnly, Category = "Score")
    TObjectPtr<UMD_ScoreTableData> ScoreTable;

    // 점검 경고 상태. 다음 점검 기준 ×1.1
    UPROPERTY(Replicated)
    bool bInspectionWarned = false;

    // 서버 원장 로그. 정산 화면 연결 전까지 복제하지 않음
    UPROPERTY(Transient)
    TArray<FMD_ScoreEntry> ScoreLog;

    UPROPERTY(ReplicatedUsing = OnRep_InspectionReport)
    FMD_InspectionReport LastInspectionReport;

    UFUNCTION()
    void OnRep_InspectionReport();

    UPROPERTY(ReplicatedUsing = OnRep_CurrentDay)
    int32 CurrentDay = 1;

    // 계약 인원수 (0 = 미확정). 계약 시작 시점 고정
    UPROPERTY(Replicated)
    int32 ContractCrewCount = 0;

    // 비상 4일차 여부
    UPROPERTY(Replicated)
    bool bEmergencyDay = false;

    UFUNCTION()
    void OnRep_CurrentDay();
    
    UFUNCTION()
    void OnRep_WithdrawalPhase();

    void HandleWithdrawalPhaseChanged();

    UFUNCTION()
    void OnRep_Score();

public:
    UFUNCTION(BlueprintPure, Category = "MD|Merchant")
    UMD_MerchantComponent* GetMerchantComponent() const { return MerchantComponent; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Merchant")
    TObjectPtr<UMD_MerchantComponent> MerchantComponent;
};