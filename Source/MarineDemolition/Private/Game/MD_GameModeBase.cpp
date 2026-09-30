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
#include "Data/SaveGame/MD_SaveSlotManager.h"
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
        if (auto* Saves=GI->GetSubsystem<UMD_SaveSlotManager>())
            Saves->PrepareContaminationForWorld(GetWorld());
        else if (auto* Persistence=GI->GetSubsystem<UMD_DayPersistenceSubsystem>())
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

    // 서버 월드 수명에 맞추어 저장 이벤트에 게임 진행과 플레이어 소유자 등록
    UGameInstance* SaveGameInstance = GetGameInstance();
    // 유효한 게임 인스턴스의 저장 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = IsValid(SaveGameInstance) == true ? SaveGameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    // 저장 관리자 생성 여부를 확인한 뒤 현재 게임 모드 연결
    if (IsValid(SaveSlotManager) == true)
    {
        // 전체 저장 시 서버 상태 수집 연결
        SaveSlotManager->OnCollectGameData.AddUniqueDynamic(this, &ThisClass::CollectGameData);
        // 전체 복원 시 서버 상태 적용 연결
        SaveSlotManager->OnOverwriteGameData.AddUniqueDynamic(this, &ThisClass::OverwriteGameData);
    }

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

    // Expedition readiness is scheduled after the pending save operation below.

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
    // 트래블로 제거되는 이전 게임 모드의 구독을 해제하기 위한 관리자 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 종료 시 게임 인스턴스가 유효한 경우에만 저장 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    // 다음 월드에 이전 서버 수집기가 참여하지 않도록 구독 해제
    if (IsValid(SaveSlotManager) == true)
    {
        // 이전 월드 수집 연결 해제
        SaveSlotManager->OnCollectGameData.RemoveDynamic(this, &ThisClass::CollectGameData);
        // 이전 월드 적용 연결 해제
        SaveSlotManager->OnOverwriteGameData.RemoveDynamic(this, &ThisClass::OverwriteGameData);
    }

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
        // 월드 복원 전에 모든 개인 기록의 아이템 자산 준비
        bool bAssetsReady = false;
        // 손상된 품목과 로드 실패가 부분 복원으로 이어지는 상황 차단
        if (SaveSlotManager->PrepareSavedAssets(bAssetsReady) == false)
        {
            // 초기화와 자동 저장을 막는 자산 준비 실패 기록
            UE_LOG(LogTemp, Error, TEXT("Saved assets could not be prepared; initialization stopped."));
            // 준비되지 않은 장비로 월드 진입 차단
            return;
        }
        // 방문 구역의 내부 액터가 준비되기 전 저장 데이터 적용 연기
        bool bStreamingReady = false;
        // 사라진 구역과 로더 설정 변경의 실패 검사
        if (SaveSlotManager->PrepareSavedWorldStreaming(bStreamingReady) == false)
        {
            // 초기화와 자동 저장을 막는 구역 복원 실패 기록
            UE_LOG(LogTemp, Error, TEXT("Saved streaming levels could not be restored; initialization stopped."));
            // 불완전한 월드의 정상 진입 차단
            return;
        }
        // 구역과 아이템 로딩을 같은 프레임에 진행하고 둘 다 준비될 때까지 대기
        if (bStreamingReady == false || bAssetsReady == false)
        {
            // 첫 구역 및 아이템 준비 요청의 실시간 기준 보관
            if (SavedStreamingStartSeconds == 0.0)
            {
                // 게임 시간 정지와 무관한 로딩 제한 기준
                SavedStreamingStartSeconds = FPlatformTime::Seconds();
            }
            // 엔진 로드 실패가 무한 대기로 이어지는 상황 차단
            if (FPlatformTime::Seconds() - SavedStreamingStartSeconds > 120.0)
            {
                // 구역 복원 시간 초과 원인 기록
                UE_LOG(LogTemp, Error, TEXT("Saved level or save asset loading timed out; initialization stopped."));
                // 정상 완료와 자동 저장 차단
                return;
            }
            // 구역 로딩을 막지 않는 다음 프레임 재시도
            GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ApplyPendingGameDataOperation);
            // 현재 프레임의 데이터 적용 연기
            return;
        }
        // 추가 구역을 포함한 모든 오염 표면의 초기화 완료 대기
        if (auto* Initializer = GetWorld()->GetSubsystem<UMD_ScrapeInitializationSubsystem>(); IsValid(Initializer) == true && Initializer->IsPending() == true)
        {
            // 신규 구역 표면의 준비를 다음 프레임에서 재확인
            GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ApplyPendingGameDataOperation);
            // 오염 초기화 중 월드 복원 중단
            return;
        }
        // 작업 소비 전에 파일 복원과 새 출항 및 다음 일차 전환을 구분할 요청 보관
        if (UMD_TrashSpawnManager* Trash = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
        {
            bool bTrashReady = false;
            if (!Trash->PrepareGenerators(bTrashReady))
            {
                UE_LOG(LogTemp, Error, TEXT("Trash floors could not be prepared; saved world restoration stopped."));
                return;
            }
            if (!bTrashReady)
            {
                GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::ApplyPendingGameDataOperation);
                return;
            }
        }
        EMD_PendingGameDataOperation PendingOperation = SaveSlotManager->GetPendingGameDataOperation();
        // 파일 불러오기에서 저장된 퀘스트와 시각 유지
        bRestoredSavedWorld = PendingOperation == EMD_PendingGameDataOperation::LoadGame;
        // 작업 없음은 정상 진입으로 유지하고 실제 복원 실패만 중단
        if (PendingOperation != EMD_PendingGameDataOperation::None && SaveSlotManager->ApplyPendingGameDataOperation() == false)
        {
            // 부분 복원 월드의 시간 재개와 자동 저장 차단 원인 기록
            UE_LOG(LogTemp, Error, TEXT("Saved game restoration failed; world initialization and auto-save stopped."));
            // 준비 완료를 게시하지 않고 복원 실패 상태 유지
            return;
        }
        // 새 출항은 작업 소비 직후 계약을 갱신하여 이전 수락 상태로 물품이 생성되는 상황 방지
        if (PendingOperation == EMD_PendingGameDataOperation::NewGame
            || PendingOperation == EMD_PendingGameDataOperation::ContinueGame
            || PendingOperation == EMD_PendingGameDataOperation::NewContract)
        {
            // 목적지 맵의 일차를 새 계약에 적용할 서버 상태 조회
            AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
            // 게임 상태가 준비된 출항에서만 새 퀘스트 구성
            if (IsValid(MDGameState) == true)
            {
                // 다음 틱의 스포너 평가보다 먼저 새 계약 확정
                SyncMerchantToGameDay(MDGameState->GetCurrentDay(), true);
            }
        }

    }
    GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);
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

    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == true && IsValid(NewPlayer->PlayerState) == true)
    {
        SaveSlotManager->LoadPlayerData(NewPlayer->PlayerState);
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
    // 맵 이동 중에도 유지되는 저장 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    // 저장 관리자가 없는 이동의 개인 수집 생략
    if (IsValid(SaveSlotManager) == false)
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
        // 파일 자동 저장 없이 메모리에 보관하여 목적지의 캐릭터 재생성에 대비
        SaveSlotManager->ParseDataFromPlayer(PlayerState);
    }
}

void AMD_GameModeBase::Logout(AController* Exiting)
{
    PendingMapRegistrationPlayers.Remove(Exiting);
    // PlayerState 제거 전에 퇴장 이름을 남은 플레이어에게 전달
    NotifyPlayerJoinAndExit(Exiting, false);

    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == true && IsValid(Exiting) == true)
    {
        SaveSlotManager->ParseDataFromPlayer(Exiting->PlayerState);
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
        // 제한 시간 도달 원인을 보고서까지 전달
        RequestEndDay(EMD_DayEndReason::TimeExpired);
    }
}

void AMD_GameModeBase::RequestEndDay(EMD_DayEndReason EndReason, FGameplayTag ReportWidgetTag)
{
    // 18:00 자동 마감과 인터랙트가 겹쳐도 1회만 실행
    if (bDayFinalized)
    {
        return;
    }

    GetWorldTimerManager().ClearTimer(CheckGameTimeTimerHandle);

    // 정산 결과와 요청 원인을 함께 반영
    RouteAfterDay(FinalizeDay(), EndReason, ReportWidgetTag);
}

void AMD_GameModeBase::TravelToNextDay()
{
    const UMD_DayPersistenceSubsystem* Persistence = GetGameInstance()->GetSubsystem<UMD_DayPersistenceSubsystem>();
    if (!IsValid(Persistence) || !Persistence->HasSnapshot())
    {
        UE_LOG(LogTemp, Error, TEXT("Day snapshot is unavailable; next-day travel is stopped to preserve remaining trash."));
        return;
    }

    PreparePlayersForTravel(EMD_LoadingContext::NextShift);

    // 같은 레벨을 다시 로드 → BeginPlay에서 복원 경로 진입
    RestartLevelAfterLoadingIntro();
}

// 세션 절차를 거치지 않는 직접 ServerTravel 앞에서 모든 소유 클라이언트의 로딩을 켠다.
// 클라이언트가 서버와 같은 시점에 인트로를 시작해야 하므로
// RestartLevelAfterLoadingIntro의 콜백 안이 아니라 그 전에 호출해야 한다.
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

// 같은 레벨 재시작(?Restart)을 로딩 인트로가 끝난 뒤로 미룬다. 세션 이동 경로는
// MD_MultiplayManager::RunAfterLoadingIntro가 같은 역할을 하며, 이 헬퍼는 세션을
// 거치지 않는 Day 전환과 목적지 미지정 fallback 두 곳이 공유한다.
void AMD_GameModeBase::RestartLevelAfterLoadingIntro()
{
    UGameInstance* GameInstance = GetGameInstance();
    UMD_LoadingScreenSubsystem* LoadingScreen = IsValid(GameInstance) == true
        ? GameInstance->GetSubsystem<UMD_LoadingScreenSubsystem>()
        : nullptr;

    // 로딩 서브시스템이 없는 실행에서는 기존 동작대로 즉시 이동한다.
    if (IsValid(LoadingScreen) == false)
    {
        if (UWorld* World = GetWorld())
        {
            World->ServerTravel(TEXT("?Restart"));
        }
        return;
    }

    TWeakObjectPtr<AMD_GameModeBase> WeakThis(this);
    LoadingScreen->RunAfterIntro([WeakThis]()
    {
        AMD_GameModeBase* Self = WeakThis.Get();
        UWorld* TravelWorld = Self != nullptr ? Self->GetWorld() : nullptr;
        if (IsValid(TravelWorld) == false)
        {
            return;
        }
        if (TravelWorld->ServerTravel(TEXT("?Restart")) == false)
        {
            UE_LOG(LogTemp, Error, TEXT("[Day] 같은 레벨 재시작 트래블을 시작하지 못했습니다."));
        }
    });
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

    // 점검 실패 페널티까지 포함한 당일 최종 평가 점수를 크레딧 연동 보너스로 지급
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

void AMD_GameModeBase::RouteAfterDay(EMD_InspectionResult Result, EMD_DayEndReason EndReason, FGameplayTag ReportWidgetTag)
{
    AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>();
    const int32 Day = IsValid(GS) == true ? GS->GetCurrentDay() : 0;

    // Day4 상한 가드: 4일차 이후 '계속' 결과(통과/경고/비상)는 나올 수 없음 → 게임오버 강제
    if (Day >= 4 && Result != EMD_InspectionResult::ContractCleared)
    {
        Result = EMD_InspectionResult::GameOver;
    }

    PendingResult = Result;

    UGameInstance* GameInstance = GetGameInstance();
    // 유효성 검사와 서브시스템 조회에 동일 인스턴스 재사용
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
        // 콘솔 확인과 시간 종료를 구분할 서버 마감 원인 보존
        DailyReport.EndReason = EndReason;
        DailyReport.DailyScore = GS->GetScoreForDay(Day);
        // 일일 보고서가 서버 점수 Map을 그대로 전달받도록 구성
        DailyReport.DailyScoreTotals = GS->GetDailyScoreTotals();

        if (IsValid(Merchant) == true)
        {
            DailyReport.DailyCreditTotals = Merchant->GetDailyCreditTotals();
        }
        GS->SetInspectionReport(DailyReport);
    }

    // 하루 마감 진입점의 단일 실행 보장 이후 출발 상태 반영
    if (IsValid(GS) == true)
    {
        // 모든 클라이언트에 출발 단계 전달
        GS->SetWithdrawalPhase(EMD_WithdrawalPhase::Departed);
    }
    // 시간 종료 등 콘솔이 없는 요청에는 등록된 기본 보고서 태그 사용
    if (ReportWidgetTag.IsValid() == false)
    {
        // UI Manager 설정에 등록된 보고서 조회 태그
        ReportWidgetTag = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.GameMenu.Report"));
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
    // 일반 결과는 다음 일자, 최종 판정은 기존 결과 이동 실행
    ExecuteDayTransition();
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
        Persistence->ResetContaminationContext();
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
        Persistence->ResetContaminationContext();
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
        PreparePlayersForTravel(EMD_LoadingContext::Default);
        RestartLevelAfterLoadingIntro();
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
        // 생성 위치가 복귀 구역과 겹치는 미운반 샘플의 자동 납품 방지
        if (IsValid(Sample) == false || Sample->HasBeenCarried() == false)
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
    else if (Day == 3)
    {
        // 2일차 실패 경고가 남아 있으면 3일차 재실패를 게임 오버로 확정한다.
        if (MDGameState->IsInspectionWarned() == true)
        {
            Result = EMD_InspectionResult::GameOver;
        }
        else
        {
            // 2일차 점검을 통과한 계약에는 비상 4일차 기회를 부여한다.
            Result = EMD_InspectionResult::EmergencyDayOpened;
        }
    }
    else
    {
        Result = EMD_InspectionResult::GameOver;
    }

    // 2일차 점검 실패에만 경고 감점과 다음 점검 기준 상승을 적용한다.
    if (Result == EMD_InspectionResult::WarningIssued)
    {
        MDGameState->AddScoreEvent(MDGameplayTags::Score_Penalty_Warning);
        MDGameState->SetInspectionWarned(true);
    }

    // 경고 없이 3일차 점검에 실패한 경우 비상 4일차 개방 상태만 유지한다.
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
        Persistence->ClearSnapshot();
        Persistence->CaptureSnapshot(GetWorld());
    }
}

bool AMD_GameModeBase::IsAutoSaveAllowed()
{
    // 자동 저장 정책을 적용할 현재 서버 월드 조회
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        // 실행 중인 맵이 없는 상태의 자동 저장 차단
        return false;
    }
    // 에디터 실행 접두사를 제거한 맵 참조로 제외 목록 확인
    return AutoSaveExcludedMaps.Contains(MakeWorldAssetReference(World)) == false;
}

void AMD_GameModeBase::InitializeExpeditionDay()
{
    if (UMD_TrashSpawnManager* Trash = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
    {
        bool bTrashReady = false;
        if (!Trash->PrepareGenerators(bTrashReady))
        {
            UE_LOG(LogTemp, Error, TEXT("Trash floors could not be prepared; day initialization and auto-save stopped."));
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
        // Auto-save and contract setup must never capture a partially prepared world.
        GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);
        return;
    }
    // 파일 복원은 이미 적용됐으므로 다음 일차 경계에서만 스냅샷을 복원한다.
    if (bRestoredSavedWorld == false)
    {
        // 복원 실패 시 새 추첨과 자동 저장으로 기존 잔량을 덮지 않는다.
        if (!RestoreDaySnapshot())
        {
            UE_LOG(LogTemp, Error, TEXT("Day snapshot could not be restored; trash initialization and auto-save are stopped."));
            return;
        }
        // 새 일차의 계약 설정에 사용할 서버 진행 상태 조회
        AMD_GameStateBase* RestoredGameState = GetGameState<AMD_GameStateBase>();
        // 파일 복원이 아닌 새 출항 및 일차 전환의 계약 동기화
        if (IsValid(RestoredGameState) == true)
        {
            // 실제 일차가 바뀐 경로의 착수금과 퀘스트 해금 처리
            SyncMerchantToGameDay(RestoredGameState->GetCurrentDay());
        }
    }

    // BeginPlay registers generators; file/day state is restored before fresh draws.
    bool bTrashReady = true;
    if (UMD_TrashSpawnManager* TrashManager = GetWorld()->GetSubsystem<UMD_TrashSpawnManager>())
    {
        bTrashReady = TrashManager->InitializeUnrestoredGenerators();
        if (!bTrashReady)
        {
            UE_LOG(LogTemp, Error, TEXT("Procedural trash initialization failed; initial auto-save is skipped."));
            return;
        }
    }

    if (auto* GS = GetGameState<AMD_GameStateBase>()) GS->FinishWorldInitialization();

    // 월드 복원과 상인 퀘스트 일차 동기화가 끝난 경계 상태만 자동 저장하는 처리
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (bTrashReady && IsValid(SaveSlotManager) == true)
    {
        // Collection/serialization stays at this boundary; immutable file writes run off-thread.
        SaveSlotManager->AsyncSaveGameData(TEXT("Auto"), true, FOnSaveMDGameDataComplete());
    }
}

void AMD_GameModeBase::HandleGameDayChanged(int32 NewDay)
{
    // 저장 데이터 복원으로 발생한 일차 알림인지 확인할 게임 인스턴스 조회
    UGameInstance* GameInstance = GetGameInstance();
    // 현재 데이터 이벤트 처리 상태를 제공하는 저장 관리자 조회
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    // 복원 중인 상인 원장을 새로운 일차 시작으로 다시 초기화하는 상황 방지
    if (IsValid(SaveSlotManager) == true && SaveSlotManager->IsGameDataOperationInProgress() == true)
    {
        // 각 구독자가 자신의 저장 필드를 복원하도록 일차 동기화 생략
        return;
    }
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
    // 기존 현장과 일치하더라도 명시적인 새 출항에서는 퀘스트 재추첨
    if (bStartNewContract == true || CurrentSite != ResolvedSite || Merchant->GetMainQuests().IsEmpty() == true)
    {
        // 이전 수락 및 실패와 진행도를 새 현장의 초기 퀘스트 목록으로 교체
        Merchant->BeginContract(ResolvedSite);
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
        // 복원된 GameState의 비상 Day 상태를 착수금 계산에 전달
        AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
        bool bEmergencyDay = IsValid(MDGameState) == true ? bEmergencyDay = MDGameState->IsEmergencyDay() : false;

        Merchant->BeginDay(Day, ExpeditionAdvancePayment, bEmergencyDay);
    }
}

bool AMD_GameModeBase::RestoreDaySnapshot()
{
    UMD_DayPersistenceSubsystem* Persistence =
        GetGameInstance()->GetSubsystem<UMD_DayPersistenceSubsystem>();
    return !IsValid(Persistence) || !Persistence->HasSnapshot()
        || Persistence->RestoreSnapshot(GetWorld());
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
    // 기존 알림 및 시간 종료 처리 경로로 진입
    CheckGameTimeEvents();
}

void AMD_GameModeBase::CollectGameData(FMD_GameData& GameData, bool& bWasSuccessful)
{
    // 현재 게임 모드가 소유한 서버 진행 상태 조회
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
    // 서버 진행 상태의 존재와 권한 확인
    if (IsValid(MDGameState) == false || MDGameState->HasAuthority() == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_GameStateData::DataFieldKey.ToString());
        // 진행 상태가 없는 수집 종료
        return;
    }
    // 점수와 일차 정보를 현재 저장 필드에 기록
    if (GameData.AddDataField(FMD_GameStateData::DataFieldKey, MDGameState->GetGameStateSaveData()) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_GameStateData::DataFieldKey.ToString());
        // 실패한 진행 상태 수집 종료
        return;
    }
    // 로그아웃 기록을 유지하면서 현재 접속자의 장비 기록 갱신
    for (APlayerState* PlayerState : MDGameState->PlayerArray)
    {
        // 아직 생성되지 않은 Pawn은 재접속 기록을 유지하여 이후 복원
        if (IsValid(PlayerState) == true && IsValid(PlayerState->GetPawn()) == true && CollectPlayerGameData(PlayerState, GameData) == false)
        {
            // 실제 접속자의 장비 수집 실패를 전체 저장에 반영
            bWasSuccessful = false;
        }
    }
}

void AMD_GameModeBase::OverwriteGameData(const FMD_GameData& GameData, bool& bWasSuccessful)
{
    // 저장된 점수와 일차를 적용할 현재 서버 진행 상태 조회
    AMD_GameStateBase* MDGameState = GetGameState<AMD_GameStateBase>();
    // 전달된 데이터에서 조회할 게임 진행 필드
    const FMD_GameStateData* GameStateData = GameData.FindDataField<FMD_GameStateData>(FMD_GameStateData::DataFieldKey);
    // 게임 진행을 복원할 현재 상태 객체의 유효성 확인
    if (IsValid(MDGameState) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_GameStateData::DataFieldKey.ToString());
        // 게임 진행 대상 부재 시 개인 장비 적용 종료
        return;
    }
    // 전달된 데이터에서 게임 진행 태그의 필드 조회
    if (GameStateData == nullptr)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_GameStateData::DataFieldKey.ToString());
        // 게임 진행 필드 조회 실패 시 적용 종료
        return;
    }
    // 조회를 마친 필드를 현재 게임 상태에 적용
    if (MDGameState->ApplyGameStateSaveData(*GameStateData) == false)
    {
        // 다른 구독자의 성공으로 덮어쓰지 않는 현재 요청의 실패 반영
        bWasSuccessful = false;
        // 실패한 기능의 필드 키 기록
        UE_LOG(LogTemp, Warning, TEXT("저장 데이터 처리 실패: %s"), *FMD_GameStateData::DataFieldKey.ToString());
        // 진행 상태를 복원하지 못한 경우 개인 장비 적용 종료
        return;
    }
    // 같은 데이터 참조에서 현재 접속자의 개인 기록을 찾아 직접 적용
    for (APlayerState* PlayerState : MDGameState->PlayerArray)
    {
        // 신규 접속자와 미생성 Pawn은 개인 기록을 적용할 다음 시점까지 대기
        if (IsValid(PlayerState) == true && IsValid(PlayerState->GetPawn()) == true
            && GameData.PlayerFields.Contains(BuildPlayerPersistenceKey(PlayerState)) == true
            && OverwritePlayerGameData(PlayerState, GameData) == false)
        {
            // 존재하는 장비 기록을 복원하지 못한 결과 전파
            bWasSuccessful = false;
        }
    }
}

FString AMD_GameModeBase::BuildPlayerPersistenceKey(const APlayerState* PlayerState)
{
    // 제거된 플레이어에 대한 저장 키 생성 차단
    if (IsValid(PlayerState) == false)
    {
        // 유효하지 않은 개인 식별자 반환
        return FString();
    }
    // 온라인 계정 식별자를 여러 번 참조하기 위한 조회 결과 보관
    FUniqueNetIdRepl UniqueNetID = PlayerState->GetUniqueId();
    // 온라인 식별자가 없는 환경의 이름 키 대체 준비
    FString NetworkKey = UniqueNetID.IsValid() == true ? UniqueNetID.ToString() : FString();
    // 기존 온라인 우선 및 오프라인 이름 정책에 따른 개인 식별자 반환
    return NetworkKey.IsEmpty() == false ? NetworkKey : PlayerState->GetPlayerName();
}

bool AMD_GameModeBase::CollectPlayerGameData(const APlayerState* PlayerState, FMD_GameData& GameData)
{
    // 다른 월드의 플레이어와 클라이언트 개인 기록 수집 차단
    if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false || PlayerState->GetWorld() != GetWorld())
    {
        // 현재 서버 소유가 아닌 플레이어의 수집 실패 반환
        return false;
    }
    // 재접속 복원에 재사용할 온라인 또는 오프라인 개인 식별자 생성
    FString PlayerKey = BuildPlayerPersistenceKey(PlayerState);
    // 빈 키로 다른 플레이어의 기록을 덮어쓰는 상황 차단
    if (PlayerKey.IsEmpty() == true)
    {
        // 개인 식별자 부재의 실패 반환
        return false;
    }
    // 장비 데이터 소유자인 플레이어 캐릭터 조회
    AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());
    // 장비 저장 기능을 제공하는 핫바 컴포넌트 조회
    UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
    // Pawn 미생성 시 기존 개인 기록을 빈 데이터로 교체하는 상황 차단
    if (IsValid(EquipmentComponent) == false)
    {
        // 준비되지 않은 개인 장비의 수집 실패 반환
        return false;
    }
    // 읽기 전용 수집 대상의 개인 상점 이력 조회
    const AMD_PlayerState* MDPlayerState = Cast<AMD_PlayerState>(PlayerState);
    // 개인 이력 없이 장비만 저장되는 불완전한 수집 차단
    if (IsValid(MDPlayerState) == false)
    {
        // 프로젝트 플레이어 상태가 없는 수집 실패 반환
        return false;
    }
    // 이번 요청에서 갱신할 플레이어별 장비 필드 조회
    FMD_PlayerData& PlayerData = GameData.PlayerFields.FindOrAdd(PlayerKey);
    // 현재 장비의 수량과 충전 상태를 포함한 기존 저장 스냅샷 기록
    PlayerData = EquipmentComponent->BuildEquipmentSaveData();
    // 보유 도구와 동일한 시점의 구매 이력 및 선행 도구 구매 금지 기록
    MDPlayerState->CollectShopPurchaseState(PlayerData);
    // 불완전한 슬롯 상태가 파일에 기록되는 상황 차단
    return PlayerData.IsStructurallyValid();
}

bool AMD_GameModeBase::OverwritePlayerGameData(const APlayerState* PlayerState, const FMD_GameData& GameData)
{
    // 다른 월드의 플레이어와 클라이언트 장비 변경 차단
    if (IsValid(PlayerState) == false || PlayerState->HasAuthority() == false || PlayerState->GetWorld() != GetWorld())
    {
        // 현재 서버 소유가 아닌 플레이어의 적용 실패 반환
        return false;
    }
    // 로그아웃 수집과 동일한 개인 식별자로 저장 기록 조회 준비
    FString PlayerKey = BuildPlayerPersistenceKey(PlayerState);
    // 식별자 없는 개인 기록의 잘못된 적용 차단
    if (PlayerKey.IsEmpty() == true)
    {
        // 개인 식별자 부재의 적용 실패 반환
        return false;
    }
    // 읽기 전용 저장 구조체에서 해당 플레이어의 기록 조회
    const FMD_PlayerData* PlayerData = GameData.PlayerFields.Find(PlayerKey);
    // 저장에 참여하지 않은 신규 플레이어의 기본 장비 유지
    if (PlayerData == nullptr)
    {
        // 복원할 개인 기록 부재 반환
        return false;
    }
    // 실제 장비 복원을 담당할 현재 플레이어 캐릭터 조회
    AMD_Character* PlayerCharacter = Cast<AMD_Character>(PlayerState->GetPawn());
    // 기존 장비 복원 API를 제공하는 핫바 컴포넌트 조회
    UMD_EquipHotbarComponent* EquipmentComponent = IsValid(PlayerCharacter) == true ? PlayerCharacter->GetHotbarComponent() : nullptr;
    // Pawn이 아직 준비되지 않은 플레이어의 적용 연기
    if (IsValid(EquipmentComponent) == false)
    {
        // 장비 복원 대상 부재의 실패 반환
        return false;
    }
    // 실제 장비 소유 Pawn에 연결된 변경 가능한 개인 이력 대상 조회
    AMD_PlayerState* MDPlayerState = PlayerCharacter->GetPlayerState<AMD_PlayerState>();
    // 다른 플레이어 이력의 적용과 연결 전 복원 차단
    if (IsValid(MDPlayerState) == false || MDPlayerState != PlayerState)
    {
        // 장비와 개인 이력을 모두 유지한 실패 반환
        return false;
    }
    // 장비 교체 전에 기존 파일의 업그레이드 이력까지 검증할 임시 기록
    FMD_PlayerData RestoredPlayerData = *PlayerData;
    // 개인 구매 이력과 선행 정의를 검증한 뒤 실제 장비 복원
    if (RestoredPlayerData.MigrateShopPurchaseState() == false || EquipmentComponent->RestoreEquipmentSaveData(RestoredPlayerData) == false)
    {
        // 장비 복원이 실패한 요청의 구매 이력 변경 차단
        return false;
    }
    // 장비 복원 완료 후 개인 이력 교체와 상점 화면 동기화
    return MDPlayerState->RestoreShopPurchaseState(RestoredPlayerData);
}
