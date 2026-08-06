// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "EngineUtils.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "World/Return/MD_ReturnZone.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"

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

    // 레벨의 모든 격벽에 붕괴·철거 점수 처리 바인딩 (GameMode는 서버 전용이라 중복 적용 없음)
    for (TActorIterator<AMD_CuttableWall> It(GetWorld()); It; ++It)
    {
        It->OnStrutCollapsed.AddDynamic(this, &AMD_GameModeBase::HandleStrutCollapsed);
        It->OnWallDemolished.AddDynamic(this, &AMD_GameModeBase::HandleWallDemolished);
    }

    // 이전 Day 스냅샷 복원은 전 액터 BeginPlay 이후로 미룸 (벽 오염 초기화보다 늦게)
    GetWorldTimerManager().SetTimerForNextTick(
        this, &AMD_GameModeBase::RestoreDaySnapshot);
}

void AMD_GameModeBase::RestartPlayer(AController* NewPlayer)
{
    Super::RestartPlayer(NewPlayer);
}

void AMD_GameModeBase::Logout(AController* Exiting)
{
    Super::Logout(Exiting);
}

void AMD_GameModeBase::HandleStrutCollapsed(AMD_CuttableWall* Wall, AActor* Cutter)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->AddScoreEvent(MDGameplayTags::Score_Penalty_Collapse_Unsupported);
    }
}

void AMD_GameModeBase::HandleWallDemolished(AMD_CuttableWall* Wall, AActor* Cutter)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        // 칸막이 +15 / 지지 상태 지주 +30
        GS->AddScoreEvent(Wall->GetWallType() == EMD_WallType::Strut
            ? MDGameplayTags::Score_Demolition_Strut
            : MDGameplayTags::Score_Demolition_Partition);
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

        FinalizeDay();

        GetWorldTimerManager().ClearTimer(
            CheckGameTimeTimerHandle
        );
    }
}

void AMD_GameModeBase::FinalizeDay()
{
    // 18:00 자동 마감 후 MD_EndDay가 호출돼도 감점·점검이 중복 적용되지 않게 1회만 실행
    if (bDayFinalized)
    {
        return;
    }

    bDayFinalized = true;

    FinalizeReturnStatuses();

    FinalizeDailyScore();

    EvaluateInspection();

    CaptureDaySnapshot();
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

void AMD_GameModeBase::FinalizeDailyScore()
{
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();

    if (!IsValid(MDGameState))
    {
        return;
    }

    // 복귀 정산: 1명이라도 미복귀면 보너스 없이 인당 차감
    int32 FailedCount = 0;
    int32 PlayerCount = 0;

    for (APlayerState* PlayerState : MDGameState->PlayerArray)
    {
        const AMD_PlayerState* MDPlayerState = Cast<AMD_PlayerState>(PlayerState);

        if (!IsValid(MDPlayerState))
        {
            continue;
        }

        ++PlayerCount;

        if (MDPlayerState->GetReturnStatus() == EMD_ReturnStatus::Failed)
        {
            ++FailedCount;
        }
    }

    if (FailedCount > 0)
    {
        MDGameState->AddScoreEvent(MDGameplayTags::Score_Penalty_NotReturned, FailedCount);
    }
    else if (PlayerCount > 0)
    {
        MDGameState->AddScoreEvent(MDGameplayTags::Score_Daily_AllReturned);
    }

    // 샘플 정산: 복귀 구역 안 = 납품, 밖 = 미회수
    TArray<AMD_ReturnZone*> ReturnZones;

    for (TActorIterator<AMD_ReturnZone> It(GetWorld()); It; ++It)
    {
        ReturnZones.Add(*It);
    }

    for (TActorIterator<AMD_SampleItem> It(GetWorld()); It; ++It)
    {
        AMD_SampleItem* Sample = *It;

        const bool bDelivered = ReturnZones.ContainsByPredicate(
            [Sample](const AMD_ReturnZone* Zone)
            {
                return Zone->ContainsPoint(Sample->GetActorLocation());
            });

        if (bDelivered)
        {
            MDGameState->AddSampleScoreEvent(
                Sample->Grade,
                Sample->bDamaged ? EMD_SampleScoreType::Damaged : EMD_SampleScoreType::Intact);

            // 납품 완료된 샘플은 제거 → 스냅샷에서 빠져 다음날 중복 정산 없음
            Sample->Destroy();
        }
        else
        {
            MDGameState->AddSampleScoreEvent(Sample->Grade, EMD_SampleScoreType::Unrecovered);
        }
    }

    // 현장 미수거: 월드에 남은 분리 대상. 현재는 벽 조각만 집계
    int32 UncollectedCount = 0;

    for (TActorIterator<AMD_WallPiece> It(GetWorld()); It; ++It)
    {
        ++UncollectedCount;
    }

    if (UncollectedCount > 0)
    {
        MDGameState->AddScoreEvent(MDGameplayTags::Score_Penalty_UncollectedRemain, UncollectedCount);
    }
}

EMD_InspectionResult AMD_GameModeBase::EvaluateInspection()
{
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();

    if (!IsValid(MDGameState))
    {
        return EMD_InspectionResult::NotApplicable;
    }

    const int32 Day = MDGameState->GetCurrentDay();
    const int32 Threshold = MDGameState->GetInspectionThreshold(Day);

    // 기준이 없는 날(Day1)은 점검 없음
    if (Threshold <= 0)
    {
        return EMD_InspectionResult::NotApplicable;
    }

    // 판정은 경고 감점(-40) 적용 전 점수 기준
    const int32 JudgedScore = MDGameState->GetScore();
    const bool bPassed = JudgedScore >= Threshold;

    EMD_InspectionResult Result;

    if (bPassed)
    {
        Result = (Day >= 3) ? EMD_InspectionResult::ContractCleared : EMD_InspectionResult::Passed;
    }
    else if (Day == 2)
    {
        Result = EMD_InspectionResult::WarningIssued;
    }
    else if (Day == 3 && !MDGameState->IsInspectionWarned())
    {
        Result = EMD_InspectionResult::EmergencyDayOpened;
    }
    else
    {
        Result = EMD_InspectionResult::GameOver;
    }

    // 경고 부여: 감점 + 다음 점검 기준 배율 상승
    if (Result == EMD_InspectionResult::WarningIssued
        || Result == EMD_InspectionResult::EmergencyDayOpened)
    {
        MDGameState->AddScoreEvent(MDGameplayTags::Score_Penalty_Warning);
        MDGameState->SetInspectionWarned(true);
    }

    // 결과를 복제 리포트로 기록 → 전 클라이언트 UI 통지
    FMD_InspectionReport Report;
    Report.bValid = true;
    Report.Day = Day;
    Report.Result = Result;
    Report.Score = JudgedScore;
    Report.Threshold = Threshold;
    MDGameState->SetInspectionReport(Report);

    // 클리어·게임오버 흐름(레벨 전환 등)은 후속 카드에서 연결
    return Result;
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
    FinalizeDay();

    // 같은 레벨을 다시 로드 → BeginPlay에서 복원 경로 진입
    GetWorld()->ServerTravel(TEXT("?Restart"));
}
