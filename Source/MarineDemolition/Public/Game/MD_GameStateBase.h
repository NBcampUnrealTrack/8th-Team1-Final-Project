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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMD_OnScoreChanged, int32, NewScore);
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

    // 월드 준비(쓰레기 생성·계약 동기화) 완료 표시. 복제되어 클라이언트 스포너 평가 게이트로 쓰인다
    void FinishWorldInitialization();
    UFUNCTION(BlueprintPure, Category = "Game Time")
    bool IsWorldInitializationReady() const { return bWorldInitializationReady; }

    // 평가 점수 증감. 서버에서만 호출
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void AddScoreDelta(int32 Delta);

    UFUNCTION(BlueprintPure, Category = "Score")
    int32 GetScore() const { return Score; }

    // 계약 동안 누적된 분류별 점수 원장을 네트워크 전달용 항목 배열로 반환하는 함수
    TArray<FMD_DailyReportValue> GetDailyScoreTotals() const;

    UPROPERTY(BlueprintAssignable, Category = "Score")
    FMD_OnScoreChanged OnScoreChanged;

    // 점수 이벤트 발생. 기본값은 ScoreTable 단가×Count이며, 계산 완료된 점수는 Count를 최종 증감값으로 사용
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score", meta = (AdvancedDisplay = "bUseCountAsDelta"))
    void AddScoreEvent(FGameplayTag EventTag, int32 Count = 1, bool bUseCountAsDelta = false);

    UFUNCTION(BlueprintPure, Category = "Score")
    const UMD_ScoreTableData* GetScoreTable() const { return ScoreTable; }

    // 계약 시작 시점에 고정되는 인원수
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Contract")
    void SetContractCrewCount(int32 Crew);

    UFUNCTION(BlueprintPure, Category = "Contract")
    int32 GetContractCrewCount() const { return ContractCrewCount; }

    // 출발 시 선택한 난이도를 계약 전체에 사용하도록 고정하는 설정
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Score")
    void SetContractDifficulty(EMD_Difficulty Difficulty);

    // HUD가 동일한 계약 난이도를 조회하기 위한 접근 함수
    UFUNCTION(BlueprintPure, Category = "Score")
    EMD_Difficulty GetContractDifficulty() const { return ContractDifficulty; }

protected:
    virtual void BeginPlay() override;

    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutLifetimeProps
    ) const override;

private:
    UPROPERTY(Replicated)
    bool bWorldInitializationReady = false;

    UPROPERTY(ReplicatedUsing = OnRep_Score)
    int32 Score = 0;

    // 단가표. BP GameState 기본값에서 지정
    UPROPERTY(EditDefaultsOnly, Category = "Score")
    TObjectPtr<UMD_ScoreTableData> ScoreTable;

    // 서버 원장 로그. 정산 화면 연결 전까지 복제하지 않음
    UPROPERTY(Transient)
    TArray<FMD_ScoreEntry> ScoreLog;

    // 계약 동안 점수를 정산 분류별로 누적하는 서버 Map 원장
    TMap<EMD_DailyReportCategory, int64> DailyScoreTotals;

    // 계약 인원수 (0 = 미확정). 계약 시작 시점 고정
    UPROPERTY(Replicated)
    int32 ContractCrewCount = 0;

    // 출발 시 서버가 확정하는 계약 난이도
    UPROPERTY(Replicated)
    EMD_Difficulty ContractDifficulty = EMD_Difficulty::Standard;

    // 점수 이벤트 태그를 공통 정산 Enum으로 변환하는 함수
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

public:
    // PlayerController 응답 RPC가 조회하는 서버 계약 원장 합계
    UPROPERTY(Transient)
    FMD_InspectionReport DailyLedger;

    // 활성 보고서가 구독할 원장 갱신 이벤트
    FMD_OnDailyLedgerChanged OnDailyLedgerChanged;

    // 서버 점수와 크레딧 원장을 응답용 보고서에 반영
    void RefreshDailyLedger();

    // 응답 RPC 구성에 사용할 서버 계약 보고서 조회
    FORCEINLINE FMD_InspectionReport& GetDailyLedger() { return DailyLedger; }
};
