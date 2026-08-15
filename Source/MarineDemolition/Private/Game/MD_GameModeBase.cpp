// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "EngineUtils.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Game/MD_GameStateBase.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Player/MD_PlayerController.h"
#include "TimerManager.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "World/Return/MD_ReturnZone.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Engine/GameInstance.h"

AMD_GameModeBase::AMD_GameModeBase()
{
    PlayerStateClass = AMD_PlayerState::StaticClass();

    bUseSeamlessTravel = true;
}

void AMD_GameModeBase::BeginPlay()
{
    Super::BeginPlay();

    // 게임 맵의 액터 초기화 이후 새 게임 또는 불러오기 상태를 적용하기 위한 다음 틱 예약
    GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ApplyPendingGameDataOperation);

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

    // 이전 Day 스냅샷 복원은 전 액터 BeginPlay 이후로 미룸 (벽 오염 초기화보다 늦게).
    // 복원 직후 Merchant Day·퀘스트를 GameState와 맞춘다.
    GetWorldTimerManager().SetTimerForNextTick(
        this, &AMD_GameModeBase::InitializeExpeditionDay);

    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->OnDayChanged.AddDynamic(this, &AMD_GameModeBase::HandleGameDayChanged);
    }
}

void AMD_GameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->OnDayChanged.RemoveDynamic(this, &AMD_GameModeBase::HandleGameDayChanged);
    }

    Super::EndPlay(EndPlayReason);
}

void AMD_GameModeBase::ApplyPendingGameDataOperation()
{
    // GameInstance 수명으로 트래블을 통과한 데이터 초기화 작업을 실행할 저장 관리자 참조
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == true)
    {
        SaveSlotManager->ApplyPendingGameDataOperation();
    }
}

void AMD_GameModeBase::RestartPlayer(AController* NewPlayer)
{
    Super::RestartPlayer(NewPlayer);

    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == true && IsValid(NewPlayer->PlayerState) == true)
    {
        SaveSlotManager->LoadPlayerData(NewPlayer->PlayerState);
    }
}

void AMD_GameModeBase::Logout(AController* Exiting)
{
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == true && IsValid(Exiting) == true)
    {
        SaveSlotManager->ParseDataFromPlayer(Exiting->PlayerState);
    }

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
        RequestEndDay();
    }
}

void AMD_GameModeBase::RequestEndDay()
{
    // 18:00 자동 마감과 인터랙트가 겹쳐도 1회만 실행
    if (bDayFinalized)
    {
        return;
    }

    GetWorldTimerManager().ClearTimer(CheckGameTimeTimerHandle);

    // 조기 마감이어도 출발 연출·HUD는 18:00 마감과 동일하게
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->SetWithdrawalPhase(EMD_WithdrawalPhase::Departed);
    }

    const EMD_InspectionResult Result = FinalizeDay();

    RouteAfterDay(Result);
}

void AMD_GameModeBase::TravelToNextDay()
{
    // 같은 레벨을 다시 로드 → BeginPlay에서 복원 경로 진입
    GetWorld()->ServerTravel(TEXT("?Restart"));
}

EMD_InspectionResult AMD_GameModeBase::FinalizeDay()
{
    // 18:00 자동 마감 후 MD_EndDay가 호출돼도 감점·점검이 중복 적용되지 않게 1회만 실행
    if (bDayFinalized)
    {
        return PendingResult;
    }

    bDayFinalized = true;

    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        if (UMD_MerchantSubsystem* Merchant =
            GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>())
        {
            Merchant->ResolveActiveQuestsAtDayEnd(GS->GetCurrentDay());

			// 빚에 대한 이자 측정 및 증가
			Merchant->ApplyDailyLoanInterest();
        }
    }

    FinalizeReturnStatuses();

    // 인원 배율용 인원수 고정 (Day1에 한 번, 이후 스냅샷 캐리)
    CaptureContractCrew();

    FinalizeDailyScore();

    PendingResult = EvaluateInspection();

    CaptureDaySnapshot();

    return PendingResult;
}

void AMD_GameModeBase::CaptureContractCrew()
{
    AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>();

    // 이미 고정됐으면 유지 (계약 시작 시점 인원 고정)
    if (!IsValid(GS) || GS->GetContractCrewCount() > 0)
    {
        return;
    }

    int32 Crew = 0;
    for (APlayerState* PlayerState : GS->PlayerArray)
    {
        if (Cast<AMD_PlayerState>(PlayerState))
        {
            ++Crew;
        }
    }

    GS->SetContractCrewCount(FMath::Max(Crew, 1));
}

bool AMD_GameModeBase::DoesSiteUseCurrentWorld(UMD_SiteDefinition* Site, UWorld* World) const
{
    if (IsValid(Site) == false || IsValid(World) == false)
    {
        return false;
    }

    // PIE 접두사를 제거하여 데이터 에셋 경로와 비교할 현재 맵 이름
    FString CurrentMapName = World->GetMapName();
    CurrentMapName.RemoveFromStart(World->StreamingLevelsPrefix);

    // 현장의 일차별 메인 퀘스트가 참조하는 실행 맵 검사
    for (const FMD_QuestDefinitionData& MainQuestDefinition : Site->MainQuestLine)
    {
        // 소프트 월드 경로에서 추출한 비교용 맵 애셋 이름
        FString QuestMapName = MainQuestDefinition.QuestMap.ToSoftObjectPath().GetAssetName();
        if (QuestMapName.IsEmpty() == false && QuestMapName == CurrentMapName)
        {
            return true;
        }
    }

    return false;
}

TSoftObjectPtr<UWorld> AMD_GameModeBase::MakeWorldAssetReference(UWorld* World) const
{
    // 현재 실행 월드의 원본 애셋을 가리키는 반환 참조
    TSoftObjectPtr<UWorld> OutWorldReference;

    if (IsValid(World) == true)
    {
        // PIE 접두사가 포함될 수 있는 실행 월드 패키지 이름
        FString WorldPackageName = World->GetPackage()->GetName();

        // 원본 월드 오브젝트 경로를 재구성하기 위한 패키지 경로
        FString WorldPackagePath = FPackageName::GetLongPackagePath(WorldPackageName);

        // PIE 접두사를 제거하여 원본 애셋 이름으로 복구할 현재 맵 이름
        FString WorldAssetName = FPackageName::GetShortName(WorldPackageName);
        WorldAssetName.RemoveFromStart(World->StreamingLevelsPrefix);

        // TMap의 월드 소프트 참조 키와 동일한 형식의 오브젝트 경로
        FString WorldObjectPath = FString::Printf(TEXT("%s/%s.%s"), *WorldPackagePath, *WorldAssetName, *WorldAssetName);
        OutWorldReference = TSoftObjectPtr<UWorld>(FSoftObjectPath(WorldObjectPath));
    }

    return OutWorldReference;
}

void AMD_GameModeBase::RouteAfterDay(EMD_InspectionResult Result)
{
    const AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>();
    const int32 Day = IsValid(GS) ? GS->GetCurrentDay() : 0;

    // Day4 상한 가드: 4일차 이후 '계속' 결과(통과/경고/비상)는 나올 수 없음 → 게임오버 강제
    if (Day >= 4 && Result != EMD_InspectionResult::ContractCleared)
    {
        Result = EMD_InspectionResult::GameOver;
    }

    PendingResult = Result;

    // 경고/비상 경제 컨텍스트를 다음 Day로 전달
    if (UMD_MerchantSubsystem* Merchant =
        GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>())
    {
        if (Result == EMD_InspectionResult::WarningIssued)
        {
            // 다음 정규일 착수금 ×0.7 · 소모품 할인 제거
            Merchant->SetNextDepositMultiplier(0.7f);
            Merchant->SetConsumableDiscountActive(false);
        }
        else if (Result == EMD_InspectionResult::EmergencyDayOpened)
        {
            // 비상 4일차 착수금 없음
            Merchant->SetNextDepositMultiplier(0.f);
            Merchant->SetConsumableDiscountActive(false);
        }
    }

    const bool bResultScreen =
        (Result == EMD_InspectionResult::ContractCleared
            || Result == EMD_InspectionResult::GameOver);

    GetWorldTimerManager().SetTimer(
        DepartTravelTimerHandle,
        this,
        &AMD_GameModeBase::ExecuteDayTransition,
        bResultScreen ? ResultTravelDelay : DepartTravelDelay,
        false
    );
}

void AMD_GameModeBase::ExecuteDayTransition()
{
    switch (PendingResult)
    {
    case EMD_InspectionResult::ContractCleared:
        HandleContractCleared();
        break;

    case EMD_InspectionResult::GameOver:
        HandleGameOver();
        break;

    default:
        // Passed / WarningIssued / EmergencyDayOpened / NotApplicable → 다음 날 진입
        TravelToNextDay();
        break;
    }
}

void AMD_GameModeBase::HandleContractCleared()
{
    // 반복 퀘스트 해금 판정에 사용할 현장 클리어 이력 등록
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
    if (IsValid(Merchant) == true)
    {
        Merchant->MarkSiteCleared();
    }

    // 진행(크레딧·퀘스트·호감도)은 유지, 현장 3일 스냅샷만 폐기 → 다음 현장은 새로 시작
    if (UMD_DayPersistenceSubsystem* Persistence =
        GetGameInstance()->GetSubsystem<UMD_DayPersistenceSubsystem>())
    {
        Persistence->ClearSnapshot();
    }

    TravelToLevel(ClearedLevel);
}

void AMD_GameModeBase::HandleGameOver()
{
    // 게임오버 시 세이브(진행 스냅샷) 삭제
    if (UMD_DayPersistenceSubsystem* Persistence =
        GetGameInstance()->GetSubsystem<UMD_DayPersistenceSubsystem>())
    {
        Persistence->ClearSnapshot();
    }

    // 게임오버 시에만 전체 리셋 (크레딧·계약)
    if (UMD_MerchantSubsystem* Merchant =
        GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>())
    {
        Merchant->ResetContract();
    }

    TravelToLevel(GameOverLevel);
}

void AMD_GameModeBase::TravelToLevel(const TSoftObjectPtr<UWorld>& Level)
{
    const FString Package = Level.ToSoftObjectPath().GetLongPackageName();

    if (Package.IsEmpty())
    {
        // 대상 미지정 → 안전하게 현재 레벨 리스타트 (BP에서 레벨 지정 필요)
        UE_LOG(LogTemp, Warning,
            TEXT("[Day] 전환 대상 레벨 미지정. BP GameMode에서 지정 필요"));
        GetWorld()->ServerTravel(TEXT("?Restart"));
        return;
    }

    GetWorld()->ServerTravel(Package + TEXT("?listen"));
}

void AMD_GameModeBase::FinalizeReturnStatuses()
{
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();

    if (!IsValid(MDGameState))
    {
        return;
    }

    // 복귀 판정은 마감 순간 폰의 위치로 확정한다.
    // 오버랩 진입 이벤트에만 의존하면 (1)존 안에서 스폰된 인원을 놓치고,
    // (2)한 번 들어왔다 나가도 Returned가 유지되는 문제가 있어 위치로 재판정한다.
    TArray<AMD_ReturnZone*> ReturnZones;

    for (TActorIterator<AMD_ReturnZone> It(GetWorld()); It; ++It)
    {
        ReturnZones.Add(*It);
    }

    for (APlayerState* PlayerState : MDGameState->PlayerArray)
    {
        AMD_PlayerState* MDPlayerState = Cast<AMD_PlayerState>(PlayerState);

        if (!IsValid(MDPlayerState))
        {
            continue;
        }

        bool bInReturnZone = false;

        if (const APawn* Pawn = MDPlayerState->GetPawn())
        {
            const FVector Location = Pawn->GetActorLocation();

            bInReturnZone = ReturnZones.ContainsByPredicate(
                [&Location](const AMD_ReturnZone* Zone)
                {
                    return Zone->ContainsPoint(Location);
                });
        }

        MDPlayerState->SetReturnStatus(bInReturnZone
            ? EMD_ReturnStatus::Returned
            : EMD_ReturnStatus::Failed);
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

    // 비상 4일차 개방 표시 (Day 간 유지)
    if (Result == EMD_InspectionResult::EmergencyDayOpened)
    {
        MDGameState->SetEmergencyDay(true);
    }

    // 결과를 복제 리포트로 기록 → 전 클라이언트 UI 통지
    FMD_InspectionReport Report;
    Report.bValid = true;
    Report.Day = Day;
    Report.Result = Result;
    Report.Score = JudgedScore;
    Report.Threshold = Threshold;
    MDGameState->SetInspectionReport(Report);

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

void AMD_GameModeBase::InitializeExpeditionDay()
{
    RestoreDaySnapshot();

    if (const AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        SyncMerchantToGameDay(GS->GetCurrentDay());
    }

    // 월드 복원과 상인 퀘스트 일차 동기화가 끝난 경계 상태만 자동 저장하는 처리
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == true)
    {
        SaveSlotManager->SaveGameData(TEXT("Auto"), true);
    }
}

void AMD_GameModeBase::HandleGameDayChanged(int32 NewDay)
{
    SyncMerchantToGameDay(NewDay);
}

UMD_SiteDefinition* AMD_GameModeBase::ResolveExpeditionSite() const
{
    // 현재 월드와 퀘스트 맵이 일치하는 현장만 반환하기 위한 결과
    UMD_SiteDefinition* OutSite = nullptr;

    // PIE 맵 이름까지 포함하여 후보 현장의 실제 실행 맵을 확인하기 위한 월드
    UWorld* World = GetWorld();
    if (IsValid(World) == true)
    {
        // PIE에서도 원본 맵 키로 조회하기 위한 현재 월드 소프트 참조
        TSoftObjectPtr<UWorld> CurrentWorldReference = MakeWorldAssetReference(World);

        // 현재 맵과 일대일로 등록된 현장 데이터 조회
        TObjectPtr<UMD_SiteDefinition> RegisteredSite = ExpeditionSitesByMap.FindRef(CurrentWorldReference);
        if (IsValid(RegisteredSite) == true)
        {
            OutSite = RegisteredSite.Get();
        }

        // 기존 단일 Site 설정을 사용하는 Blueprint와의 호환 경로
        if (IsValid(OutSite) == false && DoesSiteUseCurrentWorld(ExpeditionSite, World) == true)
        {
            OutSite = ExpeditionSite;
        }

        if (IsValid(OutSite) == false)
        {
            // 등록 현장이 없을 때 테스트용 PlayerController 현장을 제한적으로 사용하는 대체 경로
            for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            {
                // 현재 서버 월드에서 테스트용 현장 설정을 제공하는 플레이어 컨트롤러
                AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(It->Get());

                // 플레이어 컨트롤러가 제공하는 원정 현장 후보
                UMD_SiteDefinition* PlayerSite = IsValid(PlayerController) == true ? PlayerController->GetExpeditionSite() : nullptr;
                if (DoesSiteUseCurrentWorld(PlayerSite, World) == true)
                {
                    OutSite = PlayerSite;
                    break;
                }
            }
        }
    }

    return OutSite;
}

void AMD_GameModeBase::SyncMerchantToGameDay(int32 Day)
{
    if (Day <= 0)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World) || World->GetNetMode() == NM_Client)
    {
        return;
    }

    UMD_MerchantSubsystem* Merchant =
        GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
    if (!IsValid(Merchant))
    {
        return;
    }

    // 여러 현장 후보 중 현재 맵에 연결된 계약 데이터
    UMD_SiteDefinition* ResolvedSite = ResolveExpeditionSite();
    if (IsValid(ResolvedSite) == false)
    {
        UE_LOG(LogTemp, Log, TEXT("[Merchant] 현재 맵에 연결된 원정 Site 없음: %s"), *World->GetMapName());
        return;
    }

    // 같은 현장의 Day 전환은 유지하고 다른 현장 진입만 새 계약으로 교체하기 위한 현재 데이터
    UMD_SiteDefinition* CurrentSite = Merchant->GetCurrentSite();
    if (CurrentSite != ResolvedSite)
    {
        Merchant->BeginContract(ResolvedSite, ExpeditionAdvancePayment);
    }

    bool bShouldBeginDay = Merchant->GetCurrentDay() != Day;
    if (!bShouldBeginDay)
    {
        for (const FMD_MainQuestState& QuestState : Merchant->GetMainQuests())
        {
            if (QuestState.UnlockDay <= Day
                && QuestState.Status == EMD_MainQuestStatus::Locked)
            {
                bShouldBeginDay = true;
                break;
            }
        }
    }

    if (bShouldBeginDay)
    {
        Merchant->BeginDay(Day);
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
    RequestEndDay();
}

void AMD_GameModeBase::MD_AddScore(int32 Delta)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->AddScoreDelta(Delta);
    }
}

void AMD_GameModeBase::MD_SetDay(int32 Day)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->SetCurrentDay(Day);
    }
}

void AMD_GameModeBase::MD_SetCrew(int32 Crew)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->SetContractCrewCount(Crew);
    }
}

void AMD_GameModeBase::MD_SetWarned(int32 bWarned)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->SetInspectionWarned(bWarned != 0);
    }
}
