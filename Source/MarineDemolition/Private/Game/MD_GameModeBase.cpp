// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
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
#include "Engine/World.h"
#include "Online/Session/MD_MultiplayManager.h"

AMD_GameModeBase::AMD_GameModeBase()
{
    PlayerStateClass = AMD_PlayerState::StaticClass();

    bUseSeamlessTravel = true;
}

void AMD_GameModeBase::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    // 출발 콘솔이 전달한 난이도 옵션만 계약 난이도로 인정하기 위한 원문 검사
    FString DifficultyOption = UGameplayStatics::ParseOption(Options, TEXT("Difficulty"));
    if (DifficultyOption.IsEmpty() == false)
    {
        // 변조되거나 정의되지 않은 정수 값이 점검 기준을 오염시키지 않도록 하는 서버 검증
        uint8 DifficultyValue = MAX_uint8;
        if (LexTryParseString(DifficultyValue, *DifficultyOption) == true && DifficultyValue >= static_cast<uint8>(EMD_Difficulty::Safe) && DifficultyValue <= static_cast<uint8>(EMD_Difficulty::Extreme))
        {
            RequestedContractDifficulty = static_cast<EMD_Difficulty>(DifficultyValue);
            bHasRequestedContractDifficulty = true;
        }
    }

    // 출발 서버가 확정한 인원수만 계약 인원수로 인정하기 위한 원문 검사
    FString CrewCountOption = UGameplayStatics::ParseOption(Options, TEXT("CrewCount"));
    if (CrewCountOption.IsEmpty() == false)
    {
        // 기획에서 지원하는 1~4인 범위 외 값이 점검 기준을 오염시키지 않도록 하는 서버 검증
        int32 CrewCountValue = 0;
        if (LexTryParseString(CrewCountValue, *CrewCountOption) == true && CrewCountValue >= 1 && CrewCountValue <= 4)
        {
            RequestedContractCrewCount = CrewCountValue;
            bHasRequestedContractCrewCount = true;
        }
    }
}

void AMD_GameModeBase::BeginPlay()
{
    Super::BeginPlay();

    // GameState 생성이 끝난 시점에 출발 서버가 확정한 계약 설정을 서버 원장에 반영
    ApplyRequestedContractSettings();

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

void AMD_GameModeBase::ApplyRequestedContractSettings()
{
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
    if (IsValid(MDGameState) == false)
    {
        return;
    }

    // 출발 콘솔을 거치지 않은 직접 맵 실행에서는 GameState의 표준 난이도 기본값 유지
    if (bHasRequestedContractDifficulty == true)
    {
        MDGameState->SetContractDifficulty(RequestedContractDifficulty);
    }

    // 출발 콘솔을 거치지 않은 직접 맵 실행에서는 미확정 인원 상태 유지
    if (bHasRequestedContractCrewCount == true)
    {
        MDGameState->SetContractCrewCount(RequestedContractCrewCount);
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

    const EMD_InspectionResult Result = FinalizeDay();

    RouteAfterDay(Result);
}

void AMD_GameModeBase::ConfirmDayTransition(AMD_PlayerController* RequestingController)
{
    // EndDay 정산이 확정되기 전에 임의 전환 요청을 실행하지 않도록 하는 검사
    if (bDayFinalized == false)
    {
        return;
    }

    // 먼저 도착한 확인 요청만 실제 전환을 실행하도록 하는 중복 검사
    if (bDayTransitionConfirmed == true)
    {
        return;
    }

    // 유효하지 않은 컨트롤러를 통한 전환 요청을 거부하는 검사
    if (IsValid(RequestingController) == false)
    {
        return;
    }

    // 이전 월드나 다른 월드의 컨트롤러 요청이 현재 전환에 섞이지 않도록 하는 검사
    if (RequestingController->GetWorld() != GetWorld())
    {
        return;
    }

    // 동시에 도착한 여러 플레이어의 확인 RPC가 맵 이동을 중복 호출하지 않도록 하는 선점 상태
    bDayTransitionConfirmed = true;

    // 보고서 확인으로 전환이 확정된 시점부터 모든 클라이언트의 출발 연출을 시작하기 위한 상태 변경
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
    if (IsValid(MDGameState) == true)
    {
        MDGameState->SetWithdrawalPhase(EMD_WithdrawalPhase::Departed);
    }

    // 판정 종류와 관계없이 보고서를 닫고 동일한 출발 연출을 노출하도록 하는 클라이언트 상태 변경
    UWorld* World = GetWorld();
    if (IsValid(World) == true)
    {
        for (FConstPlayerControllerIterator ControllerIterator = World->GetPlayerControllerIterator(); ControllerIterator; ++ControllerIterator)
        {
            AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(ControllerIterator->Get());
            if (IsValid(PlayerController) == true)
            {
                PlayerController->ClientCloseDailyReport();
            }
        }
    }

    // 기존 판정별 딜레이 선택은 유지하고 시작 시점만 보고서 확인 이후로 제한하는 처리
    const bool bResultScreen = PendingResult == EMD_InspectionResult::ContractCleared || PendingResult == EMD_InspectionResult::GameOver;
    GetWorldTimerManager().SetTimer(DepartTravelTimerHandle, this, &AMD_GameModeBase::ExecuteDayTransition, bResultScreen ? ResultTravelDelay : DepartTravelDelay, false);
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

    // 복귀 구역 샘플 제출을 퀘스트 실패 확정보다 먼저 반영하기 위한 마감 순서
    FinalizeReturnStatuses();
    FinalizeDeliveredSamples();

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

    FinalizeDailyScore();

    PendingResult = EvaluateInspection();

    CaptureDaySnapshot();

    return PendingResult;
}

bool AMD_GameModeBase::DoesSiteUseCurrentWorld(UMD_SiteDefinition* Site, UWorld* World) const
{
    if (IsValid(Site) == false || IsValid(World) == false)
    {
        return false;
    }

    // PIE 접두사를 제거한 전체 오브젝트 경로를 비교하여 같은 이름의 다른 패키지를 오인하지 않는다.
    const FSoftObjectPath SiteMapPath = Site->QuestMap.ToSoftObjectPath();
    const FSoftObjectPath CurrentWorldPath = MakeWorldAssetReference(World).ToSoftObjectPath();
    return SiteMapPath.IsNull() == false && SiteMapPath == CurrentWorldPath;
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
    AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>();
    const int32 Day = IsValid(GS) == true ? GS->GetCurrentDay() : 0;

    // Day4 상한 가드: 4일차 이후 '계속' 결과(통과/경고/비상)는 나올 수 없음 → 게임오버 강제
    if (Day >= 4 && Result != EMD_InspectionResult::ContractCleared)
    {
        Result = EMD_InspectionResult::GameOver;
    }

    PendingResult = Result;
    bDayTransitionConfirmed = false;

    // 경고 상태와 보고서 크레딧에 사용할 서버 상인 원장 참조
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;

    // 경고/비상 경제 컨텍스트를 다음 Day로 전달
    if (IsValid(Merchant) == true)
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

    // Day 1처럼 점검 기준이 없는 날에도 DailyReport가 표시될 수 있도록 하는 결과 보정
    FMD_InspectionReport DailyReport;
    if (IsValid(GS) == true)
    {
        DailyReport = GS->GetInspectionReport();

        // Day 1처럼 기존 점검 보고서가 없는 경우에만 누적 점수와 기준을 새로 구성하는 처리
        if (DailyReport.bValid == false)
        {
            DailyReport.Score = GS->GetScore();
            DailyReport.Threshold = GS->GetInspectionThreshold(Day);
        }

        DailyReport.bValid = true;
        DailyReport.Day = Day;
        DailyReport.Result = Result;
        DailyReport.DailyScore = GS->GetScoreForDay(Day);
        DailyReport.Credits = IsValid(Merchant) == true ? Merchant->GetCredits() : 0;
        GS->SetInspectionReport(DailyReport);
    }

    // 서버가 확정한 동일한 결과 화면을 각 로컬 플레이어에게 생성하도록 하는 클라이언트 알림
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        return;
    }

    for (FConstPlayerControllerIterator ControllerIterator = World->GetPlayerControllerIterator(); ControllerIterator; ++ControllerIterator)
    {
        AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(ControllerIterator->Get());
        if (IsValid(PlayerController) == true)
        {
            PlayerController->ClientShowDailyReport(DailyReport);
        }
    }
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
    // 클리어 이력 등록 후 남은 반복 퀘스트의 미회수 정산과 같은 현장 재진입 판정을 위한 기존 계약 종료
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
    if (IsValid(Merchant) == true)
    {
        Merchant->MarkSiteCleared();
        Merchant->FinalizeContract();
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

    // 게임 세션 종료와 로비 복귀를 하나의 비동기 절차로 처리할 세션 관리자 참조
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MultiplayManager* MultiplayManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MultiplayManager>() : nullptr;
    if (IsValid(MultiplayManager) == false)
    {
        UE_LOG(LogTemp, Error, TEXT("[Day] 세션 종료와 로비 이동을 처리할 MultiplayManager가 존재하지 않습니다."));
        return;
    }

    // 기존 Listen Server 연결을 유지하면서 세션 상태와 광고 맵을 로비 기준으로 갱신하는 처리
    MultiplayManager->EndSession(Package);
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

void AMD_GameModeBase::FinalizeDeliveredSamples()
{
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
    if (IsValid(Merchant) == false)
    {
        return;
    }

    // 마감 순간 샘플의 제출 여부를 위치로 판정하기 위한 복귀 구역 목록
    TArray<AMD_ReturnZone*> ReturnZones;
    for (TActorIterator<AMD_ReturnZone> It(GetWorld()); It; ++It)
    {
        ReturnZones.Add(*It);
    }

    for (TActorIterator<AMD_SampleItem> It(GetWorld()); It; ++It)
    {
        AMD_SampleItem* Sample = *It;
        if (IsValid(Sample) == false)
        {
            continue;
        }

        // 복귀 구역 경계 내부에 존재하는 샘플만 자동 제출 대상으로 인정하는 위치 판정
        FVector SampleLocation = Sample->GetActorLocation();
        bool bDelivered = ReturnZones.ContainsByPredicate(
            [&SampleLocation](const AMD_ReturnZone* Zone)
            {
                bool bContainsSample = IsValid(Zone) == true && Zone->ContainsPoint(SampleLocation) == true;
                return bContainsSample;
            });
        if (bDelivered == false)
        {
            continue;
        }

        // 통합된 제출 경로에서 액터 검증과 제거가 성공한 경우에만 퀘스트 진행도를 반영한다.
        bool bSampleDelivered = Merchant->TryDeliverQuestItem(Sample);
        if (bSampleDelivered == false)
        {
            continue;
        }
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
    if (CurrentSite != ResolvedSite || Merchant->GetMainQuests().IsEmpty() == true)
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
