#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagContainer.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "MD_GameStateBase.generated.h"

class UMD_ScoreTableData;
class UMD_MerchantComponent;
class UMD_LoreComponent;
struct FMD_GameStateData;

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
// 접속 플레이어 구성 또는 표시 이름 변경을 활성 UI에 전달하는 네이티브 이벤트 선언
DECLARE_MULTICAST_DELEGATE(FMD_OnPlayerListChanged);
// 서버 원장 변경을 구독 컨트롤러에 전달하는 이벤트
DECLARE_MULTICAST_DELEGATE(FMD_OnDailyLedgerChanged);

UCLASS()
class MARINEDEMOLITION_API AMD_GameStateBase : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMD_GameStateBase();

    virtual void AddPlayerState(APlayerState* PlayerState) override;

    virtual void RemovePlayerState(APlayerState* PlayerState) override;

    // 활성화된 플레이어 목록 UI가 구독하는 변경 이벤트 저장
    FMD_OnPlayerListChanged OnPlayerListChanged;

    //Timer
    void StartGameTime();
    void FinishWorldInitialization();
    UFUNCTION(BlueprintPure, Category = "Game Time")
    bool IsWorldInitializationReady() const { return bWorldInitializationReady; }

    // 현재 일자의 인게임 시각을 지정한 시의 정각으로 설정
    bool SetGameTimeHour(int32 Hour);

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

    // 서버 점수 원장에서 지정 Day에 발생한 증감만 합산하여 일일 보고서에 제공하는 함수
    int32 GetScoreForDay(int32 Day) const;

    // 현재 Day의 Enum Map 점수 원장을 네트워크 전달용 항목 배열로 반환하는 함수
    TArray<FMD_DailyReportValue> GetDailyScoreTotals() const;

    UPROPERTY(BlueprintAssignable, Category = "Score")
    FMD_OnScoreChanged OnScoreChanged;

    // 점수 이벤트 발생. 기본값은 ScoreTable 단가×Count이며, 계산 완료된 점수는 Count를 최종 증감값으로 사용
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score", meta = (AdvancedDisplay = "bUseCountAsDelta"))
    void AddScoreEvent(FGameplayTag EventTag, int32 Count = 1, bool bUseCountAsDelta = false);

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

    // 출발 시 선택한 난이도를 계약 전체 점검 기준에 사용하도록 고정하는 설정
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void SetContractDifficulty(EMD_Difficulty Difficulty);

    // HUD와 점검 판정이 동일한 계약 난이도를 조회하기 위한 접근 함수
    UFUNCTION(BlueprintPure, Category = "Score")
    EMD_Difficulty GetContractDifficulty() const { return ContractDifficulty; }

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
    UPROPERTY(Replicated)
    bool bWorldInitializationReady = false;
    UPROPERTY(Replicated)
    double PendingElapsedRealSeconds = 0.0;
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

    // 현재 Day 점수를 정산 분류별로 누적하는 서버 Map 원장
    TMap<EMD_DailyReportCategory, int64> DailyScoreTotals;

    UPROPERTY(ReplicatedUsing = OnRep_InspectionReport)
    FMD_InspectionReport LastInspectionReport;

    UFUNCTION()
    void OnRep_InspectionReport();

    UPROPERTY(ReplicatedUsing = OnRep_CurrentDay)
    int32 CurrentDay = 1;

    // 계약 인원수 (0 = 미확정). 계약 시작 시점 고정
    UPROPERTY(Replicated)
    int32 ContractCrewCount = 0;

    // 출발 시 서버가 확정하고 Day 전환과 저장 이후에도 유지하는 계약 난이도
    UPROPERTY(Replicated)
    EMD_Difficulty ContractDifficulty = EMD_Difficulty::Standard;

    // 비상 4일차 여부
    UPROPERTY(Replicated)
    bool bEmergencyDay = false;

    UFUNCTION()
    void OnRep_CurrentDay();
    
    UFUNCTION()
    void OnRep_WithdrawalPhase();

    void HandleWithdrawalPhaseChanged();

    // 점수 이벤트 태그를 공통 일일 정산 Enum으로 변환하는 함수
    EMD_DailyReportCategory ResolveScoreCategory(FGameplayTag EventTag) const;

    // 점수 변경과 로그 및 Enum Map 원장을 한 경로에서 기록하는 함수
    void RecordScore(EMD_DailyReportCategory Category, FGameplayTag EventTag, int32 Delta);

    UFUNCTION()
    void OnRep_Score();

public:
    // 로어 진행을 조회하기 위한 접근 함수
    UFUNCTION(BlueprintPure, Category = "MD|Lore")
    UMD_LoreComponent* GetLoreComponent() const { return LoreComponent; }

    UFUNCTION(BlueprintPure, Category = "MD|Merchant")
    UMD_MerchantComponent* GetMerchantComponent() const { return MerchantComponent; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Lore")
    TObjectPtr<UMD_LoreComponent> LoreComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Merchant")
    TObjectPtr<UMD_MerchantComponent> MerchantComponent;

    // 현재 서버 권위 점수·일차·시간 상태를 저장 필드로 복사하는 함수
    FMD_GameStateData GetGameStateSaveData() const;

    // 저장 필드를 변경 없이 검사하여 다른 서브시스템과의 원자적 복원 순서를 보장하는 함수
    bool CanApplyGameStateSaveData(const FMD_GameStateData& SaveData) const;

    // 검증된 저장 필드를 복제 GameState와 리슨 서버 알림에 적용하는 함수
    bool ApplyGameStateSaveData(const FMD_GameStateData& SaveData);
public:
    // PlayerController 응답 RPC가 조회하는 서버 당일 원장 합계
    UPROPERTY(Transient)
    FMD_InspectionReport DailyLedger;

    // 활성 보고서가 구독할 원장 갱신 이벤트
    FMD_OnDailyLedgerChanged OnDailyLedgerChanged;

    // 서버 점수와 크레딧 원장을 응답용 보고서에 반영
    void RefreshDailyLedger();

    // 응답 RPC 구성에 사용할 서버 당일 보고서 조회
    FORCEINLINE FMD_InspectionReport& GetDailyLedger() { return DailyLedger; }
};
