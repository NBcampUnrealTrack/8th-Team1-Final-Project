#include "Game/MD_GameStateBase.h"

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