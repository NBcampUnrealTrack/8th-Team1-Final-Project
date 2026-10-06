// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "World/Sample/MD_QuestSampleSpawnerManager.h"
#include "World/Wall/MD_WallShadowBatchSubsystem.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Gameplay/Persistence/MD_DayPersistenceSubsystem.h"
#include "Gameplay/Scrape/MD_ScrapeInitializationSubsystem.h"
#include "Game/MD_GameStateBase.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Player/MD_PlayerController.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "World/Return/MD_ReturnZone.h"
#include "World/Sample/MD_SampleItem.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallPiece.h"
#include "World/Trash/MD_TrashSpawnManager.h"
#include "Online/Session/MD_SessionCarrySubsystem.h"
#include "Gameplay/Loading/MD_MapRegistrationSubsystem.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"

AMD_GameModeBase::AMD_GameModeBase()
{
    PlayerStateClass = AMD_PlayerState::StaticClass();

    bUseSeamlessTravel = true;
}

void AMD_GameModeBase::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    if (auto* GI=GetGameInstance())
    {
        if (auto* Persistence=GI->GetSubsystem<UMD_DayPersistenceSubsystem>())
            Persistence->PrepareContaminationContext(GetWorld());
    }

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

    // 게임 맵의 액터 초기화 이후 월드 준비와 계약 동기화를 위한 다음 틱 예약
    GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);

    // 레벨의 모든 격벽에 붕괴·철거 점수 처리 바인딩 (GameMode는 서버 전용이라 중복 적용 없음)
    for (TActorIterator<AMD_CuttableWall> It(GetWorld()); It; ++It)
    {
        It->OnStrutCollapsed.AddDynamic(this, &AMD_GameModeBase::HandleStrutCollapsed);
        It->OnWallDemolished.AddDynamic(this, &AMD_GameModeBase::HandleWallDemolished);
    }

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
    // 맵 이동 후 GameInstance 타이머 관리자의 이전 현장 예약 해제
    GetWorldTimerManager().ClearTimer(LoanInterestTimerHandle);
    // 종료된 월드에서 정산 완료 이동 콜백 실행 방지
    GetWorldTimerManager().ClearTimer(DepartTravelTimerHandle);

    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->OnDayChanged.RemoveDynamic(this, &AMD_GameModeBase::HandleGameDayChanged);
    }

    Super::EndPlay(EndPlayReason);
}

void AMD_GameModeBase::StartPlay()
{
    if (const auto* Registration = GetWorld()->GetSubsystem<UMD_MapRegistrationSubsystem>(); Registration && Registration->IsPending())
    {
        bStartPlayWaitingForMapRegistration = true;
        return;
    }
    bStartPlayWaitingForMapRegistration = false;
    auto WaitingPlayers = MoveTemp(PendingMapRegistrationPlayers);
    PendingMapRegistrationPlayers.Reset();
    for (const auto& Weak : WaitingPlayers)
        if (AController* Player = Weak.Get(); Player && !Player->IsActorBeingDestroyed() && Player->GetWorld() == GetWorld()) RestartPlayer(Player);
    // Restore players before actor BeginPlay, matching the normal seamless-travel order.
    Super::StartPlay();
}

void AMD_GameModeBase::FinishMapRegistration()
{
    if (bStartPlayWaitingForMapRegistration) StartPlay();
}

void AMD_GameModeBase::RestartPlayer(AController* NewPlayer)
{
    if (!IsValid(NewPlayer) || NewPlayer->IsActorBeingDestroyed()) return;
    if (const auto* Registration = GetWorld()->GetSubsystem<UMD_MapRegistrationSubsystem>(); Registration && Registration->IsPending())
    {
        PendingMapRegistrationPlayers.AddUnique(NewPlayer);
        return;
    }
    Super::RestartPlayer(NewPlayer);

    // 트래블 전이나 로그아웃 때 보관한 핫바와 구매 이력을 새 Pawn에 적용
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SessionCarrySubsystem* Carry = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SessionCarrySubsystem>() : nullptr;
    if (IsValid(Carry) == true && IsValid(NewPlayer->PlayerState) == true)
    {
        Carry->RestorePlayer(NewPlayer->PlayerState);
    }
}

void AMD_GameModeBase::GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList)
{
    Super::GetSeamlessTravelActorList(bToTransition, ActorList);

    // 출발 맵에서만 수집하여 전환 맵의 임시 장비로 기존 기록을 덮어쓰는 상황 방지
    if (bToTransition == false)
    {
        return;
    }
    // 기존 캐릭터가 제거되기 전에 개인 기록을 보관할 게임 인스턴스 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 맵 이동 중에도 유지되는 운반 보관소 조회
    UMD_SessionCarrySubsystem* Carry = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SessionCarrySubsystem>() : nullptr;
    // 보관소가 없는 이동의 개인 수집 생략
    if (IsValid(Carry) == false)
    {
        return;
    }
    // 현재 접속 플레이어 목록이 준비된 경우에만 장비 수집
    if (IsValid(GameState) == false)
    {
        return;
    }
    // 호스트와 접속 클라이언트의 최신 장비를 각 개인 기록에 반영
    for (APlayerState* PlayerState : GameState->PlayerArray)
    {
        // 메모리에 보관하여 목적지의 캐릭터 재생성에 대비
        Carry->CapturePlayer(PlayerState);
    }
}

void AMD_GameModeBase::Logout(AController* Exiting)
{
    PendingMapRegistrationPlayers.Remove(Exiting);
    // PlayerState 제거 전에 퇴장 이름을 남은 플레이어에게 전달
    NotifyPlayerJoinAndExit(Exiting, false);

    // 재접속에 대비해 퇴장 플레이어의 핫바와 구매 이력 보관
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SessionCarrySubsystem* Carry = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SessionCarrySubsystem>() : nullptr;
    if (IsValid(Carry) == true && IsValid(Exiting) == true)
    {
        Carry->CapturePlayer(Exiting->PlayerState);
    }

    Super::Logout(Exiting);
}

void AMD_GameModeBase::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    // 로그인 완료 후 참여자 본인을 포함한 모든 플레이어에게 전달
    NotifyPlayerJoinAndExit(NewPlayer, true);
}

void AMD_GameModeBase::NotifyPlayerJoinAndExit(AController* ChangedPlayer, bool bIsJoin)
{
    // 접속 변경 주체가 없는 경우 알림 생략
    if (IsValid(ChangedPlayer) == false)
    {
        return;
    }

    // 퇴장 이후에도 사용할 이름을 복사하기 위한 상태 및 월드 참조
    APlayerState* ChangedPlayerState = ChangedPlayer->PlayerState;
    UWorld* World = GetWorld();
    // 이름 또는 수신자 조회가 불가능한 경우 알림 생략
    if (IsValid(ChangedPlayerState) == false || IsValid(World) == false)
    {
        return;
    }

    // 액터 수명과 독립적인 알림용 이름 스냅샷 생성
    FString PlayerName = ChangedPlayerState->GetPlayerName();
    FText PlayerNameText = FText::FromString(PlayerName);
    // 서버의 각 컨트롤러를 통해 해당 소유 클라이언트로 전달
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        // 프로젝트 HUD를 사용하는 수신자 선택
        AMD_PlayerController* Recipient = Cast<AMD_PlayerController>(It->Get());
        // 무효한 수신자와 연결을 종료하는 당사자 제외
        if (IsValid(Recipient) == false || (bIsJoin == false && Recipient == ChangedPlayer))
        {
            continue;
        }

        // 원격 클라이언트 및 리슨 서버 호스트의 로컬 HUD로 전달
        Recipient->ClientNotifyPlayerJoinAndExit(PlayerNameText, bIsJoin);
    }
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

void AMD_GameModeBase::StartLoanInterestTimer()
{
    // 타이머를 소유할 현재 서버 월드 조회
    UWorld* World = GetWorld();
    // 로비와 클라이언트 및 이미 정산된 작업의 이자 예약 차단
    if (IsValid(World) == false || World->GetNetMode() == NM_Client || bDayFinalized == true || IsValid(ResolveExpeditionSite()) == false)
    {
        // 게임 현장 외 이자 부과 방지
        return;
    }
    // 일차 동기화나 재호출로 첫 이자 시점이 연장되는 상황 차단
    if (GetWorldTimerManager().TimerExists(LoanInterestTimerHandle) == true)
    {
        // 현재 현장의 최초 예약 유지
        return;
    }
    // 초기화 대기 시간도 맵 진입 이후 경과 시간에 포함
    GetWorldTimerManager().SetTimer(LoanInterestTimerHandle, this, &ThisClass::CheckGameTimeEvents, 180.f, true,
        FMath::Max(600.f - World->GetTimeSeconds(), KINDA_SMALL_NUMBER));
}

void AMD_GameModeBase::CheckGameTimeEvents()
{
    // 이자 원장을 보유한 게임 인스턴스 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 정산 종료 이후 호출과 유효하지 않은 인스턴스 차단
    if (HasAuthority() == false || bDayFinalized == true || IsValid(GameInstance) == false)
    {
        // 종료된 작업의 부채 변경 방지
        return;
    }
    // 팀 공용 부채를 관리하는 서버 원장 조회
    UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
    // 유효한 계약의 미상환 잔액에만 주기 이자 적용
    if (IsValid(Merchant) == true && Merchant->HasActiveContract() == true)
    {
        // 원장 변경 알림을 통한 호스트 UI 및 클라이언트 복제 갱신
        Merchant->ApplyDailyLoanInterest();
    }
}

void AMD_GameModeBase::RequestEndDay(EMD_DayEndReason EndReason, FGameplayTag ReportWidgetTag)
{
    // 서버의 수동 퇴근만 허용하고 시간 만료 및 중복 요청 차단
    if (HasAuthority() == false || EndReason != EMD_DayEndReason::Console || bDayFinalized == true)
    {
        return;
    }
    // 월드 초기화가 끝난 보고서만 최종 정산 대상으로 허용
    AMD_GameStateBase* State = GetGameState<AMD_GameStateBase>();
    // 로딩 중이나 게임 상태가 없는 요청의 불완전한 정산 차단
    if (IsValid(State) == false || State->IsWorldInitializationReady() == false)
    {
        return;
    }
    // 수동 정산 이후 보고서 대기 중 추가 이자 부과 방지
    GetWorldTimerManager().ClearTimer(LoanInterestTimerHandle);
    // 확정된 정산을 전체 플레이어에게 전달하고 로비 복귀 예약
    RouteAfterDay(FinalizeDay(), EndReason, ReportWidgetTag);
}


void AMD_GameModeBase::PreparePlayersForTravel(EMD_LoadingContext Context)
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        if (AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(Iterator->Get()))
        {
            PlayerController->ClientPrepareLoadingTransition(Context);
        }
    }
}


EMD_InspectionResult AMD_GameModeBase::FinalizeDay()
{
    // 여러 플레이어의 퇴근 요청에도 점수와 보상을 한 번만 정산
    if (bDayFinalized == true)
    {
        return PendingResult;
    }

    bDayFinalized = true;

    // 일일 점수 정산에 반영할 플레이어 복귀 상태 확정
    FinalizeReturnStatuses();

    FinalizeDailyScore();

    // 다음 일차 점검 없이 현재 작업의 정산 결과만 확정
    PendingResult = EMD_InspectionResult::NotApplicable;

    // 현재 작업의 최종 평가 점수를 크레딧 연동 보너스로 지급
    AMD_GameStateBase* ScoreGameState = GetGameState<AMD_GameStateBase>();
    bool bScoreGameStateValid = IsValid(ScoreGameState);
    if (bScoreGameStateValid == true)
    {
        // 누적 점수가 아니라 현재 Day의 순 평가 점수만 보너스 기준으로 사용
        int32 CurrentDay = ScoreGameState->GetCurrentDay();
        int32 DailyScore = ScoreGameState->GetScoreForDay(CurrentDay);
        int32 DailyScoreCreditReward = FMath::Max(DailyScore, 0);

        // 음수인 당일 점수는 추가 크레딧 차감 없이 보너스 0으로 처리
        if (DailyScoreCreditReward > 0)
        {
            UGameInstance* GameInstance = GetGameInstance();
            bool bGameInstanceValid = IsValid(GameInstance);
            if (bGameInstanceValid == true)
            {
                UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
                bool bMerchantValid = IsValid(Merchant);
                if (bMerchantValid == true)
                {
                    // 당일 점수 보상을 보고서의 평가 금액 행에 함께 기록
                    Merchant->AddCredits(EMD_DailyReportCategory::Evaluation, DailyScoreCreditReward);
                }
            }
        }
    }

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

void AMD_GameModeBase::RouteAfterDay(EMD_InspectionResult Result, EMD_DayEndReason EndReason, FGameplayTag ReportWidgetTag)
{
    AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>();
    // 기존 원장 형식으로 현재 작업의 집계 구간 조회
    int32 Day = IsValid(GS) == true ? GS->GetCurrentDay() : 0;

    // 모든 수동 퇴근에 동일한 일반 정산 결과 사용
    PendingResult = Result;
    // 보고서에 포함할 현장 및 재화 원장 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 게임 인스턴스 수명의 기존 부채와 재화 보존
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;

    // 이전 점검 결과를 재사용하지 않는 현재 작업의 확정 보고서
    FMD_InspectionReport DailyReport;
    if (IsValid(GS) == true)
    {
        // 퇴근 시점의 평가 점수 반영
        DailyReport.Score = GS->GetScore();
        DailyReport.bValid = true;
        DailyReport.Day = Day;
        DailyReport.Result = Result;
        // 서버에서 검증한 수동 퇴근 원인 보존
        DailyReport.EndReason = EndReason;
        // 현재 작업의 누적 점수 기록
        DailyReport.CumulativeScore = GS->GetScore();
        // 계약 종료 시 다음 점검이 없는 상태 표시
        DailyReport.NextThreshold = 0;
        // 이전 보고서의 보유 잔액 초기화
        DailyReport.Credits = 0;
        // 이전 현장 표시 초기화
        DailyReport.SiteName = FText::GetEmpty();
        DailyReport.DailyScore = GS->GetScoreForDay(Day);
        // 일일 보고서가 서버 점수 Map을 그대로 전달받도록 구성
        DailyReport.DailyScoreTotals = GS->GetDailyScoreTotals();

        if (IsValid(Merchant) == true)
        {
            DailyReport.DailyCreditTotals = Merchant->GetDailyCreditTotals();
            // 최종 일일 정산과 같은 시점의 보유 크레딧 기록
            DailyReport.Credits = Merchant->GetCredits();
            // 현재 계약의 현장 데이터 조회
            UMD_SiteDefinition* Site = Merchant->GetCurrentSite();
            // 유효한 현장 이름만 확정 보고서에 전달
            if (IsValid(Site) == true)
            {
                // 클라이언트 복제 순서와 무관한 현장 표시값 확정
                DailyReport.SiteName = Site->SiteName;
            }
        }
        GS->SetInspectionReport(DailyReport);
    }

    // 하루 마감 진입점의 단일 실행 보장 이후 출발 상태 반영
    if (IsValid(GS) == true)
    {
        // 모든 클라이언트에 출발 단계 전달
        GS->SetWithdrawalPhase(EMD_WithdrawalPhase::Departed);
    }
    // 별도 화면 태그가 없는 수동 요청에 등록된 기본 보고서 사용
    if (ReportWidgetTag.IsValid() == false)
    {
        // UI Manager 설정에 등록된 보고서 조회 태그
        ReportWidgetTag = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.GameHUD.Report"));
    }
    // 클라이언트의 남은 시간 계산에 사용할 서버 종료 시각
    double EndServerTime = GetWorld()->GetTimeSeconds() + 5.0;
    // 현재 접속한 각 플레이어의 소유 연결에 동일한 최종 정산 전달
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        // 소유 클라이언트 RPC를 제공하는 플레이어 선택
        AMD_PlayerController* Controller = Cast<AMD_PlayerController>(It->Get());
        if (IsValid(Controller) == true)
        {
            // 보고서 미표시 플레이어도 서버 정산 화면 생성
            Controller->ClientShowDayEndReport(ReportWidgetTag, DailyReport, EndServerTime);
        }
    }
    // 재클릭으로 연장되지 않는 서버 기준 5초 정산 표시
    GetWorldTimerManager().SetTimer(DepartTravelTimerHandle, this, &AMD_GameModeBase::FinishDayEndReport, 5.0f, false);
}

void AMD_GameModeBase::FinishDayEndReport()
{
    // 서버가 예약한 정산 종료만 한 번 처리
    if (HasAuthority() == false || bDayFinalized == false || DepartTravelTimerHandle.IsValid() == false)
    {
        return;
    }
    // 재호출과 종료 후 타이머 실행 방지
    GetWorldTimerManager().ClearTimer(DepartTravelTimerHandle);
    // 로딩 및 맵 이동 전에 모든 소유 클라이언트의 정산 화면 종료
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        // 종료 RPC를 받을 접속 플레이어 확인
        AMD_PlayerController* Controller = Cast<AMD_PlayerController>(It->Get());
        if (IsValid(Controller) == true)
        {
            // 각 UI Manager가 보관한 정산 화면만 비활성화
            Controller->ClientCloseDayEndReport();
        }
    }
    // 정산 결과와 무관하게 모든 접속자의 로비 복귀 실행
    ExecuteDayTransition();
}

void AMD_GameModeBase::ExecuteDayTransition()
{
    // 재화와 개인 장비를 보존하는 게임 인스턴스 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 현장 계약만 종료하고 누적 재화 및 부채 유지
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
    // 유효한 원장의 현장 진행 상태 정리
    if (IsValid(Merchant) == true)
    {
        // 다음 출항에서 새로운 현장 계약 초기화 허용
        Merchant->FinalizeContract();
    }
    // 이전 현장의 오염 상태가 로비 또는 다음 출항에 적용되는 상황 방지
    UMD_DayPersistenceSubsystem* Persistence = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_DayPersistenceSubsystem>() : nullptr;
    // 유효한 현장 상태 보관소만 정리
    if (IsValid(Persistence) == true)
    {
        // 개인 로어와 장비 이관에 영향을 주지 않는 현장 오염 상태 해제
        Persistence->ResetContaminationContext();
    }
    // 모든 정산 결과의 목적지를 로비 맵으로 고정
    TravelToLevel(TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_Lobby.L_Lobby"))));
}


void AMD_GameModeBase::TravelToLevel(const TSoftObjectPtr<UWorld>& Level)
{
    // 서버가 지정한 로비 에셋의 패키지 경로 조회
    FString Package = Level.ToSoftObjectPath().GetLongPackageName();
    // 접속 플레이어 전체 이동을 수행할 서버 월드 조회
    UWorld* World = GetWorld();
    // 잘못된 목적지를 같은 현장 재시작으로 대체하지 않도록 차단
    if (IsValid(World) == false || HasAuthority() == false || Package.IsEmpty() == true)
    {
        return;
    }
    // 온라인 세션의 종료와 로비 광고 갱신을 담당할 관리자 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 독립 실행과 실제 진행 중인 온라인 세션 구분
    UMD_MultiplayManager* MultiplayManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MultiplayManager>() : nullptr;
    // 진행 중인 세션은 기존 연결을 유지하는 종료 절차 사용
    if (IsValid(MultiplayManager) == true && MultiplayManager->IsSessionInProgress() == true)
    {
        // 세션 상태와 참가 허용 및 광고 맵을 갱신한 뒤 전체 이동
        MultiplayManager->EndSession(Package);
        return;
    }
    // 세션을 만들지 않은 실행에서도 전체 플레이어의 로딩 시작
    PreparePlayersForTravel(EMD_LoadingContext::MissionIncomplete);
    // 실제 맵 이동 전에 로딩 인트로 완료 대기
    UMD_LoadingScreenSubsystem* LoadingScreen = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_LoadingScreenSubsystem>() : nullptr;
    // 비동기 인트로 중 종료된 월드 접근 방지
    TWeakObjectPtr<UWorld> WeakWorld(World);
    // 서버 트래블로 호스트와 접속 플레이어의 목적지 통일
    auto StartTravel = [WeakWorld, Package]()
    {
        // 인트로 완료 시점에 여전히 살아 있는 서버 월드 조회
        UWorld* TravelWorld = WeakWorld.Get();
        // 유효한 월드에서만 로비 이동 요청
        if (IsValid(TravelWorld) == true && TravelWorld->ServerTravel(Package) == false)
        {
            // 실패를 같은 현장 재시작으로 숨기지 않는 오류 기록
            UE_LOG(LogTemp, Error, TEXT("[Report] 로비 맵 이동을 시작하지 못했습니다."));
        }
    };
    // 로딩 시스템이 있는 실행의 인트로 완료 후 이동
    if (IsValid(LoadingScreen) == true)
    {
        // 현재 로딩 인트로를 유지한 단일 이동 예약
        LoadingScreen->RunAfterIntro(MoveTemp(StartTravel));
    }
    else
    {
        // 로딩 화면이 없는 독립 실행의 직접 이동
        StartTravel();
    }
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


void AMD_GameModeBase::InitializeExpeditionDay()
{
    if (UMD_TrashSpawnManager* Trash = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
    {
        bool bTrashReady = false;
        if (!Trash->PrepareGenerators(bTrashReady))
        {
            UE_LOG(LogTemp, Error, TEXT("Trash floors could not be prepared; expedition initialization stopped."));
            return;
        }
        if (!bTrashReady)
        {
            GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);
            return;
        }
    }
    // 구버전 퀘스트 물품을 새 빈 생존 목록으로 자동 저장하는 상황 방지
    UMD_QuestSampleSpawnerManager* Spawners = GetWorld()->GetSubsystem<UMD_QuestSampleSpawnerManager>();
    // 같은 프레임의 타이머 실행 순서와 무관하게 물품 복구 완료 대기
    if (IsValid(Spawners) == true && Spawners->IsLegacyQuestRecoveryPending() == true)
    {
        // 구버전 복구 후 월드 완료와 자동 저장 재개
        GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);
        // 불완전한 생존 목록의 저장 차단
        return;
    }
    if (const auto* Shadows = GetWorld()->GetSubsystem<UMD_WallShadowBatchSubsystem>(); Shadows && Shadows->IsInitializationPending())
    {
        GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);
        return;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(MD_GameMode_InitializeExpeditionDay);
    if (const auto* Initializer = GetWorld()->GetSubsystem<UMD_ScrapeInitializationSubsystem>(); Initializer && Initializer->IsPending())
    {
        // Contract setup must never run on a partially prepared world.
        GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);
        return;
    }
    // 세이브가 없으므로 모든 출항은 새 계약으로 시작한다.
    AMD_GameStateBase* ExpeditionGameState = GetGameState<AMD_GameStateBase>();
    if (IsValid(ExpeditionGameState) == true)
    {
        // 다음 틱의 스포너 평가보다 먼저 새 계약 확정
        SyncMerchantToGameDay(ExpeditionGameState->GetCurrentDay(), true);
    }

    // BeginPlay registers generators; fresh draws happen once the world is ready.
    if (UMD_TrashSpawnManager* TrashManager = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
    {
        if (!TrashManager->InitializeUnrestoredGenerators())
        {
            UE_LOG(LogTemp, Error, TEXT("Procedural trash initialization failed."));
            return;
        }
    }

    if (auto* GS = GetGameState<AMD_GameStateBase>()) GS->FinishWorldInitialization();
}

void AMD_GameModeBase::HandleGameDayChanged(int32 NewDay)
{
    // 일반 게임 진행에서 발생한 일차 변경을 상인 상태에 반영
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

void AMD_GameModeBase::SyncMerchantToGameDay(int32 Day, bool bStartNewContract)
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

    // 같은 현장의 다음 일차는 유지하되 로비에서 새로 출항한 계약은 재구성
    UMD_SiteDefinition* CurrentSite = Merchant->GetCurrentSite();
    // 명시적인 새 출항 또는 현장 변경 시 계약 초기화
    if (bStartNewContract == true || CurrentSite != ResolvedSite)
    {
        // 새 계약의 일차와 착수금 배율 초기화
        Merchant->BeginContract(ResolvedSite);
    }

    // 저장된 일차와 다를 때만 착수금을 지급하여 복원 시 중복 지급 방지
    if (Merchant->GetCurrentDay() != Day)
    {
        // 복원된 GameState의 비상 Day 상태를 착수금 계산에 전달
        AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
        bool bEmergencyDay = IsValid(MDGameState) == true ? bEmergencyDay = MDGameState->IsEmergencyDay() : false;

        Merchant->BeginDay(Day, ExpeditionAdvancePayment, bEmergencyDay);
    }
    // 착수금과 계약 확정 이후 맵 진입 기준 이자 주기 시작
    StartLoanInterestTimer();

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

void AMD_GameModeBase::MD_SetTime(int32 Hour)
{
    // 이미 정산 중인 날짜의 시간 변경 차단
    if (bDayFinalized == true)
    {
        return;
    }
    // 현재 일자의 시간을 관리하는 서버 상태 조회
    AMD_GameStateBase* State = GetGameState<AMD_GameStateBase>();
    if (IsValid(State) == false)
    {
        return;
    }
    // 현재 일자는 유지하고 지정한 시의 정각으로 변경
    bool bApplied = State->SetGameTimeHour(Hour);
    if (bApplied == false)
    {
        // 설정 범위와 마감 상태 확인을 위한 실패 안내
        UE_LOG(LogTemp, Warning, TEXT("[Time] 시각 설정 실패 %d시 작업 시간 범위와 마감 상태 확인"), Hour);
        return;
    }
    // 적용된 정각 표시
    UE_LOG(LogTemp, Display, TEXT("[Time] 현재 일자 시각 변경 %02d:00"), Hour);
}
