// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameModeBase.h"
#include "World/Sample/MD_QuestSampleSpawnerManager.h"
#include "World/Wall/MD_WallShadowBatchSubsystem.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
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
    // 출발 콘솔이 전달한 난이도 옵션만 계약 난이도로 인정하기 위한 원문 검사
    FString DifficultyOption = UGameplayStatics::ParseOption(Options, TEXT("Difficulty"));
    if (DifficultyOption.IsEmpty() == false)
    {
        // 변조되거나 정의되지 않은 정수 값이 점검 기준을 오염시키지 않도록 하는 서버 검증
        uint8 DifficultyValue = MAX_uint8;
        if (LexTryParseString(DifficultyValue, *DifficultyOption) == true && DifficultyValue >= static_cast<uint8>(EMD_Difficulty::Safe) && DifficultyValue <= static_cast<uint8>(EMD_Difficulty::Dangerous))
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

    // 게임 맵의 액터 초기화 이후 월드 준비와 계약 시작을 위한 다음 틱 예약
    GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::InitializeExpeditionDay);

    // 레벨의 모든 격벽에 철거 점수 처리 바인딩 (GameMode는 서버 전용이라 중복 적용 없음)
    for (TActorIterator<AMD_CuttableWall> It(GetWorld()); It; ++It)
    {
        It->OnWallDemolished.AddDynamic(this, &AMD_GameModeBase::HandleWallDemolished);
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
    // GameInstance 서브시스템에 남은 이 월드의 파산 바인딩 해제
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>())
        {
            Merchant->OnBankrupt.RemoveAll(this);
        }
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

void AMD_GameModeBase::HandleWallDemolished(AMD_CuttableWall* Wall, AActor* Cutter)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        // 벽 철거 +15 (벽 종류 구분 없음)
        GS->AddScoreEvent(MDGameplayTags::Score_Demolition_Partition);
    }
}

// 세션 절차를 거치지 않는 직접 ServerTravel 앞에서 모든 소유 클라이언트의 로딩을 켠다.
// 클라이언트가 서버와 같은 시점에 인트로를 시작해야 하므로 트래블 콜백 안이 아니라 그 전에 호출해야 한다.
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

void AMD_GameModeBase::HandleContractCleared()
{
    // 다음 출항을 새 계약으로 판정하기 위한 기존 계약 종료
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MerchantSubsystem* Merchant = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
    if (IsValid(Merchant) == true)
    {
        Merchant->FinalizeContract();
    }

    // 마감의 목적지는 항상 로비
    TravelToLevel(LobbyLevel);
}

void AMD_GameModeBase::TravelToLevel(const TSoftObjectPtr<UWorld>& Level)
{
    const FString Package = Level.ToSoftObjectPath().GetLongPackageName();

    if (Package.IsEmpty())
    {
        // 잘못된 목적지를 같은 현장 재시작으로 숨기지 않는다. BP GameMode의 LobbyLevel 지정 필요
        UE_LOG(LogTemp, Error, TEXT("[Contract] 로비 레벨 미지정. BP_MDGameModeBase의 LobbyLevel을 설정해야 합니다."));
        return;
    }

    UWorld* World = GetWorld();
    if (IsValid(World) == false || HasAuthority() == false)
    {
        return;
    }

    // 온라인 세션의 종료와 로비 광고 갱신을 담당할 관리자 조회
    UGameInstance* GameInstance = GetGameInstance();
    UMD_MultiplayManager* MultiplayManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MultiplayManager>() : nullptr;
    // 진행 중인 세션은 기존 Listen Server 연결을 유지하는 종료 절차 사용
    if (IsValid(MultiplayManager) == true && MultiplayManager->IsSessionInProgress() == true)
    {
        MultiplayManager->EndSession(Package);
        return;
    }

    // 세션을 만들지 않은 실행(PIE 등)에서도 전체 플레이어의 로딩 시작 후 직접 이동
    PreparePlayersForTravel(EMD_LoadingContext::MissionIncomplete);
    UMD_LoadingScreenSubsystem* LoadingScreen = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_LoadingScreenSubsystem>() : nullptr;
    TWeakObjectPtr<UWorld> WeakWorld(World);
    auto StartTravel = [WeakWorld, Package]()
    {
        UWorld* TravelWorld = WeakWorld.Get();
        if (IsValid(TravelWorld) == true && TravelWorld->ServerTravel(Package) == false)
        {
            UE_LOG(LogTemp, Error, TEXT("[Report] 로비 맵 이동을 시작하지 못했습니다."));
        }
    };
    if (IsValid(LoadingScreen) == true)
    {
        LoadingScreen->RunAfterIntro(MoveTemp(StartTravel));
    }
    else
    {
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
    // 세이브가 없으므로 모든 출항은 새 계약으로 시작한다. 스포너 평가보다 먼저 확정
    BeginExpeditionContract();

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

UMD_SiteDefinition* AMD_GameModeBase::FindExpeditionSiteForWorld(UWorld* World) const
{
    if (IsValid(World) == false)
    {
        return nullptr;
    }
    // PIE에서도 원본 맵 키로 조회하기 위한 현재 월드 소프트 참조
    TSoftObjectPtr<UWorld> CurrentWorldReference = MakeWorldAssetReference(World);
    // 현재 맵과 일대일로 등록된 현장 데이터 조회
    TObjectPtr<UMD_SiteDefinition> RegisteredSite = ExpeditionSitesByMap.FindRef(CurrentWorldReference);
    if (IsValid(RegisteredSite) == true)
    {
        return RegisteredSite.Get();
    }
    // 기존 단일 Site 설정을 사용하는 Blueprint와의 호환 경로
    if (DoesSiteUseCurrentWorld(ExpeditionSite, World) == true)
    {
        return ExpeditionSite;
    }
    return nullptr;
}

UMD_SiteDefinition* AMD_GameModeBase::ResolveExpeditionSite() const
{
    // 현재 월드와 퀘스트 맵이 일치하는 현장만 반환하기 위한 결과
    UMD_SiteDefinition* OutSite = nullptr;

    // PIE 맵 이름까지 포함하여 후보 현장의 실제 실행 맵을 확인하기 위한 월드
    UWorld* World = GetWorld();
    if (IsValid(World) == true)
    {
        OutSite = FindExpeditionSiteForWorld(World);

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

void AMD_GameModeBase::MD_SetCrew(int32 Crew)
{
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->SetContractCrewCount(Crew);
    }
}

void AMD_GameModeBase::BeginExpeditionContract()
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->GetNetMode() == NM_Client)
    {
        return;
    }
    UMD_MerchantSubsystem* Merchant = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
    if (!IsValid(Merchant))
    {
        return;
    }
    UMD_SiteDefinition* ResolvedSite = ResolveExpeditionSite();
    if (IsValid(ResolvedSite) == false)
    {
        UE_LOG(LogTemp, Log, TEXT("[Merchant] 현재 맵에 연결된 원정 Site 없음: %s"), *World->GetMapName());
        return;
    }
    // 새 계약과 착수금 지급
    Merchant->BeginContract(ResolvedSite, ExpeditionAdvancePayment);
    // 서브시스템은 GameInstance 수명이라 이 월드의 GameMode만 받도록 중복 없이 연결. EndPlay에서 해제
    Merchant->OnBankrupt.RemoveAll(this);
    Merchant->OnBankrupt.AddUObject(this, &AMD_GameModeBase::HandleBankruptcy);
    // 착수금과 계약 확정 이후 맵 진입 기준 이자 주기 시작
    StartLoanInterestTimer();
}

void AMD_GameModeBase::HandleBankruptcy()
{
    // 종료 트래블 중에도 초당 차감이 계속 들어오므로 첫 발화만 처리한다
    if (HasAuthority() == false || bDayFinalized == true)
    {
        return;
    }
    bDayFinalized = true;
    GetWorldTimerManager().ClearTimer(LoanInterestTimerHandle);
    GetWorldTimerManager().ClearTimer(DepartTravelTimerHandle);

    // 복귀 상태와 점수는 기록으로 남기되, 평가 보너스 지급은 하지 않는다(잔액을 다시 채우면 안 됨)
    FinalizeReturnStatuses();
    FinalizeDailyScore();

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AMD_PlayerController* Controller = Cast<AMD_PlayerController>(It->Get());
        if (IsValid(Controller) == true)
        {
            Controller->ClientShowHUDMessage(NSLOCTEXT("MDContract", "Bankrupt", "자금이 바닥났습니다. 계약이 강제 종료됩니다."));
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[Contract] 파산 — 정산 보고서 표시 후 로비 이동"));
    // 마감 콘솔과 같은 흐름: 확정 보고서 5초 표시 → FinishDayEndReport → HandleContractCleared(로비)
    ShowFinalReport(FGameplayTag());
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
    // 재호출로 첫 이자 시점이 연장되는 상황 차단
    if (GetWorldTimerManager().TimerExists(LoanInterestTimerHandle) == true)
    {
        // 현재 현장의 최초 예약 유지
        return;
    }
    // 초기화 대기 시간도 맵 진입 이후 경과 시간에 포함. 첫 징수 9분, 이후 3분 주기
    GetWorldTimerManager().SetTimer(LoanInterestTimerHandle, this, &ThisClass::CheckGameTimeEvents,
        FMath::Max(LoanInterestIntervalSeconds, KINDA_SMALL_NUMBER), true,
        FMath::Max(LoanFirstInterestDelaySeconds - World->GetTimeSeconds(), KINDA_SMALL_NUMBER));
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
        // 실제 부과된 금액으로 이자 발생 알림 여부 판정
        int32 InterestAmount = Merchant->ApplyDailyLoanInterest();
        // 완납 또는 부채 상한으로 이자가 추가되지 않은 시점 제외
        if (InterestAmount <= 0)
        {
            // 금액 변화가 없는 알림 종료
            return;
        }
        // 이자 반영 직후 모든 플레이어에게 전달할 공통 부채 확정
        int32 DebtBalance = Merchant->GetDebtBalance();
        // 비율 경계의 곱셈 오버플로 방지를 위한 원금 확장
        int64 Principal = Merchant->GetLoanPrincipal();
        // 상환으로 부채가 원금 이하인 경우 추가 이자 0으로 처리
        int64 AddedInterest = FMath::Max<int64>(static_cast<int64>(DebtBalance) - Principal, 0);
        // 원금 대비 5퍼센트 이하에 해당하는 기본 단계
        int32 NotificationStage = 0;
        // 부동소수점 나눗셈 없이 20퍼센트 초과 판정
        if (Principal > 0 && AddedInterest * 100 > Principal * 20)
        {
            // 가장 높은 독촉 단계 적용
            NotificationStage = 3;
        }
        // 10퍼센트 초과부터 20퍼센트 이하까지 판정
        else if (Principal > 0 && AddedInterest * 100 > Principal * 10)
        {
            // 두 번째 비율 경계 초과 단계 적용
            NotificationStage = 2;
        }
        // 5퍼센트 초과부터 10퍼센트 이하까지 판정
        else if (Principal > 0 && AddedInterest * 100 > Principal * 5)
        {
            // 첫 번째 비율 경계 초과 단계 적용
            NotificationStage = 1;
        }
        // 접속 중인 각 플레이어에게 동일한 부과 결과 전달
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            // 소유 클라이언트의 알림 RPC를 제공하는 컨트롤러 조회
            AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(It->Get());
            // 유효한 플레이어에게만 이자 발생 알림 전송
            if (IsValid(PlayerController) == true)
            {
                // 복제 값의 도착 순서와 무관한 금액 및 단계 전달
                PlayerController->ClientNotifyLoanInterest(InterestAmount, DebtBalance, NotificationStage);
            }
        }
    }
}

void AMD_GameModeBase::RequestEndDay(FGameplayTag ReportWidgetTag)
{
    // 콘솔 중복 상호작용에도 1회만 실행
    if (HasAuthority() == false || bDayFinalized)
    {
        return;
    }
    // 수동 정산 이후 보고서 대기 중 추가 이자 부과 방지
    GetWorldTimerManager().ClearTimer(LoanInterestTimerHandle);
    FinalizeDay();
    ShowFinalReport(ReportWidgetTag);
}

void AMD_GameModeBase::FinalizeDay()
{
    if (bDayFinalized)
    {
        return;
    }
    bDayFinalized = true;

    // 정산에 반영할 플레이어 복귀 상태 확정
    FinalizeReturnStatuses();
    FinalizeDailyScore();

    // 최종 평가 점수를 크레딧 연동 보너스로 지급 (음수면 차감 없이 0)
    AMD_GameStateBase* ScoreGameState = GetGameState<AMD_GameStateBase>();
    if (IsValid(ScoreGameState) == true)
    {
        int32 ScoreCreditReward = FMath::Max(ScoreGameState->GetScore(), 0);
        if (ScoreCreditReward > 0)
        {
            UMD_MerchantSubsystem* Merchant = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
            if (IsValid(Merchant) == true)
            {
                Merchant->AddCredits(EMD_DailyReportCategory::Evaluation, ScoreCreditReward);
            }
        }
    }
}

void AMD_GameModeBase::ShowFinalReport(FGameplayTag ReportWidgetTag)
{
    // 모든 최종 보상 반영 이후의 시각을 그래프 종료점으로 고정
    UMD_MerchantSubsystem* Merchant = GetGameInstance()->GetSubsystem<UMD_MerchantSubsystem>();
    // 유효한 서버 원장에만 시간 확정 요청
    if (IsValid(Merchant) == true)
    {
        // 이후 보고서 표시 대기 시간을 작업 기록에서 제외
        Merchant->FinishCreditHistory();
    }
    // 정산 직후의 서버 원장을 확정 보고서로 사용
    FMD_InspectionReport FinalReport;
    if (AMD_GameStateBase* GS = GetGameState<AMD_GameStateBase>())
    {
        GS->RefreshDailyLedger();
        FinalReport = GS->GetDailyLedger();
        FinalReport.bValid = true;
    }
    // 콘솔이 없는 요청에는 등록된 기본 보고서 태그 사용
    if (ReportWidgetTag.IsValid() == false)
    {
        ReportWidgetTag = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.GameHUD.Report"));
    }
    // 클라이언트의 남은 시간 계산에 사용할 서버 종료 시각
    double EndServerTime = GetWorld()->GetTimeSeconds() + 5.0;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AMD_PlayerController* Controller = Cast<AMD_PlayerController>(It->Get());
        if (IsValid(Controller) == true)
        {
            Controller->ClientShowDayEndReport(ReportWidgetTag, FinalReport, EndServerTime);
        }
    }
    // 서버 기준 5초 정산 표시 뒤 로비 이동
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
    // 맵 이동 전에 모든 소유 클라이언트의 정산 화면 종료
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AMD_PlayerController* Controller = Cast<AMD_PlayerController>(It->Get());
        if (IsValid(Controller) == true)
        {
            Controller->ClientCloseDayEndReport();
        }
    }
    // 마감 = 계약 종료. 항상 로비로 복귀
    HandleContractCleared();
}
