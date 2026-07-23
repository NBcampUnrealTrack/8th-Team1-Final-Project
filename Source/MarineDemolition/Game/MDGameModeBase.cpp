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

    //17:00
    if (!bWithdrawalNoticeLogged
        && MDGameState->HasReachedTime(17, 0))
    {
        bWithdrawalNoticeLogged = true;

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("17:00 철수하세요")
        );
    }

    //17:30
    if (!bWithdrawalWarningLogged
        && MDGameState->HasReachedTime(17, 30))
    {
        bWithdrawalWarningLogged = true;

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("17:30 철수하셔야 할 텐데?")
        );
    }

    //18:00
    if (!bShipDepartureLogged
        && MDGameState->HasReachedTime(18, 0))
    {
        bShipDepartureLogged = true;

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("18:00 배는 이대로 영영 떠나갔습니다,,")
        );

        GetWorldTimerManager().ClearTimer(
            CheckGameTimeTimerHandle
        );
    }
}