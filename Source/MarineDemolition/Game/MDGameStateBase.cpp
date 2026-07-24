#include "MDGameStateBase.h"

#include "Net/UnrealNetwork.h"

AMDGameStateBase::AMDGameStateBase()
{
    bReplicates = true;
}

void AMDGameStateBase::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        StartGameTime();
    }
}

void AMDGameStateBase::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMDGameStateBase, GameStartServerTime);
    DOREPLIFETIME(AMDGameStateBase, bGameTimeStarted);
    DOREPLIFETIME(AMDGameStateBase, WithdrawalPhase);
}

//Timer
void AMDGameStateBase::StartGameTime()
{
    if (!HasAuthority())
    {
        return;
    }

    GameStartServerTime = GetServerWorldTimeSeconds();
    bGameTimeStarted = true;

    ForceNetUpdate();
}

double AMDGameStateBase::GetElapsedRealSeconds() const
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

double AMDGameStateBase::GetElapsedGameSeconds() const
{
    const float TotalGameSeconds =(EndHour - StartHour) * 60 * 60;

    if (RealDayDurationSeconds <= 0.0f)
    {
        return 0.0;
    }

    //Real: 1sec, InGame: 60sec
    const float TimeScale =
        TotalGameSeconds / RealDayDurationSeconds;

    return FMath::Clamp(
        GetElapsedRealSeconds() * TimeScale,
        0.0,
        TotalGameSeconds
    );
}

int32 AMDGameStateBase::GetCurrentHour() const
{
    const int32 ElapsedMinutes =
        FMath::FloorToInt(GetElapsedGameSeconds() / 60.0);

    return StartHour + ElapsedMinutes / 60;
}

int32 AMDGameStateBase::GetCurrentMinute() const
{
    const int32 ElapsedMinutes =
        FMath::FloorToInt(GetElapsedGameSeconds() / 60.0);

    return ElapsedMinutes % 60;
}

bool AMDGameStateBase::HasReachedTime(
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

// Withdrawal
void AMDGameStateBase::SetWithdrawalPhase(
    EMDWithdrawalPhase NewPhase
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

EMDWithdrawalPhase
AMDGameStateBase::GetWithdrawalPhase() const
{
    return WithdrawalPhase;
}

void AMDGameStateBase::OnRep_WithdrawalPhase()
{
   
    HandleWithdrawalPhaseChanged();
}

void AMDGameStateBase::HandleWithdrawalPhaseChanged()
{
    switch (WithdrawalPhase)
    {
    case EMDWithdrawalPhase::Notice:
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("17:00 -> 철수하세요")
        );
        break;

    case EMDWithdrawalPhase::Warning:
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("17:30 -> 철수하셔야할텐데?")
        );
        break;

    case EMDWithdrawalPhase::Departed:
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("18:00 -> 배가 출발합니다,,")
        );
        break;

    case EMDWithdrawalPhase::None:
    default:
        break;
    }
}