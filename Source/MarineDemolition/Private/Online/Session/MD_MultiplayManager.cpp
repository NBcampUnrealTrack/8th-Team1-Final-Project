// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/Session/MD_MultiplayManager.h"
#include "OnlineSubsystem.h"            // IOnlineSubsystem
#include "OnlineSessionSettings.h"      // FOnlineSessionSettings
#include "Online/OnlineSessionNames.h"  // NAME_GameSession
#include "FindSessionsCallbackProxy.h"  // FBlueprintSessionResult
#include "GameFramework/GameModeBase.h"
#include "Engine/AssetManager.h"

#include "Online/Session/MD_SessionSettingKey.h"
#include "Types/Online/MD_RoomCode.h"
#include "Types/Online/MD_SessionTypes.h"
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

    // Online Session 비동기 생성 절차 실행
    CreateSessionInternal(ServerName, MaxPlayerCount, SessionCreateParams);
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

    // SessionName의 기본값 설정(UFUNCTION 측에서 NAME_GameSession을 인식하지 못하여 .cpp 측에서 설정)
    SessionName = SessionName == NAME_None ? NAME_GameSession : SessionName;

    // 이전의 Session이 존재하는지 검사
    if (SessionInterface->GetNamedSession(SessionName) == nullptr)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s 현재 참여하거나 호스팅 중인 GameSession이 없습니다."), *UE_CALL_LOCATIONINFO);

        // Destroy Session 성공 이벤트 실행 (삭제할 Session이 존재하지 않음)
        OnDestroySessionComplete.Broadcast(false);
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
    SessionSettings.bShouldAdvertise = SessionCreateParams.bPrivateSession == false;		        // 비공개 Session 여부 설정
    SessionSettings.bAllowJoinInProgress = false;		                                    // 게임 진행 중 참여 불가 고정
    SessionSettings.bUsesPresence = true;				                                    // Presence 사용 고정
    SessionSettings.bAllowJoinViaPresence = SessionCreateParams.bAllowJoinByFollow;		            // 친구의 Presence 정보를 통하여 Session을 찾아 참여 허용 여부 설정
    SessionSettings.bAllowJoinViaPresenceFriendsOnly = SessionCreateParams.bAllowJoinByFollow;		// 친구 전용 Presence를 통한 Session을 참여 허용 여부 설정
    SessionSettings.bAllowInvites = true;				                                    // 초대 허용 고정
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

    // 심리스 트래블은 Listen을 시작하지 않으므로 첫 트래블은 하드 트래블로 강제
    // 최초 Listen 이동의 경우 하드 트래블으로 설정되어야만 "?listen" 옵션이 새 NetDriver를 정상 생성할 수 있다.
    AGameModeBase* GameMode = World->GetAuthGameMode();
    if (IsValid(GameMode) == true)
    {
        GameMode->bUseSeamlessTravel = false;
    }

    // Session Map의 패키지 이름에 "?listen" 옵션 추가
    FString TravelURL = FString::Printf(TEXT("%s?listen"), *SessionMapPackagePath);

    // 현재 클라이언트를 Listen Server의 호스트로 설정된 맵 호스팅
    bool bWasTravelStarted = World->ServerTravel(TravelURL);

    if (bWasTravelStarted == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Listen Server 맵 이동을 시작하지 못했습니다: %s"), *UE_CALL_LOCATIONINFO, *TravelURL);

        // 생성에 성공한 NamedSession을 조용히 제거한 뒤 Create Session 실패 이벤트를 전달합니다.
        RollbackSession(SessionName, EMD_SessionRollbackOperation::CreateSession);
    }

    // 맵 이동이 시작된 경우에만 Create Session 성공 이벤트를 즉시 전달합니다.
    if (bWasTravelStarted == true)
    {
        OnCreateSessionComplete.Broadcast(true);
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

    // 플레이어를 Session의 Listen Server로 이동
    PlayerController->ClientTravel(ConnectInfo, ETravelType::TRAVEL_Absolute);

    // Join Session 성공 이벤트 실행
    OnJoinSessionComplete.Broadcast(true);
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

    // 현재 NetDriver 연결 종료
    // 클라이언트 : Session에서 나가기 / Listen Server Host : 서버 닫기
    Engine->HandleDisconnect(World, World->GetNetDriver());

    // Session 제거 성공 이벤트 실행
    OnDestroySessionComplete.Broadcast(true);
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
