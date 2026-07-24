// Fill out your copyright notice in the Description page of Project Settings.


#include "MDGameModeBase.h"
#include "MDGameStateBase.h"
#include "TimerManager.h"

void AMDGameModeBase::BeginPlay()
{
    Super::BeginPlay();

    GetWorldTimerManager().SetTimer(
        CheckGameTimeTimerHandle,
        this,
        &AMDGameModeBase::CheckGameTimeEvents,
        0.1f,
        true
    );
}

void AMDGameModeBase::CheckGameTimeEvents()
{
    AMDGameStateBase* MDGameState =
        GetGameState<AMDGameStateBase>();

    if (!IsValid(MDGameState))
    {
        return;
    }

    const EMDWithdrawalPhase CurrentPhase =
        MDGameState->GetWithdrawalPhase();

    //17:00
    if (CurrentPhase == EMDWithdrawalPhase::None
        && MDGameState->HasReachedTime(17, 0))
    {
        MDGameState->SetWithdrawalPhase(
            EMDWithdrawalPhase::Notice
        );

        return;
    }

    //17:30
    if (CurrentPhase == EMDWithdrawalPhase::Notice
        && MDGameState->HasReachedTime(17, 30))
    {
        MDGameState->SetWithdrawalPhase(
            EMDWithdrawalPhase::Warning
        );

        return;
    }

    //18:00
    if (CurrentPhase == EMDWithdrawalPhase::Warning
        && MDGameState->HasReachedTime(18, 0))
    {
        MDGameState->SetWithdrawalPhase(
            EMDWithdrawalPhase::Departed
        );

        GetWorldTimerManager().ClearTimer(
            CheckGameTimeTimerHandle
        );
    }
}