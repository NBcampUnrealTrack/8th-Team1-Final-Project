#include "Game/MD_GameStateBase.h"
#include "Data/Score/MD_ScoreTableData.h"
#include "Net/UnrealNetwork.h"

AMD_GameStateBase::AMD_GameStateBase()
{
    bReplicates = true;
}

void AMD_GameStateBase::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        StartGameTime();
    }
}

void AMD_GameStateBase::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMD_GameStateBase, GameStartServerTime);
    DOREPLIFETIME(AMD_GameStateBase, bGameTimeStarted);
    DOREPLIFETIME(AMD_GameStateBase, WithdrawalPhase);
    
    DOREPLIFETIME(AMD_GameStateBase, Score);

    DOREPLIFETIME(AMD_GameStateBase, bInspectionWarned);

    DOREPLIFETIME(AMD_GameStateBase, LastInspectionReport);

    DOREPLIFETIME(AMD_GameStateBase, CurrentDay);

    DOREPLIFETIME(AMD_GameStateBase, ContractCrewCount);

    DOREPLIFETIME(AMD_GameStateBase, bEmergencyDay);
}

//Timer
void AMD_GameStateBase::StartGameTime()
{
    if (!HasAuthority())
    {
        return;
    }

    GameStartServerTime = GetServerWorldTimeSeconds();
    bGameTimeStarted = true;

    ForceNetUpdate();
}

double AMD_GameStateBase::GetElapsedRealSeconds() const
{
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
    if (!HasAuthority())
    {
        return;
    }

    Score += Delta;
    // 리슨 서버는 OnRep이 안 오므로 직접 방송
    OnRep_Score();
}

void AMD_GameStateBase::AddScoreEvent(FGameplayTag EventTag, int32 Count)
{
    if (!HasAuthority() || !IsValid(ScoreTable) || Count == 0)
    {
        return;
    }

    const int32 Delta = ScoreTable->GetEventValue(EventTag) * Count;
    if (Delta == 0)
    {
        return;
    }

    ScoreLog.Add({EventTag, Delta, CurrentDay});
    AddScoreDelta(Delta);
}

void AMD_GameStateBase::AddSampleScoreEvent(EMD_SampleGrade Grade, EMD_SampleScoreType Type)
{
    if (!HasAuthority() || !IsValid(ScoreTable))
    {
        return;
    }

    const int32 Delta = ScoreTable->GetSampleValue(Grade, Type);
    if (Delta == 0)
    {
        return;
    }

    ScoreLog.Add({FGameplayTag(), Delta, CurrentDay});
    AddScoreDelta(Delta);
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
    float Multiplier = ScoreTable->DifficultyThresholdMultiplier;
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
    CurrentDay = NewDay;
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