// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/MD_MultiplayManager.h"
#include "OnlineSubsystem.h"            // IOnlineSubsystem
#include "OnlineSessionSettings.h"      // FOnlineSessionSettings
#include "Online/OnlineSessionNames.h"  // NAME_GameSession
#include "FindSessionsCallbackProxy.h"  // FBlueprintSessionResult
#include "MD_SessionSettingKey.h"
#include "MD_LogUtility.h"
#include "Online/MD_RoomCode.h"
#include "GameFramework/GameModeBase.h"

void UMD_MultiplayManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

    InitOnlineSessionInterface();

    // Session 초대 수락 처리 완료에 대한 이벤트 등록
    if (SessionInterface.IsValid() == true)
    {
        SessionInterface->OnSessionUserInviteAcceptedDelegates.AddUObject(this, &ThisClass::HandleSessionUserInviteAccepted);
    }

    OnDestroySessionComplete.AddDynamic(this, &ThisClass::ExecutePostDestroySessionComplete);
}

void UMD_MultiplayManager::Deinitialize()
{
    OnDestroySessionComplete.RemoveAll(this);

    // Online Session의 유효성 검사
    if (SessionInterface.IsValid() == true)
    {
        // Session 생성 완료에 대한 이벤트 연결 해제
        SessionInterface->OnCreateSessionCompleteDelegates.RemoveAll(this);

        // Session 검색 완료에 대한 이벤트 연결 해제
        SessionInterface->OnFindSessionsCompleteDelegates.RemoveAll(this);

        // Session 참여 완료에 대한 이벤트 연결 해제
        SessionInterface->OnJoinSessionCompleteDelegates.RemoveAll(this);

        // Session 제거 완료에 대한 이벤트 연결 해제
        SessionInterface->OnDestroySessionCompleteDelegates.RemoveAll(this);

        // Session 초대 수락 처리 완료에 대한 이벤트 연결 해제
        SessionInterface->OnSessionUserInviteAcceptedDelegates.RemoveAll(this);
    }

    SessionInterface.Reset();
    ExternalUIInterface.Reset();

	Super::Deinitialize();
}

void UMD_MultiplayManager::CreateSession(const FString& ServerName, int32 MaxPlayerCount, FMD_CreateSessionParams CreateSessionParams)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    if (MaxPlayerCount <= 0)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 최대 인원 수가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Create Session을 위한 파라미터 유효성 확인
    if (CreateSessionParams.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Create Session을 위한 Parameter가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 이전에 참여했던 Session 정보가 남아있는지 확인
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession != nullptr)
    {
        // Destroy Session 완료 이후 실제 Create Session 동작 함수가 호출되도록 Binding
        OnPostDestroySessionComplete.BindLambda([this, ServerName, MaxPlayerCount, CreateSessionParams](bool bWasSuccessful)
            {
                // Destroy Session 성공 시 Session 생성 함수 호출
                if (bWasSuccessful == true)
                {
                    CreateSessionInternal(ServerName, MaxPlayerCount, CreateSessionParams);
                }
            });

        // 이전 Session 삭제
        DestroySession();
        return;
    }

    // 실제 Session 생성
    CreateSessionInternal(ServerName, MaxPlayerCount, CreateSessionParams);
}

void UMD_MultiplayManager::FindSessions(int32 MaxSearchResultCount)
{
    // Find Session 실패 시 반환될 검색 결과
    const TArray<FBlueprintSessionResult> EmptySearchResults;

    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }

    // Session 검색 요청(비동기) 중복 동작 방지
    if (SessionInterface->OnFindSessionsCompleteDelegates.IsBoundToObject(this) == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 이전 Create Session 프로세스가 처리 중입니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
        return;
    }

    // 최대 검색 결과 수 유효성 검사
    if (MaxSearchResultCount <= 0)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Find Sessions 결과를 저장하는 최대 수가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

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

    // Online Subsystem 얻기
    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

    // Online Subsystem이 NULL Subsystem이면 LAN 검색 사용
    SessionSearch->bIsLanQuery = OnlineSubsystem != nullptr && OnlineSubsystem->GetSubsystemName() == FName(TEXT("NULL"));

    // 로비 Session 검색 설정
    SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

    SessionSearch->QuerySettings.Set(MD_SessionKey::Key_ProjectID, MD_SessionKey::Value_ProjectID, EOnlineComparisonOp::Equals);

    // Session 모듈의 Session 검색 완료 이벤트 연결
    SessionInterface->OnFindSessionsCompleteDelegates.AddUObject(this, &ThisClass::HandleFindSessionsComplete, SessionSearch);

    // Session 모듈을 통하여 Session 검색 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->FindSessions(*LocalPlayerNetID, SessionSearch.ToSharedRef());

    // Session 검색 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Session 모듈의 Session 검색 완료 이벤트 해제
        SessionInterface->OnFindSessionsCompleteDelegates.RemoveAll(this);

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s FindSessions 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(EmptySearchResults, false);
    }
}

void UMD_MultiplayManager::JoinSessionBySessionResult(const FBlueprintSessionResult& BlueprintSessionResult)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 참여하려는 Session 정보의 유효성 검사
    if (BlueprintSessionResult.OnlineResult.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 참여하려는 Session 정보가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 이전에 참여했던 또는 현재 참여중인 Session 정보가 있는지 확인
    FNamedOnlineSession* ExistingSession = SessionInterface->GetNamedSession(NAME_GameSession);
    if (ExistingSession != nullptr)
    {
        // Destroy Session 완료 이후 실제 Join Session 동작 함수가 호출되도록 Binding
        OnPostDestroySessionComplete.BindLambda([this, BlueprintSessionResult](bool bWasSuccessful)
            {
                // Destroy Session 성공 시 Session 참여 함수 호출
                if (bWasSuccessful == true)
                {
                    JoinSessionInternal(BlueprintSessionResult.OnlineResult);
                }
            });

        // 이전 Session 삭제
        DestroySession();
        return;
    }

    // Session 참여 함수 호출
    JoinSessionInternal(BlueprintSessionResult.OnlineResult);
}

void UMD_MultiplayManager::JoinSessionByRoomCode(const FMD_RoomCode& RoomCode)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 검색 요청(비동기) 중복 동작 방지
    if (SessionInterface->OnFindSessionsCompleteDelegates.IsBoundToObject(this) == true)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 다른 Session 검색이 진행 중입니다."), *UE_CALL_LOCATIONINFO);
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 방 코드 유효성 확인
    if (RoomCode.IsValid() == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 방 코드가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Local Player의 Net ID 얻기 및 유효성 검사
    const FUniqueNetIdPtr LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false || LocalPlayerNetID->IsValid() == false)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s Local Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    const TSharedPtr<FOnlineSessionSearch> SessionSearch = MakeShared<FOnlineSessionSearch>();
    SessionSearch->MaxSearchResults = 10;

    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
    SessionSearch->bIsLanQuery = OnlineSubsystem != nullptr && OnlineSubsystem->GetSubsystemName() == FName(TEXT("NULL"));

    SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
    SessionSearch->QuerySettings.Set(MD_SessionKey::Key_ProjectID, MD_SessionKey::Value_ProjectID, EOnlineComparisonOp::Equals);
    SessionSearch->QuerySettings.Set(MD_SessionKey::Key_RoomCode, RoomCode.ToString(), EOnlineComparisonOp::Equals);

    SessionInterface->OnFindSessionsCompleteDelegates.AddUObject(this, &ThisClass::HandleFindSessionByRoomCodeComplete, SessionSearch);

    // Session 모듈을 통하여 Session 검색 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->FindSessions(*LocalPlayerNetID, SessionSearch.ToSharedRef());

    // Session 검색 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Session 모듈의 Session 검색 완료 이벤트 해제
        SessionInterface->OnFindSessionsCompleteDelegates.RemoveAll(this);

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 방 코드를 통한 Session 검색 요청에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        OnJoinSessionComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::DestroySession()
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    // Session 제거 요청(비동기) 중복 동작 방지
    if (SessionInterface->OnDestroySessionCompleteDelegates.IsBoundToObject(this) == true)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s 이전 Destroy Session 프로세스를 처리 중입니다."), *UE_CALL_LOCATIONINFO);

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

    // Session 모듈의 Session 제거 완료 이벤트 등록
    SessionInterface->OnDestroySessionCompleteDelegates.AddUObject(this, &ThisClass::HandleDestroySessionComplete);

    // Session 모듈을 통하여 Session 제거 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->DestroySession(NAME_GameSession);

    // Session 제거 요청이 정상적으로 처리되었는지 검사
    if (bIsAsyncRequested == false)
    {
        // Session 모듈의 Session 제거 완료 이벤트 해제
        SessionInterface->OnDestroySessionCompleteDelegates.RemoveAll(this);

        // 로그 출력
        UE_LOG(MarineDemolition, Log, TEXT("%s DestroySession 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
    }
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
        bool bHasRoomCode = NamedOnlineSession->SessionSettings.Get(MD_SessionKey::Key_RoomCode, StringRoomCode);
        
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

void UMD_MultiplayManager::CreateSessionInternal(const FString& ServerName, int32 MaxPlayerCount, FMD_CreateSessionParams CreateSessionParams)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Session 생성 요청(비동기) 중복 동작 방지
    if (SessionInterface->OnCreateSessionCompleteDelegates.IsBoundToObject(this) == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 이전 Create Session 프로세스가 처리 중입니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    if (MaxPlayerCount <= 0)
    {
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 최대 인원 수가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Create Session을 위한 Parameter의 유효성 확인
    if (CreateSessionParams.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 생성에 유효하지 않은 Parameter가 사용되었습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

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
    TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShared<FOnlineSessionSettings>();

    FMD_RoomCode RoomCode = FMD_RoomCode::Random();

    IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

    // LAN Session 사용 설정
    SessionSettings->bIsLANMatch = OnlineSubsystem != nullptr && OnlineSubsystem->GetSubsystemName() == FName("NULL");

    // Listen Server 사용 설정
    SessionSettings->bIsDedicated = CreateSessionParams.bIsDedicatedServer;

    // 다른 플레이어의 Session 검색 허용
    SessionSettings->bShouldAdvertise = true;

    // Session이 진행중인 경우 참가 허용
    SessionSettings->bAllowJoinInProgress = true;

    // Presence 사용
    SessionSettings->bUsesPresence = true;

    // Presence를 통한 세션 참가 허용.
    SessionSettings->bAllowJoinViaPresence = true;

    // 친구 초대 허용
    SessionSettings->bAllowInvites = true;

    //// 친구만 Presence를 통한 세션 참가 허용.
    //SessionSettings->bAllowJoinViaPresenceFriendsOnly = true;

    // 로비 시스템을 가능한 경우 사용
    SessionSettings->bUseLobbiesIfAvailable = true;

    // 로비 음성 채팅 활성화
    SessionSettings->bUseLobbiesVoiceChatIfAvailable = true;

    // Session 최대 접속 인원 수 설정
    SessionSettings->NumPublicConnections = MaxPlayerCount;

    // 비공개 접속 가능 인원 수 설정
    SessionSettings->NumPrivateConnections = 0;

    // 프로젝트의 빌드 식별 값 설정
    SessionSettings->BuildUniqueId = 1;

    // 다른 프로젝트의 App ID 480 세션과 구분할 값 저장
    SessionSettings->Set(MD_SessionKey::Key_ProjectID, MD_SessionKey::Value_ProjectID, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 다른 플레이어에게 표시되는 Session 이름 저장
    SessionSettings->Set(MD_SessionKey::Key_ServerName, ServerName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Session의 Map(Level) Asset 이름 저장
    SessionSettings->Set(MD_SessionKey::Key_MapName, CreateSessionParams.InitialSessionMap.GetAssetName(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // 방 코드 저장
    SessionSettings->Set(MD_SessionKey::Key_RoomCode, RoomCode.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

    // Session 모듈의 Session 생성 완료 이벤트 연결
    SessionInterface->OnCreateSessionCompleteDelegates.AddUObject(this, &ThisClass::HandleCreateSessionComplete, CreateSessionParams);

    // Session 모듈을 통하여 Session 생성 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->CreateSession(*LocalPlayerNetID, NAME_GameSession, *SessionSettings);

    // Session 생성 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Session 모듈의 Session 생성 완료 이벤트 해제
        SessionInterface->OnCreateSessionCompleteDelegates.RemoveAll(this);

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s CreateSession 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::JoinSessionInternal(const FOnlineSessionSearchResult& SessionResult)
{
    // Online Subsystem의 Session 모듈 유효성 확인
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 참여 요청(비동기) 중복 동작 방지
    if (SessionInterface->OnJoinSessionCompleteDelegates.IsBoundToObject(this) == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s 이전 Join Session 프로세스를 처리 중입니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // 참여하려는 Session 정보에 대한 유효성 확인
    if (SessionResult.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 참여하려는 Session 정보가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Local Player의 Net ID 얻기 및 유효성 검사
    FUniqueNetIdPtr LocalPlayerNetID = GetLocalPlayerNetID();
    if (LocalPlayerNetID.IsValid() == false || LocalPlayerNetID->IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Local Player의 Net ID가 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈의 Session 참여 완료 이벤트 연결
    SessionInterface->OnJoinSessionCompleteDelegates.AddUObject(this, &ThisClass::HandleJoinSessionComplete);

    // Session 모듈을 통하여 Session 참여 요청(비동기 프로세스)
    bool bIsAsyncRequested = SessionInterface->JoinSession(*LocalPlayerNetID, NAME_GameSession, SessionResult);

    // Session 참여 요청이 정상적으로 처리되었는지 확인
    if (bIsAsyncRequested == false)
    {
        // Session 모듈의 Session 참여 완료 이벤트 연결 해제
        SessionInterface->OnJoinSessionCompleteDelegates.RemoveAll(this);

        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s JoinSession 요청이 정상적으로 처리되지 않았습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
    }
}

void UMD_MultiplayManager::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, FMD_CreateSessionParams CreateSessionParams)
{
    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈의 Create Session 완료 이벤트 연결 해제
    SessionInterface->OnCreateSessionCompleteDelegates.RemoveAll(this);

    // Session 생성 실패 시 함수 조기 종료
    if (bWasSuccessful == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 생성에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Create Session 실패 이벤트 실행
        OnCreateSessionComplete.Broadcast(false);
        return;
    }

    // 초기 Session의 Map이 존재하는지 검사
    if (CreateSessionParams.InitialSessionMap.IsNull() == true)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session의 Map이 설정되지 않았습니다."), *UE_CALL_LOCATIONINFO);

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

    // ServerTravel을 위한 URL 생성
    FString TravelURL = CreateSessionParams.InitialSessionMap.GetLongPackageFName().ToString();
    if (CreateSessionParams.bIsDedicatedServer == false)
    {
        TravelURL = TravelURL.Append(TEXT("?listen"));
    }

    // 심리스 트래블은 Listen을 시작하지 않으므로 첫 트래블은 하드 트래블로 강제
    if (AGameModeBase* GameMode = World->GetAuthGameMode())
    {
        GameMode->bUseSeamlessTravel = false;
    }

    // 초기 맵을 호스팅 하도록 ServerTravel 실행
    bool bIsTravelStarted = World->ServerTravel(TravelURL);

    // Create Session 성공 이벤트 실행
    OnCreateSessionComplete.Broadcast(bIsTravelStarted);

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s Session 생성에 성공하였습니다. ServerTravel(%s)...(%s)"), *UE_CALL_LOCATIONINFO, *TravelURL, (bIsTravelStarted == true ? TEXT("Success") : TEXT("Failed")));
}

void UMD_MultiplayManager::HandleFindSessionsComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch)
{
    // 검색 결과를 저장하는 배열 생성
    TArray<FBlueprintSessionResult> SearchResults;

    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(SearchResults, false);
        return;
    }

    // Session 모듈의 Find Sessions 완료 이벤트 연결 해제
    SessionInterface->OnFindSessionsCompleteDelegates.RemoveAll(this);

    // 검색 실패 또는 검색 결과가 유효하지 않으면 함수 조기 종료
    if (bWasSuccessful == false || SessionSearch.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Session 검색에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Find Sessions 실패 이벤트 실행
        OnFindSessionsComplete.Broadcast(SearchResults, false);
        return;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 다른 프로젝트 포함 %d개의 공개 Session이 검색되었습니다."), *UE_CALL_LOCATIONINFO, SessionSearch->SearchResults.Num());

    // 검색된 SessionSearchResult 목록 필터링
    for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
    {
        // 인덱스에 해당하는 SessionSearchResult 객체 참조 및 유효성 확인
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
        BlueprintSearchResult.OnlineResult = SessionSearch->SearchResults[Index];
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 현재 %d개의 프로젝트 Steam 세션이 검색되었습니다."), *UE_CALL_LOCATIONINFO, SearchResults.Num());

    // Find Session 성공 이벤트 실행
    OnFindSessionsComplete.Broadcast(SearchResults, true);
}

void UMD_MultiplayManager::HandleFindSessionByRoomCodeComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch)
{
    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈의 Find Sessions 완료 이벤트 연결 해제
    SessionInterface->OnFindSessionsCompleteDelegates.RemoveAll(this);

    // 검색 실패 또는 검색 결과가 유효하지 않으면 함수 조기 종료
    if (bWasSuccessful == false || SessionSearch.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 방 코드를 통한 Session 검색에 실패하였습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    if (SessionSearch->SearchResults.Num() != 1)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 동일한 방 코드가 설정된 Session이 존재하지 않거나 너무 많습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    for (const FOnlineSessionSearchResult& SearchResult : SessionSearch->SearchResults)
    {
        if (SearchResult.IsValid() == false)
        {
            continue;
        }

        // 검색된 Session에 참여
        FBlueprintSessionResult BlueprintSessionResult;
        BlueprintSessionResult.OnlineResult = SearchResult;
        JoinSessionBySessionResult(BlueprintSessionResult);
        return;
    }

    UE_LOG(MarineDemolition, Warning, TEXT("%s 방 코드에 해당하는 Session을 찾지 못했습니다."), *UE_CALL_LOCATIONINFO);
    OnJoinSessionComplete.Broadcast(false);
}

void UMD_MultiplayManager::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Join Session 실패 이벤트 실행
        OnJoinSessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈의 Join Session 완료 이벤트 연결 해제
    SessionInterface->OnJoinSessionCompleteDelegates.RemoveAll(this);

    // Join 실패 시 함수 조기 종료
    if (Result != EOnJoinSessionCompleteResult::Success)
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

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 플레이어를 Session의 Map으로 이동합니다.(%s)"), *UE_CALL_LOCATIONINFO, *ConnectInfo);

    // Join Session 성공 이벤트 실행
    OnJoinSessionComplete.Broadcast(true);

    // 플레이어를 Session의 Listen Server로 이동
    PlayerController->ClientTravel(ConnectInfo, ETravelType::TRAVEL_Absolute);
}

void UMD_MultiplayManager::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
    // Online Subsystem의 Session 모듈 유효성 검사
    if (SessionInterface.IsValid() == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Online Subsystem의 Session 모듈이 유효하지 않습니다."), *UE_CALL_LOCATIONINFO);

        // Session 제거 실패 이벤트 실행
        OnDestroySessionComplete.Broadcast(false);
        return;
    }

    // Session 모듈의 Destroy Session 완료 이벤트 연결 해제
    SessionInterface->OnDestroySessionCompleteDelegates.RemoveAll(this);

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s Destroy Session에 %s하였습니다."), *UE_CALL_LOCATIONINFO, (bWasSuccessful == true ? TEXT("성공") : TEXT("실패")));

    // Session 제거 완료 이벤트 실행
    OnDestroySessionComplete.Broadcast(bWasSuccessful);
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

    // 초대받은 Session에 참여 과정 실행
    FBlueprintSessionResult InviteSessionResult;
    InviteSessionResult.OnlineResult = InviteResult;
    JoinSessionBySessionResult(InviteSessionResult);

}

void UMD_MultiplayManager::ExecutePostDestroySessionComplete(bool bWasSuccessful)
{
    // Session 제거 완료 이후 처리해야 하는 과정이 있으면 실행
    OnPostDestroySessionComplete.ExecuteIfBound(bWasSuccessful);

    // Destroy Session 프로세스 진행 중 OnPostDestroySessionComplete 이벤트 연결 제거 방지
    if (SessionInterface.IsValid() == true)
    {
        if (SessionInterface->OnDestroySessionCompleteDelegates.IsBoundToObject(this) == true)
        {

            // Session 제거 완료 이후 처리해야 하는 과정 제거
            OnPostDestroySessionComplete.Unbind();
        }
    }
}
