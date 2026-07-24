// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/MDMultiplayManager.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"
#include "MDSessionSettingKey.h"
#include "MDLogUtility.h"

void UMDMultiplayManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

    InitOnlineSessionInterface();
}

void UMDMultiplayManager::Deinitialize()
{
    // Online Session Interface의 유효성 검사
    if (SessionInterface.IsValid() == true)
    {
        // Session 생성 완료에 대한 이벤트 연결 해제
        if (CreateSessionCompleteDelegateHandle.IsValid() == true)
        {
            SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
        }

        // Session 검색 완료에 대한 이벤트 연결 해제
        if (FindSessionsCompleteDelegateHandle.IsValid() == true)
        {
            SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
        }

        // Session 참여 완료에 대한 이벤트 연결 해제
        if (JoinSessionCompleteDelegateHandle.IsValid() == true)
        {
            SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
        }

        // Session 제거 완료에 대한 이벤트 연결 해제
        if (DestroySessionCompleteDelegateHandle.IsValid() == true)
        {
            SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
        }
    }

	Super::Deinitialize();
}

void UMDMultiplayManager::CreateSession(const FString& ServerName, int32 MaxPlayerCount)
{
    // Online Session Interface 유효성 및 Session 최대 접속 인원 수 유효성 확인
    if (SessionInterface.IsValid() == false || MaxPlayerCount <= 0)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Create Session을 위한 Parameter가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Create Session에 사용되는 Parameter 저장
    PendingCreateSessionParams = MakeShared<FMDCreateSessionParams>();
    PendingCreateSessionParams->ServerName = ServerName;
    PendingCreateSessionParams->MaxPlayerCount = MaxPlayerCount;

    // 이전 Session 존재 확인
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession != nullptr)
    {
        // 기존 Session 삭제
        DestroySession();
        return;
    }

    // 실제 Session 생성
    CreateSessionInternal();
}

void UMDMultiplayManager::FindSessions(int32 MaxSearchResultCount)
{
    // Find Session 실패 시 반환될 검색 결과
    const TArray<FMDSessionInfo> EmptyFindResults;

    // Online Session Interface 유효성 및 최대 검색 결과 수 유효성 검사
    if (SessionInterface.IsValid() == false || MaxSearchResultCount <= 0)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Find Sessions를 위한 Parameter가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptyFindResults, false);
        return;
    }

    // Local Player의 Net ID 얻기 및 유효성 검사
    TSharedPtr<const FUniqueNetId> LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Local Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptyFindResults, false);
        return;
    }

    // Session 검색 결과를 저장할 객체 생성
    SessionSearch = MakeShared<FOnlineSessionSearch>();

    // 최대 검색 결과 저장 개수 설정
    SessionSearch->MaxSearchResults = MaxSearchResultCount;

    // Online Subsystem 얻기
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

    // Online Subsystem이 NULL Subsystem이면 LAN 검색 사용
    SessionSearch->bIsLanQuery = OnlineSubsystem != nullptr && OnlineSubsystem->GetSubsystemName() == FName(TEXT("NULL"));

    //// Presence 세션만 검색하도록 설정합니다.
    //SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);

    // 로비 Session 검색 설정
    SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

    // Online Session Interface의 Session 검색 완료 이벤트 등록
    FOnFindSessionsCompleteDelegate FindSessionsDelegate = FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::HandleFindSessionsComplete);
    FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsDelegate);

    // Online Session Interface를 통하여 Session 검색 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->FindSessions(*LocalPlayerNetID, SessionSearch.ToSharedRef());

    // Session 검색 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Online Session Interface의 Session 검색 완료 이벤트 해제
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
        FindSessionsCompleteDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s FindSessions 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptyFindResults, false);
    }
}

void UMDMultiplayManager::JoinSession(int32 SessionIndex)
{
    // Online Session Interface 유효성 및 이전 Session 검색 결과 유효성 검사
    if (SessionInterface.IsValid() == false || SessionSearch.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Join Session를 위한 Parameter가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 선택한 Session 인덱스가 이전 검색 결과 목록에 대하여 유효한지 검사
    if (SessionSearch->SearchResults.IsValidIndex(SessionIndex) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 참여하려는 Session의 Index가 범위를 벗어났습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Local Player의 Net ID 얻기 및 유효성 검사
    TSharedPtr<const FUniqueNetId> LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Local Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 인덱스에 따른 FOnlieSessionSearchResult 객체 참조
    FOnlineSessionSearchResult& SelectedResult = SessionSearch->SearchResults[SessionIndex];

    // Online Session Interface의 Session 참여 완료 이벤트 등록
    FOnJoinSessionCompleteDelegate JoinSessionDelegate = FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleJoinSessionComplete);
    JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionDelegate);

    // Online Session Interface를 통하여 Session 참여 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->JoinSession(*LocalPlayerNetID, NAME_GameSession, SelectedResult);

    // Session 참여 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Online Session Interface의 Session 참여 완료 이벤트 해제
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
        JoinSessionCompleteDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s JoinSession 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
    }
}

void UMDMultiplayManager::DestroySession()
{
    // Online Session Interface 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Destroy Session를 위한 Parameter가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    // 이전의 Session이 존재하는지 검사
    if (SessionInterface->GetNamedSession(NAME_GameSession) == nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Log, TEXT("%s 제거할 Session이 존재하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 성공 이벤트 실행 (삭제할 Session이 존재하지 않음)
        OnDestroySessionComplete.Broadcast(true);
        return;
    }

    // Online Session Interface의 Session 제거 완료 이벤트 등록
    FOnDestroySessionCompleteDelegate DestroySessionDelegate = FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroySessionComplete);
    DestroySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionDelegate);

    // Online Session Interface를 통하여 Session 제거 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->DestroySession(NAME_GameSession);

    // Session 제거 요청이 정상적으로 처리되었는지 검사
    if (bIsAsyncRequested == false)
    {
        // Online Session Interface의 Session 제거 완료 이벤트 해제
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
        DestroySessionCompleteDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Log, TEXT("%s DestroySession 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
    }
}

void UMDMultiplayManager::InitOnlineSessionInterface()
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

    // Online Subsystem의 Session Interface 얻기 및 유효성 검사
    SessionInterface = OnlineSubsystem->GetSessionInterface();
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem으로부터 얻은 Online Session Interface가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        return;
    }
}

TSharedPtr<const FUniqueNetId> UMDMultiplayManager::GetLocalPlayerNetID() const
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

void UMDMultiplayManager::CreateSessionInternal()
{
    // Create Session을 위하여 저장된 Parameter의 유효성 확인
    if (PendingCreateSessionParams.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 생성에 유효하지 않은 Parameter가 사용되었습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Parameter 값 복사 및 초기화
    TSharedPtr<FMDCreateSessionParams> CreateSessionParams = PendingCreateSessionParams;
    PendingCreateSessionParams.Reset();

    // Session의 Listen Server를 호스팅하는 Local Player의 Net ID 얻기 및 유효성 확인
    TSharedPtr<const FUniqueNetId> LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 생성하려는 Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 생성하려는 Session의 설정 값을 보관하는 객체 생성(초기화)
    SessionSettings = MakeShared<FOnlineSessionSettings>();

    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

    // LAN Session 사용 설정
    SessionSettings->bIsLANMatch = OnlineSubsystem != nullptr && OnlineSubsystem->GetSubsystemName() == FName("NULL");

    // Listen Server 사용 설정
    SessionSettings->bIsDedicated = false;

    // 다른 플레이어의 Session 검색 허용
    SessionSettings->bShouldAdvertise = true;

    // Session이 진행중인 경우 참가 허용
    SessionSettings->bAllowJoinInProgress = true;

    // 친구 초대 허용
    SessionSettings->bAllowInvites = true;

    // Presence를 통한 세션 참가 허용.
    SessionSettings->bAllowJoinViaPresence = true;

    // Presence 사용
    SessionSettings->bUsesPresence = true;

    // 로비 시스템을 가능한 경우 사용
    SessionSettings->bUseLobbiesIfAvailable = true;

    // 로비 음성 채팅 비활성화
    SessionSettings->bUseLobbiesVoiceChatIfAvailable = false;

    // Session 최대 접속 인원 수 설정
    SessionSettings->NumPublicConnections = CreateSessionParams->MaxPlayerCount;

    // 비공개 접속 가능 인원 수 설정
    SessionSettings->NumPrivateConnections = 0;

    // 프로젝트의 빌드 식별 값 설정
    SessionSettings->BuildUniqueId = 1;

    // 다른 프로젝트의 App ID 480 세션과 구분할 값 저장
    SessionSettings->Set(MDSessionKey::Key_ProjectID, MDSessionKey::Value_ProjectID, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 다른 플레이어에게 표시되는 Session 이름 저장
    SessionSettings->Set(MDSessionKey::Key_ServerName, CreateSessionParams->ServerName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Session의 Map(Level) 이름 저장
    SessionSettings->Set(MDSessionKey::Key_MapName, FString(TEXT("Lvl_Lobby")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Online Session Interface의 Session 생성 완료 이벤트 등록
    FOnCreateSessionCompleteDelegate CreateSessionDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleCreateSessionComplete);
    CreateSessionCompleteDelegateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionDelegate);

    // Online Session Interface를 통하여 Session 생성 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->CreateSession(*LocalPlayerNetID, NAME_GameSession, *SessionSettings);

    // Session 생성 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Online Session Interface의 Session 생성 완료 이벤트 해제
        SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
        CreateSessionCompleteDelegateHandle.Reset();

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s CreateSession 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
    }
}

void UMDMultiplayManager::HandleCreateSessionComplete(FName SessionName, bool bIsSuccess)
{
    // Online Session Interface의 Create Session 완료 이벤트 연결 해제
    if (SessionInterface.IsValid() == true)
    {
        SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
    }
    CreateSessionCompleteDelegateHandle.Reset();

    // Online Session Interface가 유효하지 않거나 Session 생성 실패 시 함수 조기 종료
    if (SessionInterface.IsValid() == false || bIsSuccess == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session(%s) 생성에 실패하였습니다."), *UE_CALL_LOCATIONINFO, *SessionName.ToString());

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 현재 World(Map) 얻기
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Create Session 성공 이벤트 실행
    OnCreateSessionComplete.Broadcast(true);

    // Lobby Map을 Listen Server로 호스팅 하도록 ServerTravel 실행
    bool bIsTravelStarted = World->ServerTravel(TEXT("/Game/Maps/Lvl_Lobby?listen"));

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s Session 생성에 성공하였습니다. Map(Lvl_Lobby) 로드 요청...(%s)"), *UE_CALL_LOCATIONINFO, (bIsTravelStarted == true ? TEXT("Success") : TEXT("Failed")));
}

void UMDMultiplayManager::HandleFindSessionsComplete(bool bIsSuccess)
{
    // Online Session Interface의 Find Sessions 완료 이벤트 연결 해제
    if (SessionInterface.IsValid() == true)
    {
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
    }
    FindSessionsCompleteDelegateHandle.Reset();

    // 검색 결과를 저장하는 배열 생성
    TArray<FMDSessionInfo> FoundSessionInfos;

    // Online Session Interface가 유효하지 않으면 함수 조기 종료
    // 검색 실패 또는 검색 결과가 유효하지 않으면 함수 조기 종료
    if (SessionInterface.IsValid() == false || SessionSearch.IsValid() == false || bIsSuccess == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 검색에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(FoundSessionInfos, false);
        return;
    }

    // 검색된 Session 목록 필터링
    for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
    {
        // 인덱스에 해당하는 Session 검색 결과 참조 및 유효성 확인
        const FOnlineSessionSearchResult& SessionSearchResult = SessionSearch->SearchResults[Index];
        if (SessionSearchResult.IsValid() == false)
        {
            continue;
        }

        // 검색된 Session의 ProjectID값 얻기 ({ Key, Value } Pair 찾기)
        FString SessionProjectID;
        bool bHasProjectID = SessionSearchResult.Session.SessionSettings.Get<FString>(MDSessionKey::Key_ProjectID, SessionProjectID);

        // Session 설정에 ProjectID 값이 존재하는지 및 현재 프로젝트의 설정과 일치하는지 검사
        if (bHasProjectID == false || SessionProjectID != MDSessionKey::Value_ProjectID)
        {
            continue;
        }

        // Session 정보를 변환하여 저장하는 객체 생성
        FMDSessionInfo SessionInfo;

        // Session 검색 결과로부터 정보 추출
        SessionInfo.ExportSessionInfo(SessionSearchResult.Session);

        // 추가 정보 기입
        SessionInfo.PingInMs = SessionSearchResult.PingInMs;
        SessionInfo.SearchResultIndex = Index;

        // 변환된 Session 정보를 목록에 추가
        FoundSessionInfos.Add(SessionInfo);
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 현재 %d개의 프로젝트 Steam 세션이 검색되었습니다."), *UE_CALL_LOCATIONINFO, FoundSessionInfos.Num());

    // Find Session 성공 이벤트 실행
    OnFindSessionsComplete.Broadcast(FoundSessionInfos, true);
}

void UMDMultiplayManager::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    // Online Session Interface의 Join Session 완료 이벤트 연결 해제
    if (SessionInterface.IsValid() == true)
    {
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
    }
    JoinSessionCompleteDelegateHandle.Reset();

    // Join Session 성공 여부 결정
    bool bIsSuccess = Result == EOnJoinSessionCompleteResult::Success || Result == EOnJoinSessionCompleteResult::AlreadyInSession;

    // Join 실패 시 함수 조기 종료
    if (bIsSuccess == false)
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

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 현재 World(Map) 얻기
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 현재 World가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 첫번째 Player Controller(주로 Local Player Controller) 얻기 및 유효성 검사
    APlayerController* PlayerController = World->GetFirstPlayerController();
    if (IsValid(PlayerController) == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 참여하려는 PlayerController가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Join Session 성공 이벤트 실행
    OnJoinSessionComplete.Broadcast(true);

    // 플레이어를 Session의 Listen Server로 이동
    PlayerController->ClientTravel(ConnectInfo, ETravelType::TRAVEL_Absolute);

    UE_LOG(MarineDemolition, Log, TEXT("%s 참여하려는 PlayerController가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
}

void UMDMultiplayManager::HandleDestroySessionComplete(FName SessionName, bool bIsSuccess)
{
    // Online Session Interface의 Destroy Session 완료 이벤트 연결 해제
    if (SessionInterface.IsValid() == true)
    {
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
    }
    DestroySessionCompleteDelegateHandle.Reset();

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s Destroy Session 프로세스가 완료되었습니다.(Result = %s)"), *UE_CALL_LOCATIONINFO, (bIsSuccess == true ? TEXT("Success") : TEXT("Failed")));

    // Session 제거 완료 이벤트 실행
    OnDestroySessionComplete.Broadcast(bIsSuccess);

    // Destroy Session 완료 이후 Create Session을 위한 유효한 Parameter가 존재하면 Create Session 프로세스 시작
    if (bIsSuccess == true && PendingCreateSessionParams.IsValid())
    {
        CreateSessionInternal();
    }
}
