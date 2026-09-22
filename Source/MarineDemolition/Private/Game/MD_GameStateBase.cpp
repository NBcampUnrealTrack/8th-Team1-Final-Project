#include "Game/MD_GameStateBase.h"
#include "Game/MD_GameModeBase.h"
#include "Component/Lore/MD_LoreComponent.h"
#include "Data/Score/MD_ScoreTableData.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Engine/GameInstance.h"

AMD_GameStateBase::AMD_GameStateBase()
{
    bReplicates = true;

    LoreComponent = CreateDefaultSubobject<UMD_LoreComponent>(TEXT("LoreComponent"));
    MerchantComponent = CreateDefaultSubobject<UMD_MerchantComponent>(TEXT("MerchantComponent"));
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
        // 초기 착수금과 현재 일차의 보고서 구성
        RefreshDailyLedger();
    }
}

void AMD_GameStateBase::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMD_GameStateBase, bWorldInitializationReady);
    DOREPLIFETIME(AMD_GameStateBase, PendingElapsedRealSeconds);
    DOREPLIFETIME(AMD_GameStateBase, GameStartServerTime);
    DOREPLIFETIME(AMD_GameStateBase, bGameTimeStarted);
    DOREPLIFETIME(AMD_GameStateBase, WithdrawalPhase);
    
    DOREPLIFETIME(AMD_GameStateBase, Score);

    DOREPLIFETIME(AMD_GameStateBase, bInspectionWarned);

    DOREPLIFETIME(AMD_GameStateBase, LastInspectionReport);

    DOREPLIFETIME(AMD_GameStateBase, CurrentDay);

    DOREPLIFETIME(AMD_GameStateBase, ContractCrewCount);

    DOREPLIFETIME(AMD_GameStateBase, ContractDifficulty);

    DOREPLIFETIME(AMD_GameStateBase, bEmergencyDay);
}

//Timer
void AMD_GameStateBase::StartGameTime()
{
    if (!HasAuthority())
    {
        return;
    }

    PendingElapsedRealSeconds = 0.0;
    if (!bWorldInitializationReady) return;
    GameStartServerTime = GetServerWorldTimeSeconds();
    bGameTimeStarted = true;

    ForceNetUpdate();
}

void AMD_GameStateBase::FinishWorldInitialization()
{
    if (!HasAuthority() || bWorldInitializationReady) return;
    GameStartServerTime = GetServerWorldTimeSeconds() - PendingElapsedRealSeconds;
    bGameTimeStarted = true;
    bWorldInitializationReady = true;
    ForceNetUpdate();
}

double AMD_GameStateBase::GetElapsedRealSeconds() const
{
    if (!bWorldInitializationReady) return PendingElapsedRealSeconds;
    if (!bGameTimeStarted)
    {
        return 0.0;
    }

    return FMath::Max(
        GetServerWorldTimeSeconds() - GameStartServerTime,
        0.0
    );
}

double AMD_GameStateBase::GetElapsedGameSeconds() const
{
    const double TotalGameSeconds =(EndHour - StartHour) * 60 * 60;

    if (RealDayDurationSeconds <= 0.0f)
    {
        return 0.0;
    }

    //Real: 1sec, InGame: 60sec
    const double TimeScale =
        TotalGameSeconds / RealDayDurationSeconds;

    return FMath::Clamp(
        GetElapsedRealSeconds() * TimeScale,
        0.0,
        TotalGameSeconds
    );
}

int32 AMD_GameStateBase::GetCurrentHour() const
{
    const int32 ElapsedMinutes =
        FMath::FloorToInt(GetElapsedGameSeconds() / 60.0);

    return StartHour + ElapsedMinutes / 60;
}

int32 AMD_GameStateBase::GetCurrentMinute() const
{
    const int32 ElapsedMinutes =
        FMath::FloorToInt(GetElapsedGameSeconds() / 60.0);

    return ElapsedMinutes % 60;
}

bool AMD_GameStateBase::HasReachedTime(
    int32 Hour,
    int32 Minute
) const
{
    const int32 CurrentTotalMinutes =
        GetCurrentHour() * 60 + GetCurrentMinute();

    const int32 TargetTotalMinutes =
        Hour * 60 + Minute;

    return CurrentTotalMinutes >= TargetTotalMinutes;
}

float AMD_GameStateBase::GetTimeOfDayHours() const
{
    return static_cast<float>(GetCurrentHour())
        + static_cast<float>(GetCurrentMinute()) / 60.f;
}

float AMD_GameStateBase::GetDayCycleProgress() const
{
    const int32 DayLengthHours = EndHour - StartHour;
    if (DayLengthHours <= 0)
    {
        return 0.f;
    }

    const float ElapsedHours = static_cast<float>(GetElapsedGameSeconds()) / 3600.f;
    return FMath::Clamp(ElapsedHours / static_cast<float>(DayLengthHours), 0.f, 1.f);
}

// Withdrawal
void AMD_GameStateBase::SetWithdrawalPhase(
    EMD_WithdrawalPhase NewPhase
)
{
    if (!HasAuthority())
    {
        return;
    }

    if (WithdrawalPhase == NewPhase)
    {
        return;
    }

    WithdrawalPhase = NewPhase;

    HandleWithdrawalPhaseChanged();

    ForceNetUpdate();
}

EMD_WithdrawalPhase
AMD_GameStateBase::GetWithdrawalPhase() const
{
    return WithdrawalPhase;
}

void AMD_GameStateBase::AddScoreDelta(int32 Delta)
{
    if (HasAuthority() == false)
    {
        return;
    }

    // 원인이 지정되지 않은 직접 점수 변경은 사고 분류의 공통 기록 경로로 처리
    RecordScore(EMD_DailyReportCategory::Accident, FGameplayTag(), Delta);
}

int32 AMD_GameStateBase::GetScoreForDay(int32 Day) const
{
    // 저장된 점수 이벤트 합계가 표시 정수 범위를 넘어도 오버플로하지 않는 누적값
    int64 OutScore = 0;
    for (const FMD_ScoreEntry& ScoreEntry : ScoreLog)
    {
        if (ScoreEntry.Day == Day)
        {
            OutScore += ScoreEntry.Delta;
        }
    }

    // 원본 로그를 유지하면서 UI 반환형 범위에서만 합계 제한
    return static_cast<int32>(FMath::Clamp<int64>(OutScore, MIN_int32, MAX_int32));
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
    if (bIsQuest == false)
    {
        bIsQuest = EventName == TEXT("Score.Penalty.Quest");
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

    // 일차별 총점 계산과 원인 추적을 위한 기존 로그 유지
    ScoreLog.Add({EventTag, Delta, CurrentDay});

    DailyScoreTotals.FindOrAdd(Category) += Delta;

    // 누적 점수와 리슨 서버 UI를 동일한 기록 경로에서 갱신
    Score += Delta;
    OnRep_Score();
    // 당일 점수 증감을 열린 보고서와 클라이언트에 반영
    RefreshDailyLedger();
}

int32 AMD_GameStateBase::GetInspectionThreshold(int32 Day) const
{
    if (!IsValid(ScoreTable))
    {
        return 0;
    }

    const int32 BaseThreshold = ScoreTable->GetDayThreshold(Day);
    if (BaseThreshold <= 0)
    {
        return 0;
    }

    // 최종 기준 = 표준 × 난이도 × 인원 × (경고 시 1.1)
    float Multiplier = MDTravel::GetDifficultyMultiplier(ContractDifficulty);
    if (ContractCrewCount > 0)
    {
        Multiplier *= ScoreTable->GetCrewMultiplier(ContractCrewCount);
    }
    if (bInspectionWarned)
    {
        Multiplier *= ScoreTable->WarnedThresholdMultiplier;
    }

    return FMath::RoundToInt(BaseThreshold * Multiplier);
}

void AMD_GameStateBase::SetInspectionWarned(bool bWarned)
{
    if (HasAuthority())
    {
        bInspectionWarned = bWarned;
    }
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
    if (Difficulty < EMD_Difficulty::Safe || Difficulty > EMD_Difficulty::Extreme)
    {
        return;
    }

    ContractDifficulty = Difficulty;
    ForceNetUpdate();
}

void AMD_GameStateBase::SetEmergencyDay(bool bEmergency)
{
    if (HasAuthority())
    {
        bEmergencyDay = bEmergency;
    }
}

void AMD_GameStateBase::SetInspectionReport(const FMD_InspectionReport& Report)
{
    if (!HasAuthority())
    {
        return;
    }

    LastInspectionReport = Report;
    // 확정된 점검 결과와 당일 원장의 일치 보장
    RefreshDailyLedger();
    // 리슨 서버는 OnRep이 안 오므로 직접 방송
    OnRep_InspectionReport();
}

void AMD_GameStateBase::OnRep_InspectionReport()
{
    if (LastInspectionReport.bValid)
    {
        OnInspectionReport.Broadcast(LastInspectionReport);
    }
}

void AMD_GameStateBase::SetCurrentDay(int32 NewDay)
{
    // 실제 Day가 변경될 때만 이전 Day의 상세 Enum Map을 초기화
    if (CurrentDay != NewDay)
    {
        DailyScoreTotals.Reset();
    }
    CurrentDay = NewDay;
    // 이전 일자의 원장을 새 보고서에 표시하지 않도록 갱신
    RefreshDailyLedger();
    // 리슨 서버 호스트는 OnRep이 안 오므로 직접 방송
    OnRep_CurrentDay();
}

void AMD_GameStateBase::OnRep_CurrentDay()
{
    OnDayChanged.Broadcast(CurrentDay);
}

void AMD_GameStateBase::OnRep_Score()
{
    OnScoreChanged.Broadcast(Score);
}

FMD_GameStateData AMD_GameStateBase::GetGameStateSaveData() const
{
    // 하나의 수집 시점에 속한 복제 진행 상태만 슬롯에 기록하기 위한 값 복사
    FMD_GameStateData SaveData;
    SaveData.bEmergencyDay = bEmergencyDay;
    SaveData.bInspectionWarned = bInspectionWarned;
    SaveData.ContractCrewCount = ContractCrewCount;
    SaveData.ContractDifficulty = ContractDifficulty;
    SaveData.ElapsedDay = CurrentDay;
    SaveData.ElapsedSeconds = GetElapsedGameSeconds();
    SaveData.Score = Score;
    // 당일 증감 조회에 필요한 이벤트 기록 보존
    SaveData.ScoreLog = ScoreLog;
    // 분류별 정산 값을 원래 정밀도로 보존
    SaveData.DailyScoreTotals = DailyScoreTotals;
    // 마지막 점검의 판정과 정산 내역 보존
    SaveData.LastInspectionReport = LastInspectionReport;
    return SaveData;
}

bool AMD_GameStateBase::ApplyGameStateSaveData(const FMD_GameStateData& SaveData)
{
	// 선행 검증을 통과한 데이터만 복제 상태에 적용하기 위한 검사
	if (CanApplyGameStateSaveData(SaveData) == false)
	{
		return false;
	}

	// 저장 당시 인게임 경과 초를 현재 서버 시각 기준의 시작 시각으로 역산하는 처리
	const double TotalGameSeconds = static_cast<double>(EndHour - StartHour) * 60.0 * 60.0;
    const double TimeScale = TotalGameSeconds / RealDayDurationSeconds;
    const double ElapsedRealSeconds = SaveData.ElapsedSeconds / TimeScale;
    PendingElapsedRealSeconds = ElapsedRealSeconds;
    GameStartServerTime = GetServerWorldTimeSeconds() - ElapsedRealSeconds;
    bGameTimeStarted = bWorldInitializationReady;
    bEmergencyDay = SaveData.bEmergencyDay;
    bInspectionWarned = SaveData.bInspectionWarned;
    ContractCrewCount = SaveData.ContractCrewCount;
    ContractDifficulty = SaveData.ContractDifficulty;
    CurrentDay = SaveData.ElapsedDay;
    Score = SaveData.Score;
    // 보상 함수를 재실행하지 않고 저장된 점수 로그 복원
    ScoreLog = SaveData.ScoreLog;
    // 이전 세션의 원장이 섞이지 않도록 당일 합계 교체
    DailyScoreTotals = SaveData.DailyScoreTotals;
    // 구형 저장의 기본값도 이전 세션의 점검 결과를 제거하도록 적용
    LastInspectionReport = SaveData.LastInspectionReport;
    // 저장 복원 후 현재 일자 기준 보고서 재구성
    RefreshDailyLedger();

    // 리슨 서버와 접속 클라이언트가 같은 복원 결과를 즉시 관찰하도록 하는 알림과 복제 요청
    OnRep_Score();
    OnRep_CurrentDay();
    ForceNetUpdate();

	return true;
}

bool AMD_GameStateBase::CanApplyGameStateSaveData(const FMD_GameStateData& SaveData) const
{
	// 클라이언트 적용과 설정된 하루 길이를 벗어난 진행 시간의 부분 적용을 방지하기 위한 검사
	const double TotalGameSeconds = static_cast<double>(EndHour - StartHour) * 60.0 * 60.0;
	return HasAuthority() == true && SaveData.IsStructurallyValid() == true && RealDayDurationSeconds > 0.0 && TotalGameSeconds > 0.0 && SaveData.ElapsedSeconds <= TotalGameSeconds;
}

void AMD_GameStateBase::OnRep_WithdrawalPhase()
{
   
    HandleWithdrawalPhaseChanged();
}

void AMD_GameStateBase::HandleWithdrawalPhaseChanged()
{
    switch (WithdrawalPhase)
    {
    case EMD_WithdrawalPhase::Notice:
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("17:00 -> 철수하세요")
        );
        break;

    case EMD_WithdrawalPhase::Warning:
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("17:30 -> 철수하셔야할텐데?")
        );
        break;

    case EMD_WithdrawalPhase::Departed:
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("18:00 -> 배가 출발합니다,,")
        );
        break;

    case EMD_WithdrawalPhase::None:
    default:
        break;
    }
}


void AMD_GameStateBase::RefreshDailyLedger()
{
    // 서버에서만 원장 응답 데이터 구성
    if (HasAuthority() == false)
    {
        return;
    }
    // 이전 일자 점검 결과가 남지 않는 새 보고서 구성
    FMD_InspectionReport Report;
    // 현재 일자와 확정 여부를 함께 확인한 점검 결과 재사용
    if (LastInspectionReport.Day == CurrentDay && LastInspectionReport.bValid == true)
    {
        // 확정된 상태 배지와 기준 점수 보존
        Report = LastInspectionReport;
    }
    // 당일 기록 조회에 필요한 일차 반영
    Report.Day = CurrentDay;
    // 마감 이전에도 조회 가능한 원장임을 표시
    Report.bValid = true;
    // 계약 누적 점수와 당일 점수 분리
    Report.Score = Score;
    // 당일 점수 로그 합계 조회
    Report.DailyScore = GetScoreForDay(CurrentDay);
    // 분류별 점수 원장 조회
    Report.DailyScoreTotals = GetDailyScoreTotals();
    // 이전 보고서의 크레딧 원장 제거
    Report.DailyCreditTotals.Reset();
    // 현재 월드의 서버 상인 원장 조회
    UGameInstance* GameInstance = GetGameInstance();
    if (IsValid(GameInstance) == true)
    {
        // 서버 크레딧 로그를 보관하는 서브시스템 조회
        UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
        // 유효한 서버 원장의 현재 일차 기록만 반영
        if (IsValid(Merchant) == true && Merchant->GetCurrentDay() == CurrentDay)
        {
            // 실제 잔액 변동을 반영한 분류별 크레딧 기록 조회
            Report.DailyCreditTotals = Merchant->GetDailyCreditTotals();
        }
    }
    // 값이 같으면 구독 중인 모든 클라이언트의 중복 응답 전송 생략
    if (FMD_InspectionReport::StaticStruct()->CompareScriptStruct(&DailyLedger, &Report, 0) == true)
    {
        return;
    }
    // 완성된 배열의 소유권을 서버 캐시로 이전
    DailyLedger = MoveTemp(Report);
    // 변경된 원장만 구독 컨트롤러에 알림
    OnDailyLedgerChanged.Broadcast();
}


bool AMD_GameStateBase::SetGameTimeHour(int32 Hour)
{
    // 시간 기준 변경 권한을 가진 서버 여부 확인
    bool bAuthority = HasAuthority();
    if (bAuthority == false)
    {
        return false;
    }
    // 이미 마감된 날짜의 정산 및 이동 상태 보존
    if (WithdrawalPhase == EMD_WithdrawalPhase::Departed)
    {
        return false;
    }
    // 설정 가능한 작업 시간 범위의 시작 시각 확인
    if (Hour < StartHour)
    {
        return false;
    }
    if (Hour > EndHour)
    {
        return false;
    }
    // 시간 환산에 필요한 실제 하루 길이 확인
    if (RealDayDurationSeconds <= 0.0)
    {
        return false;
    }
    // 인게임 하루 길이가 양수인 설정만 허용
    double TotalGameSeconds = static_cast<double>(EndHour - StartHour) * 3600.0;
    if (TotalGameSeconds <= 0.0)
    {
        return false;
    }
    // 지정한 시각을 작업 시작 이후의 인게임 경과 초로 환산
    double TargetGameSeconds = static_cast<double>(Hour - StartHour) * 3600.0;
    // 기존 시간 진행 속도에 맞춘 서버 시작 시각 역산
    PendingElapsedRealSeconds = TargetGameSeconds * RealDayDurationSeconds / TotalGameSeconds;
    GameStartServerTime = GetServerWorldTimeSeconds() - PendingElapsedRealSeconds;
    // 시작 전에도 지정한 시각부터 시간 진행 허용
    bGameTimeStarted = true;
    // 시간을 되돌린 경우에도 기존 알림과 자동 마감 순서 재평가
    SetWithdrawalPhase(EMD_WithdrawalPhase::None);
    // 변경된 시간 기준과 출발 단계를 접속 플레이어에게 전송
    ForceNetUpdate();
    // 설정 요청의 정상 적용 결과 반환
    return true;
}
