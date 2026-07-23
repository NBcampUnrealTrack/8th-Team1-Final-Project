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

void AMDGameStateBase::OnRep_GameStartServerTime()
{
    
}

float AMDGameStateBase::GetElapsedRealSeconds() const
{
    if (!bGameTimeStarted)
    {
        return 0.0f;
    }

    return FMath::Max(
        GetServerWorldTimeSeconds() - GameStartServerTime,
        0.0f
    );
}

float AMDGameStateBase::GetElapsedGameSeconds() const
{
    const float TotalGameSeconds =(EndHour - StartHour) * 60 * 60;

    if (RealDayDurationSeconds <= 0.0f)
    {
        return 0.0f;
    }

    //Real: 1sec, InGame: 60sec
    const float TimeScale =
        TotalGameSeconds / RealDayDurationSeconds;

    return FMath::Clamp(
        GetElapsedRealSeconds() * TimeScale,
        0.0f,
        TotalGameSeconds
    );
}

int32 AMDGameStateBase::GetCurrentHour() const
{
    const int32 ElapsedMinutes =
        FMath::FloorToInt(GetElapsedGameSeconds() / 60.0f);

    return StartHour + ElapsedMinutes / 60;
}

int32 AMDGameStateBase::GetCurrentMinute() const
{
    const int32 ElapsedMinutes =
        FMath::FloorToInt(GetElapsedGameSeconds() / 60.0f);

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
