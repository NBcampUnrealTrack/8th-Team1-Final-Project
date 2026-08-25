// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Session/MD_MultiplayManager.h"
#include "OnlineSubsystem.h"            // IOnlineSubsystem
#include "OnlineSessionSettings.h"      // FOnlineSessionSettings
#include "Online/OnlineSessionNames.h"  // NAME_GameSession
#include "FindSessionsCallbackProxy.h"  // FBlueprintSessionResult
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/UObjectGlobals.h"
#include "Player/MD_PlayerController.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"

#include "Online/Session/MD_SessionSettingKey.h"
#include "Types/Online/MD_RoomCode.h"
#include "Types/Online/MD_SessionTypes.h"
#include "Data/SaveGame/MD_SaveSlotManager.h"
#include "Util/MD_LogUtility.h"

void UMD_MultiplayManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

    InitOnlineSessionInterface();

    // Session 초대 수락 처리 완료에 대한 이벤트 등록
    if (SessionInterface.IsValid() == true)
    {
        SessionInterface->OnSessionUserInviteAcceptedDelegates.AddUObject(this, &ThisClass::HandleSessionUserInviteAccepted);
    }
}

void UMD_MultiplayManager::Deinitialize()
{
    OnDestroySessionComplete.RemoveAll(this);

    // Online Session의 유효성 검사
    if (SessionInterface.IsValid() == true)
    {
        // PIE 종료 후 에디터 Online Subsystem에 세션이 남지 않도록 현재 월드 유형 확인
        UWorld* World = GetWorld();
        bool bIsPIEWorld = false;
        bool bWorldValid = IsValid(World);
        if (bWorldValid == true)
        {
            bIsPIEWorld = World->WorldType == EWorldType::PIE;
        }

        // 다음 PIE의 호스트 요청을 막는 기존 GameSession을 로컬 Named Session 원장에서 제거
        if (bIsPIEWorld == true)
        {
            FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
            if (ExistingSession != nullptr)
            {
                SessionInterface->RemoveNamedSession(NAME_GameSession);
                UE_LOG(MarineDemolition, Log, TEXT("%s PIE 종료 시 남은 GameSession을 정리했습니다."), *UE_CALL_LOCATIONINFO);
            }
        }

        // Session 생성 완료에 대한 이벤트 연결 해제
        SessionInterface->OnCreateSessionCompleteDelegates.RemoveAll(this);

        // Session 시작 완료에 대한 이벤트 연결 해제
        SessionInterface->OnStartSessionCompleteDelegates.RemoveAll(this);

        // Session 설정 갱신 완료에 대한 이벤트 연결 해제
        SessionInterface->OnUpdateSessionCompleteDelegates.RemoveAll(this);

        // Session 종료 완료에 대한 이벤트 연결 해제
        SessionInterface->OnEndSessionCompleteDelegates.RemoveAll(this);

        // Session 검색 완료에 대한 이벤트 연결 해제
        SessionInterface->OnFindSessionsCompleteDelegates.RemoveAll(this);

        // Session 참여 완료에 대한 이벤트 연결 해제
        SessionInterface->OnJoinSessionCompleteDelegates.RemoveAll(this);

        // Session 제거 완료에 대한 이벤트 연결 해제
        SessionInterface->OnDestroySessionCompleteDelegates.RemoveAll(this);

        // Session 초대 수락 처리 완료에 대한 이벤트 연결 해제
        SessionInterface->OnSessionUserInviteAcceptedDelegates.RemoveAll(this);
    }

    SessionDelegateHandle.Reset();

    SessionInterface.Reset();
    ExternalUIInterface.Reset();

	Super::Deinitialize();
}

void UMD_MultiplayManager::CreateSession(const FString& ServerName, int32 MaxPlayerCount, const FMD_SessionCreateParams& SessionCreateParams)
{
    // 호스팅을 위한 설정 값 유효성 확인
    if (SessionCreateParams.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Listen Server 호스팅을 위한 설정 값이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    if (IsValidMapPackagePath(SessionCreateParams.SessionMapPackagePath) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 맵 패키지 경로가 유효하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *SessionCreateParams.SessionMapPackagePath);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    BeginLoadingScreenForTravel(NSLOCTEXT("MDLoadingScreen", "CreatingSession", "CREW ASSEMBLING • 잠수팀 준비 중"));

    // Online Session 비동기 생성 절차 실행
    CreateSessionInternal(ServerName, MaxPlayerCount, SessionCreateParams);
}

void UMD_MultiplayManager::StartSession(const FMD_GameData& GameData, bool bLoadGame)
{
    // 클라이언트 환경 실행 제한
    UWorld* World = GetWorld();
    if (IsValid(World) == false || World->GetNetMode() == ENetMode::NM_Client)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 시작은 Host 또는 Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 세이브 데이터 관리자 참조
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;
    if (IsValid(SaveSlotManager) == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터를 관리하는 매니저가 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 불러오기 여부와 현재 진행 데이터 존재 여부에 맞는 시작 작업과 목적지 맵을 준비하는 처리
    FString GameMapPackagePath;
    const bool bWasGameDataPrepared = SaveSlotManager->OnTryStartGameWithGameData(GameData, bLoadGame, GameMapPackagePath);
    if (bWasGameDataPrepared == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 데이터 검증 실패로 세션 시작을 취소합니다."), *UE_CALL_LOCATIONINFO);
        OnStartGameComplete.Broadcast(false);
        return;
    }

    // SaveSlotManager가 결정한 시작 방식의 맵이 실제 World 패키지인지 확인하는 처리
    if (IsValidMapPackagePath(GameMapPackagePath) == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 시작에 사용할 맵 패키지 경로가 유효하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *GameMapPackagePath);

        SaveSlotManager->CancelPendingGameDataOperation();
        OnStartGameComplete.Broadcast(false);
        return;
    }

    BeginLoadingScreenForTravel(NSLOCTEXT("MDLoadingScreen", "StartingExpedition", "DIVE IN PROGRESS • 잠수 준비 중"));
    StartSessionInternal(GameMapPackagePath, GameMapPackagePath);
}

void UMD_MultiplayManager::StartSessionByMap(const FString& MapPackagePath, const FString& TravelOptions)
{
    // 클라이언트가 호스트의 Session 상태와 전체 접속자의 이동을 변경하지 못하도록 하는 실행 환경 검사
    UWorld* World = GetWorld();
    if (IsValid(World) == false || World->GetNetMode() == ENetMode::NM_Client)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 지정 맵 Session 시작은 Host 또는 Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // Session 광고와 ServerTravel에 동일한 실제 World 패키지를 사용하기 위한 경로 검사
    if (IsValidMapPackagePath(MapPackagePath) == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 지정 맵 Session 시작에 사용할 맵 패키지 경로가 유효하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *MapPackagePath);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 목적지 맵을 바꾸거나 콘솔 명령 경계를 벗어나는 문자열이 URL 옵션으로 전달되지 않도록 하는 형식 검사
    if (TravelOptions.IsEmpty() == false && (TravelOptions.StartsWith(TEXT("?")) == false || TravelOptions.Contains(TEXT("\r")) == true || TravelOptions.Contains(TEXT("\n")) == true || TravelOptions.Contains(TEXT(" ")) == true))
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 지정 맵 Session 시작에 사용할 Travel 옵션이 유효하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *TravelOptions);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 검증된 목적지 패키지 뒤에 게임 규칙 전달용 URL 옵션만 결합하는 처리
    FString TravelURL = FString::Printf(TEXT("%s%s"), *MapPackagePath, *TravelOptions);
    BeginLoadingScreenForTravel(NSLOCTEXT("MDLoadingScreen", "StartingExpedition", "DIVE IN PROGRESS • 잠수 준비 중"));
    StartSessionInternal(MapPackagePath, TravelURL);
}

void UMD_MultiplayManager::EndSession(const FString& MapPackagePath)
{
    // 클라이언트 환경 실행 제한
    UWorld* World = GetWorld();
    if (IsValid(World) == false || World->GetNetMode() == ENetMode::NM_Client)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 종료는 Host 또는 Server에서만 실행할 수 있습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 잘못된 에셋이나 일반 에셋 패키지를 대상으로 한 게임 종료 맵 이동 방지
    if (IsValidMapPackagePath(MapPackagePath) == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 종료 후 이동할 맵 패키지 경로가 유효하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *MapPackagePath);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않아 게임을 완료할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // Session 모듈에서 현재 진행중인 비동기 절차가 존재하는 경우 함수 게임 완료 요청 거부
    if (SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 진행 중인 이전 작업이 완료되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 호스트만 진행 중인 GameSession을 종료하도록 보장하기 위한 Session 상태 검증
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession == nullptr || ExistingSession->SessionState != EOnlineSessionState::InProgress)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 종료 가능한 진행 상태의 호스팅 중인 GameSession이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    BeginLoadingScreenForTravel(NSLOCTEXT("MDLoadingScreen", "ReturningToLobby", "MISSION INCOMPLETE • 작업 중단 후 복귀 중"));

    // End Session 비동기 요청의 최종 결과 수신을 위한 완료 이벤트 등록
    const FOnEndSessionCompleteDelegate OnEndSessionCompleteDelegate = FOnEndSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleEndSessionComplete, MapPackagePath);
    SessionDelegateHandle = SessionInterface->AddOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);

    // 진행 중인 GameSession의 종료 상태 전환을 위한 비동기 요청
    const bool bWasRequestSuccessful = SessionInterface->EndSession(NAME_GameSession);
    if (bWasRequestSuccessful == false && SessionDelegateHandle.IsValid() == true)
    {
        // 요청 접수 실패 이후 다른 Session 작업을 허용하기 위한 완료 이벤트 정리
        SessionInterface->ClearOnEndSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem에서 GameSession 종료 요청을 접수하지 못했습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::FindSessions(int32 MaxSearchResultCount, const FMD_SessionFindParams& SessionFindParams)
{
    // Find Session 실패 시 반환될 검색 결과
    const TArray<FBlueprintSessionResult> EmptySearchResults;

    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online SubSystem의 Session 모듈이 유효하지 않아 Session 검색을 할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }

    // Session 모듈에서 현재 진행중인 비동기 절차가 존재하는 경우 함수 Session 검색 요청 거부
    if (SessionDelegateHandle.IsValid() == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 진행중인 이전 작업이 완료되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }

    // 최대 검색 결과 수 유효성 검사
    if (MaxSearchResultCount <= 0)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 검색하고자 하는 Sessions의 개수는 1개 이상이어야 합니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }

    // Local Player의 Net ID 얻기 및 유효성 검사
    FUniqueNetIdPtr LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Local Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }

    // Session 검색 결과를 저장할 객체 생성
    TSharedPtr<FOnlineSessionSearch> SessionSearch = MakeShared<FOnlineSessionSearch>();

    // 최대 검색 결과 저장 개수 설정
    SessionSearch->MaxSearchResults = MaxSearchResultCount;

    // Online Subsystem이 NULL Subsystem이면 LAN 검색 사용
    IOnlineSubsystem* DefaultOnlineSubsystem = IOnlineSubsystem::Get();
    SessionSearch->bIsLanQuery = DefaultOnlineSubsystem != nullptr && DefaultOnlineSubsystem->GetSubsystemName() == FName("NULL");

    // 로비 Session 검색 설정
    SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

    // 서로 다른 프로젝트의 Session과 검색 결과를 구분하기 위한 프로젝트 고유값 필터 설정
    SessionSearch->QuerySettings.Set(MD_SessionKey::Key_ProjectID, MD_SessionKey::Value_ProjectID, EOnlineComparisonOp::Equals);

    // 검색하고자 하는 Sever 이름이 설정된 경우
    if (SessionFindParams.ServerName.IsEmpty() == false)
    {
        // 특정 문구가 서버 이름과 동일한 Session 검색 설정
        SessionSearch->QuerySettings.Set(MD_SessionKey::Key_ServerName, SessionFindParams.ServerName, EOnlineComparisonOp::Equals);
    }

    // 검색하고자 하는 RoomCode가 설정된 경우
    if (SessionFindParams.RoomCode.IsValid() == true)
    {
        // 특정 문구가 서버 이름에 포함된 Session 검색 설정
        SessionSearch->QuerySettings.Set(SETTING_SESSIONKEY, SessionFindParams.RoomCode.ToString(), EOnlineComparisonOp::Equals);
    }

    // Online Subsystem의 Session 모듈에 이벤트 등록
    FOnFindSessionsCompleteDelegate OnDestroySessionCompleteDelegate = FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::HandleFindSessionsComplete, SessionSearch);
    SessionDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(OnDestroySessionCompleteDelegate);

    // Session 검색 요청
    bool bWasRequestSuccess = SessionInterface->FindSessions(*LocalPlayerNetID, SessionSearch.ToSharedRef());

    // Session 검색 요청이 정상적으로 처리되었는지 확인
    if (bWasRequestSuccess == false)
    {
        // Online Subsystem의 Session 모듈에 등록된 이벤트 해제
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 Session 검색 요청에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }
}

void UMD_MultiplayManager::JoinSession(const FBlueprintSessionResult& BlueprintSessionResult)
{
    // 참여하려는 Session 정보에 대한 유효성 확인
    if (BlueprintSessionResult.OnlineResult.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 참여하려는 Session 정보가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    BeginLoadingScreenForTravel(NSLOCTEXT("MDLoadingScreen", "JoiningSession", "CREW ASSEMBLING • 잠수팀 준비 중"));

    // Session 참여 함수 호출
    JoinSessionInternal(BlueprintSessionResult.OnlineResult);
}

void UMD_MultiplayManager::DestroySession(FName SessionName)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online SubSystem의 Session 모듈이 유효하지 않아 Session을 제거할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    // Session 제거 요청(비동기) 중복 동작 방지
    if (SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 진행중인 이전 작업이 완료되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    const UWorld* CurrentWorld = GetWorld();
    const FString CurrentMapPackage = IsValid(CurrentWorld)
        ? CurrentWorld->GetOutermost()->GetName()
        : FString();
    const bool bLeavingLobby = CurrentMapPackage.Contains(TEXT("Lobby"), ESearchCase::IgnoreCase);
    BeginLoadingScreenForTravel(bLeavingLobby
        ? NSLOCTEXT("MDLoadingScreen", "LeavingLobby", "LEAVING THE CREW • 팀에서 나가는 중")
        : NSLOCTEXT("MDLoadingScreen", "ReturningToTitle", "RETURNING TO SURFACE • 수면으로 복귀 중"));

    // SessionName의 기본값 설정(UFUNCTION 측에서 NAME_GameSession을 인식하지 못하여 .cpp 측에서 설정)
    SessionName = SessionName == NAME_None ? NAME_GameSession : SessionName;

    // 이전의 Session이 존재하는지 검사
    if (SessionInterface->GetNamedSession(SessionName) == nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 참여하거나 호스팅 중인 GameSession이 없습니다."), *UE_CALL_LOCATIONINFO);

		DisconnectAfterLoadingIntro();
		return;
    }

    // Online Subsystem의 Session 모듈에 이벤트 등록
    FOnDestroySessionCompleteDelegate OnDestroySessionCompleteDelegate = FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroySessionComplete);
    SessionDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(OnDestroySessionCompleteDelegate);

    // Game Session 제거 요청
    bool bWasRequestSuccess = SessionInterface->DestroySession(SessionName);

    // Game Session 제거 요청 성공 여부 확인
    if (bWasRequestSuccess == false)
    {
        // Online Subsystem의 Session 모듈에 등록된 이벤트 해제
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 Session 제거 요청에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
    }
}

// 로컬 화면과 서버가 소유한 모든 클라이언트 화면에서 동시에 로딩을 시작한다.
// 서버만 켜면 원격 클라이언트는 트래블 순간 예고 없이 얼어붙는다.
void UMD_MultiplayManager::BeginLoadingScreenForTravel(const FText& Subtitle)
{
	if (UMD_LoadingScreenSubsystem* LoadingScreen = GetLoadingScreen())
	{
		LoadingScreen->BeginLoadingScreen(Subtitle);
	}

	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
	{
		return;
	}

	// 리슨 서버 호스트의 PC도 로컬 컨트롤러이므로 이 RPC로 호스트 화면까지 함께 켜진다.
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		if (AMD_PlayerController* PlayerController = Cast<AMD_PlayerController>(Iterator->Get()))
		{
			PlayerController->ClientPrepareLoadingTransition(Subtitle);
		}
	}
}

// 인트로가 화면을 덮은 뒤에 트래블을 실행하기 위한 게이트.
// 바로 부르면 화면이 덜 덮인 상태에서 게임 스레드가 막혀 마지막 프레임이 얼어붙는다.
// 로딩 서브시스템이 없는 실행에서는 즉시 실행해 트래블이 멈추지 않게 한다.
void UMD_MultiplayManager::RunAfterLoadingIntro(TFunction<void()>&& Callback)
{
	if (UMD_LoadingScreenSubsystem* LoadingScreen = GetLoadingScreen())
	{
		LoadingScreen->RunAfterIntro(MoveTemp(Callback));
		return;
	}
	if (Callback)
	{
		Callback();
	}
}

// Create/Start/EndGame 세 경로가 공유하는 트래블 골격.
// 콜백은 인트로가 끝난 뒤 실행되므로 그 사이에 사라졌을 수 있는 this를 약참조로 되짚는다.
void UMD_MultiplayManager::ServerTravelAfterLoadingIntro(const FString& TravelURL, const FString& FailureMessage,
	TFunction<void(UMD_MultiplayManager&)>&& OnFailed, TFunction<void(UMD_MultiplayManager&)>&& OnStarted)
{
	TWeakObjectPtr<UMD_MultiplayManager> WeakThis(this);
	RunAfterLoadingIntro(
		[WeakThis, TravelURL, FailureMessage, OnFailed = MoveTemp(OnFailed), OnStarted = MoveTemp(OnStarted)]()
	{
		UMD_MultiplayManager* Self = WeakThis.Get();
		UWorld* TravelWorld = Self ? Self->GetWorld() : nullptr;
		if (!Self || !IsValid(TravelWorld))
		{
			return;
		}

		if (!TravelWorld->ServerTravel(TravelURL))
		{
			UE_LOG(MarineDemolition, Error, TEXT("%s %s: %s"), *UE_CALL_LOCATIONINFO, *FailureMessage, *TravelURL);
			if (OnFailed) { OnFailed(*Self); }
			return;
		}
		if (OnStarted) { OnStarted(*Self); }
	});
}

// 클라이언트는 Session에서 나가고, 리슨 서버 호스트는 서버를 닫는다.
void UMD_MultiplayManager::DisconnectAfterLoadingIntro()
{
	TWeakObjectPtr<UMD_MultiplayManager> WeakThis(this);
	RunAfterLoadingIntro([WeakThis]()
	{
		UMD_MultiplayManager* Self = WeakThis.Get();
		UWorld* DisconnectWorld = Self ? Self->GetWorld() : nullptr;
		if (!Self || !IsValid(DisconnectWorld) || !GEngine)
		{
			return;
		}

		GEngine->HandleDisconnect(DisconnectWorld, DisconnectWorld->GetNetDriver());
		Self->OnDestroySessionComplete.Broadcast(true);
	});
}

UMD_LoadingScreenSubsystem* UMD_MultiplayManager::GetLoadingScreen() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return IsValid(GameInstance) ? GameInstance->GetSubsystem<UMD_LoadingScreenSubsystem>() : nullptr;
}

bool UMD_MultiplayManager::ShowInviteUI()
{
    // Online Subsystem의 External UI 모듈 유효성 검사
    if (ExternalUIInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 External UI 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // 현재 참여한 세션이 존재하는지 검사
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession == nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 친구를 초대할 Session이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // External UI 모듈을 통하여 친구 초대 UI 표시
    bool bIsUIOpened = ExternalUIInterface->ShowInviteUI(0, NAME_GameSession);

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 친구 초대 UI 출력 %s"), *UE_CALL_LOCATIONINFO, (bIsUIOpened == true ? TEXT("성공") : TEXT("실패")));

    return bIsUIOpened;
}

bool UMD_MultiplayManager::ShowFriendsUI()
{
    // Online Subsystem의 External UI 모듈 유효성 검사
    if (ExternalUIInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 External UI 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // External UI 모듈을 통하여 친구 목록 UI 표시
    bool bIsUIOpened = ExternalUIInterface->ShowFriendsUI(0);

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 친구 목록 UI 출력 %s"), *UE_CALL_LOCATIONINFO, (bIsUIOpened == true ? TEXT("성공") : TEXT("실패")));

    return bIsUIOpened;
}

FString UMD_MultiplayManager::GetCurrentRoomCode() const
{
    if (SessionInterface.IsValid() == false)
    {
        return TEXT("");
    }

    FNamedOnlineSession* NamedOnlineSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (NamedOnlineSession != nullptr)
    {
        FString StringRoomCode;
        bool bHasRoomCode = NamedOnlineSession->SessionSettings.Get(SETTING_SESSIONKEY, StringRoomCode);
        
        if (bHasRoomCode == true)
        {
            return StringRoomCode;
        }
    }
    return TEXT("");
}

FMD_RoomCode UMD_MultiplayManager::ParseRoomCode(FString StringRoomCode)
{
    return FMD_RoomCode::Parse(StringRoomCode);
}

void UMD_MultiplayManager::InitOnlineSessionInterface()
{
    // 프로젝트에서 사용하는 Online Subsystem 얻기 및 유효성 검사
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    if (OnlineSubsystem == nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 사용하는 Online Subsystem의 이름 : %s"), *UE_CALL_LOCATIONINFO, *OnlineSubsystem->GetSubsystemName().ToString());

    // Online Subsystem의 Session 모듈 얻기 및 유효성 검사
    SessionInterface = OnlineSubsystem->GetSessionInterface();
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return;
    }

    // Online Subsystem의 External UI 모듈 얻기 및 유효성 검사
    ExternalUIInterface = OnlineSubsystem->GetExternalUIInterface();
    if (ExternalUIInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s  Online Subsystem의 External UIExternal UI 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return;
    }
}

TSharedPtr<const FUniqueNetId> UMD_MultiplayManager::GetLocalPlayerNetID() const
{
    // GameInstance 얻기 및 유효성 검사
    UGameInstance* GameInstance = GetGameInstance();
    if (IsValid(GameInstance) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s GameInstance를 얻어오지 못하였습니다."), *UE_CALL_LOCATIONINFO);
        return nullptr;
    }

    // 첫 번째 Local Player 객체 얻기 및 유효성 검사
    ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
    if (IsValid(LocalPlayer) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Local Player를 얻어오지 못하였습니다."), *UE_CALL_LOCATIONINFO);
        return nullptr;
    }

    // Local Player의 Preferred Unique Net ID 얻기(해당 LocalPlayer를 온라인 환경에서 식별할 때 엔진이 우선적으로 사용하기를 기대하는 ID)
    FUniqueNetIdRepl PreferredNetID = LocalPlayer->GetPreferredUniqueNetId();

    // UniqueNetID 반환
    return PreferredNetID.IsValid() == true ? PreferredNetID.GetUniqueNetId() : nullptr;
}

void UMD_MultiplayManager::CreateSessionInternal(const FString& ServerName, int32 MaxPlayerCount, const FMD_SessionCreateParams& SessionCreateParams)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online SubSystem의 Session 모듈이 유효하지 않아 Session을 호스팅할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈에서 현재 진행중인 비동기 절차가 존재하는 경우 함수 Session 생성 요청 거부
    if (SessionDelegateHandle.IsValid() == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 진행중인 이전 작업이 완료되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 호스트 서버 이름이 비어 있는 경우 Session 생성 요청 거부
    if (ServerName.IsEmpty() == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 허가되지 않은 서버 이름입니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 접속 가능 최소 플레이어 수가 1보다 작은 경우 Session 생성 요청 거부
    if (MaxPlayerCount <= 0)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 최대 플레이어 수는 1 이상이어야 합니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 이전 Session이 남아 있는 경우 Session 생성 요청 거부
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession != nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session에 접속 중이거나 호스팅 중에는 다른 Session을 호스팅 할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Session 생성 및 Listen Server를 호스팅하는 Local Player의 Net ID 얻기 및 유효성 확인
    TSharedPtr<const FUniqueNetId> LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 생성하려는 Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 생성되는 Session의 고유 코드 발급 및 유효성 확인
    FMD_RoomCode RoomCode = FMD_RoomCode::Random();
    if (RoomCode.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 생성되는 Session을 위한 고유 RoomCode가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 생성되는 Session의 설정 값 결정
    FOnlineSessionSettings SessionSettings;

    IOnlineSubsystem* DefaultOnlineSubsystem = IOnlineSubsystem::Get();
    SessionSettings.bIsLANMatch = DefaultOnlineSubsystem != nullptr && DefaultOnlineSubsystem->GetSubsystemName() == FName("NULL");

    SessionSettings.bIsDedicated = false;		                                            // Marine Demolition 프로젝트 게임은 Listen Server 고정
    SessionSettings.bShouldAdvertise = SessionCreateParams.bPrivateSession == false || SessionCreateParams.bAllowJoinByFollow == true;		// 공개 또는 친구 전용 Steam 로비 생성을 위한 광고 설정
    SessionSettings.bAllowJoinInProgress = true;		                                    // 게임 진행 중 참여 불가 고정
    SessionSettings.bUsesPresence = true;				                                    // Presence 사용 고정
    SessionSettings.bAllowJoinViaPresence = SessionCreateParams.bAllowJoinByFollow == true;		                            // Steam Presence의 친구 따라오기를 통한 참여 허용 설정
    SessionSettings.bAllowJoinViaPresenceFriendsOnly = SessionCreateParams.bPrivateSession == true && SessionCreateParams.bAllowJoinByFollow == true;	// 비공개이면서 따라오기가 가능한 Session의 친구 전용 로비 설정
    SessionSettings.bAllowInvites = SessionCreateParams.bPrivateSession == true || SessionCreateParams.bAllowJoinByFollow == true;		// 비공개 또는 따라오기 가능 Session의 Steam 초대 허용 설정
    SessionSettings.bUseLobbiesIfAvailable = true;		                                    // 로비 사용 고정
    SessionSettings.bUseLobbiesVoiceChatIfAvailable = true;		                            // 음성 채팅 사용 고정
    SessionSettings.NumPublicConnections = MaxPlayerCount;		                                    // Session 최대 인원 수 설정
    SessionSettings.NumPrivateConnections = 0;
    SessionSettings.BuildUniqueId = 1;

    // 서로 다른 프로젝트의 Session과 검색 결과를 구분하기 위한 프로젝트 고유값 설정
    SessionSettings.Set(MD_SessionKey::Key_ProjectID, MD_SessionKey::Value_ProjectID, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 서버 이름 저장
    SessionSettings.Set(MD_SessionKey::Key_ServerName, ServerName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Session 고유 코드 저장
    SessionSettings.Set(SETTING_SESSIONKEY, RoomCode.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Session의 맵 이름을 저장
    FString SessionMapName = FPackageName::GetShortName(SessionCreateParams.SessionMapPackagePath);
    SessionSettings.Set(SETTING_MAPNAME, SessionMapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Online Subsystem의 Session 모듈에 이벤트 등록
    FOnCreateSessionCompleteDelegate OnCreateSessionCompleteDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleCreateSessionComplete, SessionCreateParams.SessionMapPackagePath);
    SessionDelegateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate);

    // Game Session 생성 요청
    bool bWasRequestSuccess = SessionInterface->CreateSession(*LocalPlayerNetID, NAME_GameSession, SessionSettings);

    // Game Session 생성 요청 성공 여부 확인
    if (bWasRequestSuccess == false)
    {
        // Online Subsystem의 Session 모듈에 등록된 이벤트 해제
        SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 Session 생성 요청에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::JoinSessionInternal(const FOnlineSessionSearchResult& SessionSearchResult)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online SubSystem의 Session 모듈이 유효하지 않아 Session에 참가할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈에서 현재 진행중인 비동기 절차가 존재하는 경우 함수 Session 참여 요청 거부
    if (SessionDelegateHandle.IsValid() == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 진행중인 이전 작업이 완료되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 이전 Session이 남아 있는 경우 Session 참여 요청 거부
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession != nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session에 접속 중이거나 호스팅 중에는 다른 Session에 참여 할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 생성 및 Listen Server를 호스팅하는 Local Player의 Net ID 얻기 및 유효성 확인
    TSharedPtr<const FUniqueNetId> LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session에 참여하려는 Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Online Subsystem의 Session 모듈에 이벤트 등록
    FOnJoinSessionCompleteDelegate OnJoinSessionCompleteDelegate = FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleJoinSessionComplete);
    SessionDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(OnJoinSessionCompleteDelegate);

    // Game Session 참여 요청
    bool bWasRequestSuccess = SessionInterface->JoinSession(*LocalPlayerNetID, NAME_GameSession, SessionSearchResult);

    // Game Session 참여 요청 성공 여부 확인
    if (bWasRequestSuccess == false)
    {
        // Online Subsystem의 Session 모듈에 등록된 이벤트 해제
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 Session 참여 요청에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::StartSessionInternal(const FString& MapPackagePath, const FString& TravelURL)
{
    // 잘못된 에셋이나 일반 에셋 패키지를 대상으로 한 게임 시작 방지
    if (IsValidMapPackagePath(MapPackagePath) == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 시작에 사용할 맵 패키지 경로가 유효하지 않습니다: %s"), *UE_CALL_LOCATIONINFO, *MapPackagePath);

        CancelPendingGameDataOperation();

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않아 게임을 시작할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // Session 모듈에서 현재 진행중인 비동기 절차가 존재하는 경우 함수 Session 시작 요청 거부
    if (SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 진행 중인 이전 작업이 완료되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 현재 호스팅 중인 GameSession 존재 및 재시작 가능 상태 확인
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession == nullptr || (ExistingSession->SessionState != EOnlineSessionState::Pending && ExistingSession->SessionState != EOnlineSessionState::Ended))
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 시작 대기 또는 종료 상태의 호스팅 중인 GameSession이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 기존 광고 설정을 유지하면서 게임 진행 중 추가 참가 설정만 닫기 위한 복사
    FOnlineSessionSettings UpdatedSessionSettings = ExistingSession->SessionSettings;
    UpdatedSessionSettings.bAllowJoinInProgress = false;

    // 세션 검색 결과의 맵 정보가 실제 게임 이동 대상과 일치하도록 갱신하는 처리
    FString SessionMapName = FPackageName::GetShortName(MapPackagePath);
    UpdatedSessionSettings.Set(SETTING_MAPNAME, SessionMapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 추가 참가 차단 완료 후 실제 Session 시작을 이어가기 위한 이벤트 등록
    FOnUpdateSessionCompleteDelegate OnUpdateSessionCompleteDelegate = FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleStartGameJoinabilityUpdateComplete, TravelURL);
    SessionDelegateHandle = SessionInterface->AddOnUpdateSessionCompleteDelegate_Handle(OnUpdateSessionCompleteDelegate);

    // 게임 시작 전에 Steam 로비의 추가 참가를 차단하기 위한 갱신 요청
    bool bWasRequestSuccessful = SessionInterface->UpdateSession(NAME_GameSession, UpdatedSessionSettings, true);

    // Session 설정 갱신 요청이 정상 처리되었는지 확인
    if (bWasRequestSuccessful == false)
    {
        // Online Subsystem의 Session 설정 갱신 이벤트 연결 해제
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 시작 전 추가 참가 차단 요청을 접수하지 못했습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        RecoverStartGameJoinability();
    }
}

void UMD_MultiplayManager::RequestStartSession(const FString& TravelURL)
{
    // 추가 참가 차단 이후 실제 Session 시작 요청을 수행할 모듈 검증
    if (SessionInterface.IsValid() == false || SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 추가 참가 차단 이후 GameSession을 시작할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        RecoverStartGameJoinability();
        return;
    }

    // 실제 Session 시작 결과 수신을 위한 이벤트 등록
    FOnStartSessionCompleteDelegate OnStartSessionCompleteDelegate = FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleStartSessionComplete, TravelURL);
    SessionDelegateHandle = SessionInterface->AddOnStartSessionCompleteDelegate_Handle(OnStartSessionCompleteDelegate);

    // 추가 참가가 차단된 Session의 시작 요청
    bool bWasRequestSuccessful = SessionInterface->StartSession(NAME_GameSession);

    // Session 시작 요청이 정상 처리되었는지 확인
    if (bWasRequestSuccessful == false)
    {
        // Online Subsystem의 Session 시작 이벤트 연결 해제
        SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈에서 Session 시작 요청에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        RecoverStartGameJoinability();
    }
}

void UMD_MultiplayManager::RecoverStartGameJoinability()
{
    // 복구할 Session 모듈과 비동기 작업 상태 검증
    if (SessionInterface.IsValid() == false || SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 게임 시작 실패 이후 로비의 추가 참가 상태를 복구할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 복구 대상 GameSession 검증
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession == nullptr)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 게임 시작 실패 이후 복구할 GameSession이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 기존 광고 설정을 유지하면서 로비의 추가 참가만 다시 허용하기 위한 복사
    FOnlineSessionSettings UpdatedSessionSettings = ExistingSession->SessionSettings;
    UpdatedSessionSettings.bAllowJoinInProgress = true;

    // 게임 시작 실패 후 세션 검색 결과를 현재 로비 맵으로 되돌리기 위한 월드 검증
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 게임 시작 실패 이후 현재 로비 맵 정보를 확인할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
        return;
    }

    // 실패한 게임 맵 대신 실제로 유지 중인 로비 맵을 다시 광고하는 처리
    FString CurrentSessionMapName = FPackageName::GetShortName(World->GetPackage()->GetName());
    UpdatedSessionSettings.Set(SETTING_MAPNAME, CurrentSessionMapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 로비 참가 상태 복구 결과를 정리하고 최초 게임 시작 실패를 전달하기 위한 이벤트 등록
    FOnUpdateSessionCompleteDelegate OnUpdateSessionCompleteDelegate = FOnUpdateSessionCompleteDelegate::CreateWeakLambda(this, [this](FName SessionName, bool bWasSuccessful)
        {
            // 로비 참가 상태 복구 절차에서 연결한 이벤트 및 핸들 초기화
            if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
            {
                SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
            }
            SessionDelegateHandle.Reset();

            // 복구 실패 시 후속 참가 문제 확인을 위한 오류 기록
            if (SessionName != NAME_GameSession || bWasSuccessful == false)
            {
                UE_LOG(MarineDemolition, Error, TEXT("%s 게임 시작 실패 이후 로비의 추가 참가 상태 복구에 실패했습니다."), *UE_CALL_LOCATIONINFO);
            }

            OnStartGameComplete.Broadcast(false);
            return;
        });
    SessionDelegateHandle = SessionInterface->AddOnUpdateSessionCompleteDelegate_Handle(OnUpdateSessionCompleteDelegate);

    // 대기 로비의 추가 참가 허용 상태 복구 요청
    bool bWasRequestSuccessful = SessionInterface->UpdateSession(NAME_GameSession, UpdatedSessionSettings, true);
    if (bWasRequestSuccessful == false)
    {
        // 접수 실패 이후 다른 Session 작업을 허용하기 위한 이벤트 정리
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Error, TEXT("%s 게임 시작 실패 이후 로비 참가 상태 복구 요청을 접수하지 못했습니다."), *UE_CALL_LOCATIONINFO);

        OnStartGameComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, const FString SessionMapPackagePath)
{
    // Session 생성 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // GameSession 생성 실패 시 함수 조기 종료
    if (SessionName != NAME_GameSession || bWasSuccessful == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 생성에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Map 이동 전 World 참조
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Listen Server를 시작할 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // 생성에 성공한 NamedSession을 조용히 제거한 뒤 Create Session 실패 이벤트를 전달합니다.
        RollbackSession(SessionName, EMD_SessionRollbackOperation::CreateSession);
        return;
    }

    // PIE 또는 실행 옵션으로 이미 열린 Listen NetDriver에 두 번째 Listen 소켓을 생성하지 않기 위한 상태 확인
    bool bIsAlreadyListenServer = World->GetNetMode() == ENetMode::NM_ListenServer && World->GetNetDriver() != nullptr;

    // 기존 Listen 연결은 심리스 트래블로 유지하고 최초 Listen 시작만 하드 트래블로 처리하기 위한 설정
    AGameModeBase* GameMode = World->GetAuthGameMode();
    if (IsValid(GameMode) == true)
    {
        GameMode->bUseSeamlessTravel = bIsAlreadyListenServer;
    }

    // 아직 Listen Server가 아닌 실행에서만 새 NetDriver 생성을 요청하는 Travel URL 구성
    FString TravelURL = bIsAlreadyListenServer == true ? SessionMapPackagePath : FString::Printf(TEXT("%s?listen"), *SessionMapPackagePath);

	ServerTravelAfterLoadingIntro(TravelURL, TEXT("Listen Server 맵 이동을 시작하지 못했습니다"),
		[SessionName](UMD_MultiplayManager& Self) { Self.RollbackSession(SessionName, EMD_SessionRollbackOperation::CreateSession); },
		[](UMD_MultiplayManager& Self) { Self.OnCreateSessionComplete.Broadcast(true); });
}

void UMD_MultiplayManager::HandleStartGameJoinabilityUpdateComplete(FName SessionName, bool bWasSuccessful, const FString TravelURL)
{
    // 추가 참가 차단 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // 추가 참가 차단 실패 이후 게임 데이터 작업 취소와 로비 상태 복구
    if (SessionName != NAME_GameSession || bWasSuccessful == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 시작 전 로비의 추가 참가 차단에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        RecoverStartGameJoinability();
        return;
    }

    // 추가 참가 차단 완료 이후 실제 Session 시작 요청
    RequestStartSession(TravelURL);
}

void UMD_MultiplayManager::HandleStartSessionComplete(FName SessionName, bool bWasSuccessful, const FString TravelURL)
{
    // Start Session 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // Start Session 실패 시 함수 조기 종료
    if (SessionName != NAME_GameSession || bWasSuccessful == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 시작에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        RecoverStartGameJoinability();
        return;
    }

    // 비동기 처리 중 World가 교체되거나 호스트 권한이 사라졌는지 맵 이동 직전에 다시 확인합니다.
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        CancelPendingGameDataOperation();

        RollbackSession(SessionName, EMD_SessionRollbackOperation::StartGame);
        return;
    }

	ServerTravelAfterLoadingIntro(TravelURL, TEXT("GameSession 시작 후 지정 맵 이동을 시작하지 못했습니다"),
		[SessionName](UMD_MultiplayManager& Self)
		{
			Self.CancelPendingGameDataOperation();
			Self.RollbackSession(SessionName, EMD_SessionRollbackOperation::StartGame);
		},
		[](UMD_MultiplayManager& Self) { Self.OnStartGameComplete.Broadcast(true); });
}

void UMD_MultiplayManager::HandleEndSessionComplete(FName SessionName, bool bWasSuccessful, const FString MapPackagePath)
{
    // End Session 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnEndSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // 다른 이름의 Session 완료 또는 Online Subsystem 실패를 성공으로 전달하지 않기 위한 결과 검증
    if (SessionName != NAME_GameSession || bWasSuccessful == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s GameSession 종료에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 종료된 GameSession의 광고 설정 복사를 위한 대상 검증
    FNamedOnlineSession* ExistingSession = SessionInterface.IsValid() == true ? SessionInterface->GetNamedSession(NAME_GameSession) : nullptr;
    if (ExistingSession == nullptr)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 게임 종료 후 추가 참가를 허용할 GameSession이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 기존 광고 설정을 유지하면서 로비의 추가 참가만 다시 허용하기 위한 복사
    FOnlineSessionSettings UpdatedSessionSettings = ExistingSession->SessionSettings;
    UpdatedSessionSettings.bAllowJoinInProgress = true;

    // 세션 검색 결과의 맵 정보가 실제 복귀 이동 대상과 일치하도록 갱신하는 처리
    FString SessionMapName = FPackageName::GetShortName(MapPackagePath);
    UpdatedSessionSettings.Set(SETTING_MAPNAME, SessionMapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 추가 참가 허용 완료 후 로비 맵 이동을 이어가기 위한 이벤트 등록
    FOnUpdateSessionCompleteDelegate OnUpdateSessionCompleteDelegate = FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleEndGameJoinabilityUpdateComplete, MapPackagePath);
    SessionDelegateHandle = SessionInterface->AddOnUpdateSessionCompleteDelegate_Handle(OnUpdateSessionCompleteDelegate);

    // 게임 종료 후 Steam 로비의 추가 참가를 다시 허용하기 위한 갱신 요청
    bool bWasRequestSuccessful = SessionInterface->UpdateSession(NAME_GameSession, UpdatedSessionSettings, true);
    if (bWasRequestSuccessful == false)
    {
        // 요청 접수 실패 이후 Session 진행 상태 복구를 허용하기 위한 이벤트 정리
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 종료 후 추가 참가 허용 요청을 접수하지 못했습니다."), *UE_CALL_LOCATIONINFO);

        RecoverEndGameSession(SessionName);
    }
}

void UMD_MultiplayManager::HandleEndGameJoinabilityUpdateComplete(FName SessionName, bool bWasSuccessful, const FString MapPackagePath)
{
    // 추가 참가 허용 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // 추가 참가 허용 실패 이후 기존 진행 상태 복구
    if (SessionName != NAME_GameSession || bWasSuccessful == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 게임 종료 후 로비의 추가 참가 허용에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        RecoverEndGameSession(NAME_GameSession);
        return;
    }

    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s GameSession 종료 후 맵 이동을 수행할 서버 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        RecoverEndGameSession(SessionName);
        return;
    }

	ServerTravelAfterLoadingIntro(MapPackagePath, TEXT("GameSession 종료 후 지정 맵 이동을 시작하지 못했습니다"),
		[SessionName](UMD_MultiplayManager& Self) { Self.RecoverEndGameSession(SessionName); },
		[](UMD_MultiplayManager& Self) { Self.OnEndGameComplete.Broadcast(true); });
}

void UMD_MultiplayManager::RecoverEndGameSession(FName SessionName)
{
    // Session 모듈이 유효하지 않거나 다른 비동기 절차를 진행중인 경우 복구 실패 처리
    if (SessionInterface.IsValid() == false || SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 맵 이동 실패 이후 GameSession을 복구할 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 복구할 GameSession과 현재 게임 맵 확인
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(SessionName);
    UWorld* World = GetWorld();
    if (ExistingSession == nullptr || IsValid(World) == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 맵 이동 실패 이후 복구할 Session 또는 현재 게임 맵이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 복귀 이동 실패 후 현재 게임 맵과 진행 중 참가 제한을 다시 광고하기 위한 설정
    FOnlineSessionSettings UpdatedSessionSettings = ExistingSession->SessionSettings;
    UpdatedSessionSettings.bAllowJoinInProgress = false;
    FString CurrentSessionMapName = FPackageName::GetShortName(World->GetPackage()->GetName());
    UpdatedSessionSettings.Set(SETTING_MAPNAME, CurrentSessionMapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 현재 게임 맵 정보 복구 완료 후 Ended Session 재시작을 이어가기 위한 이벤트 등록
    FOnUpdateSessionCompleteDelegate OnUpdateSessionCompleteDelegate = FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleRecoverEndGameSessionSettingsComplete, SessionName);
    SessionDelegateHandle = SessionInterface->AddOnUpdateSessionCompleteDelegate_Handle(OnUpdateSessionCompleteDelegate);

    // 현재 게임 맵 정보와 진행 중 참가 제한 복구 요청
    bool bWasRequestSuccessful = SessionInterface->UpdateSession(SessionName, UpdatedSessionSettings, true);
    if (bWasRequestSuccessful == false)
    {
        // 접수 실패 이후 다른 Session 작업을 허용하기 위한 이벤트 정리
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Warning, TEXT("%s 맵 이동 실패 이후 Session 맵 정보 복구 요청을 접수하지 못했습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::HandleRecoverEndGameSessionSettingsComplete(FName UpdatedSessionName, bool bWasSuccessful, FName SessionName)
{
    // 게임 종료 복구 설정에 사용한 이벤트 및 핸들 정리
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // 현재 게임 맵 정보 복구 실패 시 잘못된 세션 광고 상태에서 재시작하지 않기 위한 차단
    if (UpdatedSessionName != SessionName || bWasSuccessful == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 맵 이동 실패 이후 Session 맵 정보 복구에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // 현재 맵 정보가 복구된 Ended Session의 진행 상태 복구 요청
    RequestRecoverEndGameSessionStart(SessionName);
}

void UMD_MultiplayManager::RequestRecoverEndGameSessionStart(FName SessionName)
{
    // Session 재시작 요청을 수행할 모듈과 비동기 작업 상태 검증
    if (SessionInterface.IsValid() == false || SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 맵 이동 실패 이후 GameSession 재시작을 요청할 수 없습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
        return;
    }

    // Session 진행 상태 복구 결과를 정리하기 위한 이벤트 등록
    FOnStartSessionCompleteDelegate OnEndGameRecoveryCompleteDelegate = FOnStartSessionCompleteDelegate::CreateWeakLambda(this, [this, SessionName](FName StartedSessionName, bool bWasSuccessful)
        {
            // Session 진행 상태 복구에 사용한 이벤트 및 핸들 정리
            if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
            {
                SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(SessionDelegateHandle);
            }
            SessionDelegateHandle.Reset();

            // 실제 Session 재시작 결과 기록
            if (StartedSessionName != SessionName || bWasSuccessful == false)
            {
                UE_LOG(MarineDemolition, Error, TEXT("%s 맵 이동 실패 이후 Session 진행 상태 복구에 실패했습니다."), *UE_CALL_LOCATIONINFO);
            }
            else
            {
                UE_LOG(MarineDemolition, Log, TEXT("%s 맵 이동 실패 이후 Session을 진행 상태로 복구했습니다."), *UE_CALL_LOCATIONINFO);
            }

            OnEndGameComplete.Broadcast(false);
            return;
        });
    SessionDelegateHandle = SessionInterface->AddOnStartSessionCompleteDelegate_Handle(OnEndGameRecoveryCompleteDelegate);

    // 현재 게임 맵 정보가 복구된 Session의 시작 요청
    bool bWasRequestSuccessful = SessionInterface->StartSession(SessionName);
    if (bWasRequestSuccessful == false)
    {
        // 접수 실패 이후 다른 Session 작업을 허용하기 위한 이벤트 정리
        SessionInterface->ClearOnStartSessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        UE_LOG(MarineDemolition, Warning, TEXT("%s 맵 이동 실패 이후 GameSession 재시작 요청을 접수하지 못했습니다."), *UE_CALL_LOCATIONINFO);

        OnEndGameComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::HandleFindSessionsComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch)
{
    // Session 검색 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // 검색 결과를 저장하는 배열 생성
    TArray<FBlueprintSessionResult> SearchResults;

    // 검색 실패 시 함수 조기 종료
    if (SessionSearch.IsValid() == false || bWasSuccessful == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 검색에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(SearchResults, false);
        return;
    }

    // 검색된 Session 목록 필터링
    for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
    {
        // 인덱스에 해당하는 OnlineSessionSearchResult 객체 참조 및 유효성 확인
        const FOnlineSessionSearchResult& SessionSearchResult = SessionSearch->SearchResults[Index];
        if (SessionSearchResult.IsValid() == false)
        {
            continue;
        }

        // 프로젝트에서 설정한 Session Settings의 Custom 속성 값 확인

        // ProjectID값 확인
        FString SessionProjectID;
        bool bHasProjectID = SessionSearchResult.Session.SessionSettings.Get<FString>(MD_SessionKey::Key_ProjectID, SessionProjectID);

        // Session 설정에 ProjectID 값이 존재하는지 및 현재 프로젝트의 설정과 일치하는지 검사
        if (bHasProjectID == false || SessionProjectID != MD_SessionKey::Value_ProjectID)
        {
            continue;
        }

        // 변환된 Session 정보를 목록에 추가
        FBlueprintSessionResult& BlueprintSearchResult = SearchResults.AddDefaulted_GetRef();
        BlueprintSearchResult.OnlineResult = SessionSearchResult;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 현재 %d개의 프로젝트 Session이 검색되었습니다."), *UE_CALL_LOCATIONINFO, SearchResults.Num());

    // Find Session 성공 이벤트 실행
    OnFindSessionsComplete.Broadcast(SearchResults, true);
}

void UMD_MultiplayManager::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    // Session 참여 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // Join 실패 시 함수 조기 종료
    if (SessionName != NAME_GameSession || Result != EOnJoinSessionCompleteResult::Success)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 참여에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 참여하려는 서버 접속 주소 얻기
    FString ConnectInfo;
    bool bIsResolveSuccess = SessionInterface->GetResolvedConnectString(SessionName, ConnectInfo);

    // 서버 접속 주소 획득 실패 시 함수 조기 종료
    if (bIsResolveSuccess == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 참여하려는 Session의 접속 주소 획득에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // 참여에 성공한 NamedSession을 조용히 제거한 뒤 Join Session 실패 이벤트를 전달합니다.
        RollbackSession(SessionName, EMD_SessionRollbackOperation::JoinSession);
        return;
    }

    // 현재 World(Map) 얻기
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // 참여에 성공한 NamedSession을 조용히 제거한 뒤 Join Session 실패 이벤트를 전달합니다.
        RollbackSession(SessionName, EMD_SessionRollbackOperation::JoinSession);
        return;
    }

    // 첫번째 Player Controller(주로 Local Player Controller) 얻기 및 유효성 검사
    APlayerController* PlayerController = World->GetFirstPlayerController();
    if (IsValid(PlayerController) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 참여하려는 PlayerController가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // 참여에 성공한 NamedSession을 조용히 제거한 뒤 Join Session 실패 이벤트를 전달합니다.
        RollbackSession(SessionName, EMD_SessionRollbackOperation::JoinSession);
        return;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 플레이어를 Session의 Map으로 이동합니다.(%s)"), *UE_CALL_LOCATIONINFO, *ConnectInfo);

	TWeakObjectPtr<UMD_MultiplayManager> WeakThis(this);
	TWeakObjectPtr<APlayerController> WeakPlayerController(PlayerController);
	RunAfterLoadingIntro([WeakThis, WeakPlayerController, ConnectInfo]()
	{
		UMD_MultiplayManager* Self = WeakThis.Get();
		APlayerController* TravelController = WeakPlayerController.Get();
		if (!Self || !IsValid(TravelController))
		{
			return;
		}

		TravelController->ClientTravel(ConnectInfo, ETravelType::TRAVEL_Absolute);
		Self->OnJoinSessionComplete.Broadcast(true);
	});
}

void UMD_MultiplayManager::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
    // Session 제거 절차에서 연결한 이벤트 및 핸들 초기화
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // GameSession 제거 실패 시 함수 조기 종료
    if (bWasSuccessful == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 GameSession 제거에 실패했습니다."), *UE_CALL_LOCATIONINFO);

        // Session 제거 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    // Map 이동 전 World 참조
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Session 제거 후 네트워크 연결을 종료할 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Session 제거 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    // 엔진의 연결 종료 처리를 사용하여 NetDriver를 닫고 프로젝트 기본 맵으로 복귀할 수 있는지 확인합니다.
    UEngine* Engine = GEngine;
    if (Engine == nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Session 제거 후 네트워크 연결을 종료할 Engine이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Session 제거 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

	DisconnectAfterLoadingIntro();
}

void UMD_MultiplayManager::CancelPendingGameDataOperation()
{
    // 세이브 데이터 관리자 참조
    UGameInstance* GameInstance = GetGameInstance();
    UMD_SaveSlotManager* SaveSlotManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_SaveSlotManager>() : nullptr;

    // 현재 진행 데이터는 유지하고 실패한 시작 절차의 보류 작업만 취소하는 처리
    if (IsValid(SaveSlotManager) == true)
    {
        SaveSlotManager->CancelPendingGameDataOperation();
    }
}

void UMD_MultiplayManager::RollbackSession(FName SessionName, EMD_SessionRollbackOperation RollbackOperation)
{
    // Online Subsystem의 Session 모듈이 사라진 경우 제거를 시도하지 않고 원래 작업의 실패를 전달합니다.
    if (SessionInterface.IsValid() == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 내부 Session 롤백을 위한 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        BroadcastCompleteSessionRollback(RollbackOperation);
        return;
    }

    // 다른 비동기 Session 작업이 진행 중이면 공용 핸들을 덮어쓰지 않고 원래 작업의 실패를 전달합니다.
    if (SessionDelegateHandle.IsValid() == true)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 Session 작업이 진행 중이어서 내부 Session 롤백을 시작할 수 없습니다."), *UE_CALL_LOCATIONINFO);
        BroadcastCompleteSessionRollback(RollbackOperation);
        return;
    }

    // 제거할 NamedSession이 이미 없다면 롤백이 완료된 것으로 간주하고 원래 작업의 실패를 전달합니다.
    FNamedOnlineSession* const ExistingSession = SessionInterface->GetNamedSession(SessionName);
    if (ExistingSession == nullptr)
    {
        BroadcastCompleteSessionRollback(RollbackOperation);
        return;
    }

    // 롤백 완료 시 외부 제거 이벤트 대신 원래 생성 또는 참여 실패만 전달하도록 전용 콜백을 등록합니다.
    const FOnDestroySessionCompleteDelegate OnRollbackSessionCompleteDelegate = FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleRollbackSessionComplete, RollbackOperation);
    SessionDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(OnRollbackSessionCompleteDelegate);

    // 생성 또는 참여 과정에서 남은 NamedSession을 비동기로 제거합니다.
    const bool bWasRequestSuccess = SessionInterface->DestroySession(SessionName);
    if (bWasRequestSuccess == false)
    {
        // 요청 시작 실패 시 롤백 콜백과 공용 핸들을 즉시 정리하여 이후 Session 작업을 허용합니다.
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(SessionDelegateHandle);
        SessionDelegateHandle.Reset();

        // NamedSession이 남아 있을 수 있음을 로그로 알리고 원래 작업의 실패를 전달합니다.
        UE_LOG(MarineDemolition, Error, TEXT("%s 내부 Session 롤백 제거 요청을 시작하지 못했습니다: %s"), *UE_CALL_LOCATIONINFO, *SessionName.ToString());
        BroadcastCompleteSessionRollback(RollbackOperation);
    }
}

void UMD_MultiplayManager::HandleRollbackSessionComplete(FName SessionName, bool bWasSuccessful, EMD_SessionRollbackOperation RollbackOperation)
{
    // 내부 롤백에 사용한 제거 이벤트와 공용 핸들을 먼저 정리하여 실패 이벤트 수신자의 재시도를 허용합니다.
    if (SessionInterface.IsValid() == true && SessionDelegateHandle.IsValid() == true)
    {
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(SessionDelegateHandle);
    }
    SessionDelegateHandle.Reset();

    // 롤백 제거 실패 시 NamedSession이 남아 후속 작업을 막을 수 있으므로 오류를 기록합니다.
    if (bWasSuccessful == false)
    {
        UE_LOG(MarineDemolition, Error, TEXT("%s 내부 Session 롤백에 실패했습니다: %s"), *UE_CALL_LOCATIONINFO, *SessionName.ToString());
    }

    // 롤백 성공 여부와 관계없이 최초 생성 또는 참여 작업의 최종 결과는 실패로 전달합니다.
    BroadcastCompleteSessionRollback(RollbackOperation);
}

void UMD_MultiplayManager::BroadcastCompleteSessionRollback(EMD_SessionRollbackOperation RollbackOperation)
{
    // 생성 실패에서 시작된 롤백이면 외부 제거 이벤트 없이 생성 실패만 전달합니다.
    if (RollbackOperation == EMD_SessionRollbackOperation::CreateSession)
    {
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 참여 실패에서 시작된 롤백이면 외부 제거 이벤트 없이 참여 실패만 전달합니다.
    if (RollbackOperation == EMD_SessionRollbackOperation::JoinSession)
    {
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 게임 시작 실패에서 시작된 롤백이면 외부 제거 이벤트 없이 게임 시작 실패만 전달합니다.
    if (RollbackOperation == EMD_SessionRollbackOperation::StartGame)
    {
        OnStartGameComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult)
{
    // 초대된 Session의 유효성 검사
    if (InviteResult.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 초대받은 Session이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Invite Session Accepted 실패 이벤트 실행
        OnSessionInviteAcceptedComplete.Broadcast(false);
        return;
    }

    // 초대 수락한 Session에 대하여 현재 프로젝트의 Session 여부 확인
    FString ProjectID;
    bool bHasProjectID = InviteResult.Session.SessionSettings.Get(MD_SessionKey::Key_ProjectID, ProjectID);
    if (bHasProjectID == false || ProjectID != MD_SessionKey::Value_ProjectID)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 초대받은 Session의 프로젝트가 현재 프로젝트와 다릅니다."), *UE_CALL_LOCATIONINFO);

        // Invite Session Accepted 실패 이벤트 실행
        OnSessionInviteAcceptedComplete.Broadcast(false);
        return;
    }

    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Invite Session Accepted 실패 이벤트 실행
        OnSessionInviteAcceptedComplete.Broadcast(false);
        return;
    }

    // Session 초대 수락 처리 실패 시 함수 조기종료
    if (bWasSuccessful == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 초대에 대한 수락 처리에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Invite Session Accepted 실패 이벤트 실행
        OnSessionInviteAcceptedComplete.Broadcast(false);
        return;
    }

    // 초대를 수락한 플레이어의 유효성 검사
    if (ControllerId < 0 || UserId.IsValid() == false || UserId->IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session에 초대된 플레이어의 정보가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Invite Session Accepted 실패 이벤트 실행
        OnSessionInviteAcceptedComplete.Broadcast(false);
        return;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s Session 초대에 대하여 수락 처리에 성공하였습니다."), *UE_CALL_LOCATIONINFO);

    // Invite Session Accepted 성공 이벤트 실행
    OnSessionInviteAcceptedComplete.Broadcast(true);

    // 초대받은 Session 참여 절차 실행
    FBlueprintSessionResult InviteSessionResult;
    InviteSessionResult.OnlineResult = InviteResult;
    JoinSession(InviteSessionResult);
}

const bool UMD_MultiplayManager::IsValidMapPackagePath(const FString& MapPackagePath) const
{
    // ServerTravel 가능한 Map 패키지 경로 확인
    if (FPackageName::IsValidLongPackageName(MapPackagePath) == false)
    {
        return false;
    }

    // 패키지의 실제 파일 경로 확인
    // 오타 또는 패키지에 포함되지 않은 맵을 사용한 Session 생성 방지
    FString SessionMapFilename;
    if (FPackageName::DoesPackageExist(MapPackagePath, &SessionMapFilename) == false)
    {
        return false;
    }

    // 맵 패키지 확장자를 명시적으로 확인(.umap)
    // 일반 에셋 패키지를 통한 ServerTravel 사용 방지
    const FString PackageExtension = FString::Printf(TEXT(".%s"), *FPaths::GetExtension(SessionMapFilename));
    if (PackageExtension.Equals(FPackageName::GetMapPackageExtension(), ESearchCase::IgnoreCase) == false)
    {
        return false;
    }

    return true;
}
