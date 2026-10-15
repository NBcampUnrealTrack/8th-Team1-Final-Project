#include "Game/MD_GameStateBase.h"
#include "Game/MD_GameModeBase.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Component/Lore/MD_LoreComponent.h"
#include "Component/Facility/MD_FacilityFaultManagerComponent.h"
#include "Data/Score/MD_ScoreTableData.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

AMD_GameStateBase::AMD_GameStateBase()
{
    bReplicates = true;

    LoreComponent = CreateDefaultSubobject<UMD_LoreComponent>(TEXT("LoreComponent"));
    MerchantComponent = CreateDefaultSubobject<UMD_MerchantComponent>(TEXT("MerchantComponent"));
    FacilityFaultManager = CreateDefaultSubobject<UMD_FacilityFaultManagerComponent>(TEXT("FacilityFaultManager"));
}

void AMD_GameStateBase::AddPlayerState(APlayerState* PlayerState)
{
    // 실제 배열 변경 여부 확인을 위한 추가 전 플레이어 수 저장
    int32 PreviousPlayerCount = PlayerArray.Num();

    Super::AddPlayerState(PlayerState);

    // 중복 또는 비활성 PlayerState로 배열 개수가 유지된 경우 목록 갱신 생략
    if (PlayerArray.Num() == PreviousPlayerCount)
    {
        return;
    }

    // 새 접속 플레이어가 반영된 PlayerArray 변경 알림
    OnPlayerListChanged.Broadcast();
}

void AMD_GameStateBase::RemovePlayerState(APlayerState* PlayerState)
{
    // 실제 배열 변경 여부 확인을 위한 제거 전 플레이어 수 저장
    int32 PreviousPlayerCount = PlayerArray.Num();

    Super::RemovePlayerState(PlayerState);

    // 대상이 없어 배열 개수가 유지된 경우 목록 갱신 생략
    if (PlayerArray.Num() == PreviousPlayerCount)
    {
        return;
    }

    // 플레이어 퇴장이 반영된 PlayerArray 변경 알림
    OnPlayerListChanged.Broadcast();
}

void AMD_GameStateBase::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        if (!GetWorld()->GetAuthGameMode<AMD_GameModeBase>()) FinishWorldInitialization();
        // 초기 착수금과 계약 보고서 구성
        RefreshDailyLedger();
    }
}

void AMD_GameStateBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 월드 이동 및 게임 종료 이후 원장 갱신 예약 정리
    GetWorldTimerManager().ClearTimer(DailyLedgerRefreshTimer);
    Super::EndPlay(EndPlayReason);
}

void AMD_GameStateBase::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMD_GameStateBase, bWorldInitializationReady);

    DOREPLIFETIME(AMD_GameStateBase, Score);

    DOREPLIFETIME(AMD_GameStateBase, ContractCrewCount);

    DOREPLIFETIME(AMD_GameStateBase, ContractDifficulty);
}

//Timer
void AMD_GameStateBase::FinishWorldInitialization()
{
    if (!HasAuthority() || bWorldInitializationReady) return;
    bWorldInitializationReady = true;
    ForceNetUpdate();
}

// Withdrawal
void AMD_GameStateBase::AddScoreDelta(int32 Delta)
{
    if (HasAuthority() == false)
    {
        return;
    }

    // 원인이 지정되지 않은 직접 점수 변경은 사고 분류의 공통 기록 경로로 처리
    RecordScore(EMD_DailyReportCategory::Accident, FGameplayTag(), Delta);
}

TArray<FMD_DailyReportValue> AMD_GameStateBase::GetDailyScoreTotals() const
{
    // 서버 Map을 RPC와 복제가 지원하는 항목 배열로 변환
    TArray<FMD_DailyReportValue> OutScoreTotals;
    OutScoreTotals.Reserve(DailyScoreTotals.Num());
    for (const TPair<EMD_DailyReportCategory, int64>& ScoreEntry : DailyScoreTotals)
    {
        int32 ScoreValue = static_cast<int32>(FMath::Clamp<int64>(ScoreEntry.Value, MIN_int32, MAX_int32));
        OutScoreTotals.Add({ScoreEntry.Key, ScoreValue});
    }

    return OutScoreTotals;
}

void AMD_GameStateBase::AddScoreEvent(FGameplayTag EventTag, int32 Count, bool bUseCountAsDelta)
{
    if (HasAuthority() == false || Count == 0)
    {
        return;
    }

    // QuestDefinition처럼 외부 데이터에서 계산된 점수와 ScoreTable 단가 기반 점수를 동일한 원장 경로로 처리하기 위한 분기
    int32 Delta = Count;
    if (bUseCountAsDelta == false)
    {
        if (IsValid(ScoreTable) == false)
        {
            return;
        }

        Delta = ScoreTable->GetEventValue(EventTag) * Count;
        if (Delta == 0)
        {
            return;
        }
    }

    // 태그를 Enum 분류로 변환하여 점수·로그·Map을 한 번에 기록
    EMD_DailyReportCategory Category = ResolveScoreCategory(EventTag);
    RecordScore(Category, EventTag, Delta);
}

EMD_DailyReportCategory AMD_GameStateBase::ResolveScoreCategory(FGameplayTag EventTag) const
{
    // GameplayTag를 한 번만 문자열로 변환하여 정산 분류를 결정
    FString EventName = EventTag.ToString();
    bool bIsCategorizing = EventName.StartsWith(TEXT("Score.Sorting"));
    if (bIsCategorizing == false)
    {
        bIsCategorizing = EventName.StartsWith(TEXT("Score.Penalty.Missort"));
    }
    if (bIsCategorizing == false)
    {
        bIsCategorizing = EventName == TEXT("Score.Penalty.UnsortedRemain");
    }
    if (bIsCategorizing == false)
    {
        bIsCategorizing = EventName == TEXT("Score.Penalty.UncollectedRemain");
    }
    if (bIsCategorizing == true)
    {
        return EMD_DailyReportCategory::Categorizing;
    }

    // 복귀 성공과 실패 태그를 복귀 분류로 변환
    bool bIsReturn = EventName == TEXT("Score.Daily.AllReturned");
    if (bIsReturn == false)
    {
        bIsReturn = EventName == TEXT("Score.Penalty.NotReturned");
    }
    if (bIsReturn == true)
    {
        return EMD_DailyReportCategory::Return;
    }

    // 퀘스트 성공과 실패 태그를 퀘스트 분류로 변환
    bool bIsQuest = EventName == TEXT("Score.Daily.Quest");
    if (bIsQuest == false)
    {
        bIsQuest = EventName == TEXT("Score.Daily.QuestS");
    }
    if (bIsQuest == true)
    {
        return EMD_DailyReportCategory::Quest;
    }

    // 나머지 철거·사고·경고 태그는 사고 분류로 처리
    return EMD_DailyReportCategory::Accident;
}

void AMD_GameStateBase::RecordScore(EMD_DailyReportCategory Category, FGameplayTag EventTag, int32 Delta)
{
    if (Delta == 0)
    {
        return;
    }

    // 원인 추적을 위한 로그 유지
    ScoreLog.Add({EventTag, Delta});

    DailyScoreTotals.FindOrAdd(Category) += Delta;

    // 누적 점수와 리슨 서버 UI를 동일한 기록 경로에서 갱신
    Score += Delta;
    OnRep_Score();
    // 점수 증감을 열린 보고서와 클라이언트에 반영
    RefreshDailyLedger();
}

void AMD_GameStateBase::SetContractCrewCount(int32 Crew)
{
    if (HasAuthority())
    {
        ContractCrewCount = Crew;
    }
}

void AMD_GameStateBase::SetContractDifficulty(EMD_Difficulty Difficulty)
{
    if (HasAuthority() == false)
    {
        return;
    }

    // C++ 또는 Blueprint에서 전달된 열거형 값도 정식 난이도 범위로 제한하기 위한 검사
    if (Difficulty < EMD_Difficulty::Safe || Difficulty > EMD_Difficulty::Dangerous)
    {
        return;
    }

    ContractDifficulty = Difficulty;
    ForceNetUpdate();
}

void AMD_GameStateBase::OnRep_Score()
{
    OnScoreChanged.Broadcast(Score);
}

void AMD_GameStateBase::UpdateDailyLedgerRefreshTimer()
{
    // 클라이언트의 별도 시간 계산과 중복 서버 예약 방지
    if (HasAuthority() == false)
    {
        // 서버가 전달하는 시간축 사용
        return;
    }
    // 같은 월드의 여러 보고서가 하나의 갱신 예약 공유
    FTimerManager& TimerManager = GetWorldTimerManager();
    // 마지막 보고서가 닫히면 주기적인 원장 집계 중단
    if (OnDailyLedgerChanged.IsBound() == false)
    {
        // 다음 화면 활성화까지 경과 시간 갱신 예약 해제
        TimerManager.ClearTimer(DailyLedgerRefreshTimer);
    }
    // 추가 플레이어의 보고서 활성화로 기존 주기가 밀리지 않는 처리
    else if (TimerManager.IsTimerActive(DailyLedgerRefreshTimer) == false)
    {
        // 거래가 없는 동안에도 시작 영점부터 현재까지의 시간축 갱신
        TimerManager.SetTimer(DailyLedgerRefreshTimer, this, &AMD_GameStateBase::RefreshDailyLedger, 1.0f, true);
    }
}

void AMD_GameStateBase::RefreshDailyLedger()
{
    // 서버에서만 원장 응답 데이터 구성
    if (HasAuthority() == false)
    {
        return;
    }
    FMD_InspectionReport Report;
    // 마감 이전에도 조회 가능한 원장임을 표시
    Report.bValid = true;
    // 계약 누적 점수
    Report.Score = Score;
    Report.CumulativeScore = Score;
    Report.DailyScore = Score;
    Report.Credits = 0;
    // 계약 종료로 현장 참조가 해제되어도 마지막 보고서의 현장명 유지
    Report.SiteName = DailyLedger.SiteName;
    // 분류별 점수 원장 조회
    Report.DailyScoreTotals = GetDailyScoreTotals();
    // 현재 월드의 서버 상인 원장 조회
    UGameInstance* GameInstance = GetGameInstance();
    if (IsValid(GameInstance) == true)
    {
        UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
        // 계약 종료 후 보존된 기록도 보고서 표시 시점과 무관하게 반영
        if (IsValid(Merchant) == true)
        {
            Report.Credits = Merchant->GetCredits();
            // 같은 서버 원장의 네 개인 슬롯과 여섯 시점 및 공용 금액 반영
            Merchant->BuildCreditHistory(Report);
            UMD_SiteDefinition* Site = Merchant->GetCurrentSite();
            if (IsValid(Site) == true)
            {
                Report.SiteName = Site->SiteName;
            }
        }
    }
    // 값이 같으면 구독 중인 모든 클라이언트의 중복 응답 전송 생략
    if (FMD_InspectionReport::StaticStruct()->CompareScriptStruct(&DailyLedger, &Report, 0) == true)
    {
        return;
    }
    DailyLedger = MoveTemp(Report);
    // 변경된 원장만 구독 컨트롤러에 알림
    OnDailyLedgerChanged.Broadcast();
}
