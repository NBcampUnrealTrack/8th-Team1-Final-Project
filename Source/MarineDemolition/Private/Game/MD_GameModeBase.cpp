// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "EngineUtils.h"
#include "Game/MD_DayPersistenceSubsystem.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "Player/MD_PlayerState.h"
#include "Wall/MD_CuttableWall.h"

AMD_GameModeBase::AMD_GameModeBase()
{
    PlayerStateClass = AMD_PlayerState::StaticClass();

    bUseSeamlessTravel = true;
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

    // 레벨의 모든 격벽에 붕괴 점수 처리 바인딩 (GameMode는 서버 전용이라 중복 적용 없음)
    for (TActorIterator<AMD_CuttableWall> It(GetWorld()); It; ++It)
    {
        It->OnStrutCollapsed.AddDynamic(this, &AMD_GameModeBase::HandleStrutCollapsed);
    }

    // 이전 Day 스냅샷 복원은 전 액터 BeginPlay 이후로 미룸 (벽 오염 초기화보다 늦게)
    GetWorldTimerManager().SetTimerForNextTick(
        this, &AMD_GameModeBase::RestoreDaySnapshot);
}

void AMD_GameModeBase::HandleStrutCollapsed(AMD_CuttableWall* Wall, AActor* Cutter)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->AddScoreDelta(StrutCollapseScoreDelta);
    }
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

        FinalizeReturnStatuses();

        CaptureDaySnapshot();

        GetWorldTimerManager().ClearTimer(
            CheckGameTimeTimerHandle
        );
    }
}

void AMD_GameModeBase::FinalizeReturnStatuses()
{
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();

    if (!IsValid(MDGameState))
    {
        return;
    }

    for (APlayerState* PlayerState : MDGameState->PlayerArray)
    {
        AMD_PlayerState* MDPlayerState = Cast<AMD_PlayerState>(PlayerState);

        if (!IsValid(MDPlayerState))
        {
            continue;
        }

        if (MDPlayerState->GetReturnStatus() != EMD_ReturnStatus::Pending)
        {
            continue;
        }

        MDPlayerState->SetReturnStatus(EMD_ReturnStatus::Failed);
    }
}

void AMD_GameModeBase::CaptureDaySnapshot()
{
    if (UMD_DayPersistenceSubsystem* Persistence =
        GetGameInstance()->GetSubsystem<UMD_DayPersistenceSubsystem>())
    {
        Persistence->CaptureSnapshot(GetWorld());
    }
}

void AMD_GameModeBase::RestoreDaySnapshot()
{
    if (UMD_DayPersistenceSubsystem* Persistence =
        GetGameInstance()->GetSubsystem<UMD_DayPersistenceSubsystem>())
    {
        Persistence->RestoreSnapshot(GetWorld());
    }
}

void AMD_GameModeBase::MD_EndDay()
{
    CaptureDaySnapshot();

    // 같은 레벨을 다시 로드 → BeginPlay에서 복원 경로 진입
    GetWorld()->ServerTravel(TEXT("?Restart"));
}
