// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "Player/MD_PlayerState.h"

AMD_GameModeBase::AMD_GameModeBase()
{
    PlayerStateClass = AMD_PlayerState::StaticClass();
}

void AMD_GameModeBase::BeginPlay()
{
    Super::BeginPlay();

    GetWorldTimerManager().SetTimer(
        CheckGameTimeTimerHandle,
        this,
        &AMD_GameModeBase::CheckGameTimeEvents,
        0.1f,
        true
    );
}

void AMD_GameModeBase::CheckGameTimeEvents()
{
    AMD_GameStateBase* MDGameState =
        GetGameState<AMD_GameStateBase>();

    if (!IsValid(MDGameState))
    {
        return;
    }

    const EMD_WithdrawalPhase CurrentPhase =
        MDGameState->GetWithdrawalPhase();

    //17:00
    if (CurrentPhase == EMD_WithdrawalPhase::None
        && MDGameState->HasReachedTime(17, 0))
    {
        MDGameState->SetWithdrawalPhase(
            EMD_WithdrawalPhase::Notice
        );

        return;
    }

    //17:30
    if (CurrentPhase == EMD_WithdrawalPhase::Notice
        && MDGameState->HasReachedTime(17, 30))
    {
        MDGameState->SetWithdrawalPhase(
            EMD_WithdrawalPhase::Warning
        );

        return;
    }

    //18:00
    if (CurrentPhase == EMD_WithdrawalPhase::Warning
        && MDGameState->HasReachedTime(18, 0))
    {
        MDGameState->SetWithdrawalPhase(
            EMD_WithdrawalPhase::Departed
        );

        GetWorldTimerManager().ClearTimer(
            CheckGameTimeTimerHandle
        );
    }
}