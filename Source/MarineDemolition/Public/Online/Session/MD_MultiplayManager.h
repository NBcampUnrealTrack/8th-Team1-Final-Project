// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"      // Online Subsystem의 Session 관련 모듈 참조
#include "Interfaces/OnlineExternalUIInterface.h"   // Online SubSystem의 플랫폼 제공 UI 관련 모듈 참조
#include "Types/UI/MD_LoadingScreenTypes.h"
#include "MD_MultiplayManager.generated.h"

class UMD_LoadingScreenSubsystem;
class APlayerState;

struct FBlueprintSessionResult;
struct FMD_SessionCreateParams;     // Hosting Session 설정값
struct FMD_SessionFindParams;     // Find Session 설정 값

// Session 생성 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCreateMDSessionCompleteSignature, bool, bWasSuccessful);
// 게임 시작과 지정 맵 이동 요청 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStartMDGameCompleteSignature, bool, bWasSuccessful);
// 진행 중인 게임 Session 종료와 지정 맵 이동 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEndMDGameCompleteSignature, bool, bWasSuccessful);
// Session 검색 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFindMDSessionCompleteSignature, const TArray<FBlueprintSessionResult>&, FindResults, bool, bWasSuccessful);
// Session 참여 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJoinMDSessionCompleteSignature, bool, bWasSuccessful);
// Session 제거 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDestroyMDSessionCompleteSignature, bool, bWasSuccessful);

// Session 초대 수락 처리 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMDSessionInviteAcceptedCompleteSignature, bool, bWasSuccessful);

// 내부 Session 롤백 완료 후 실패를 전달할 원래 작업을 구분하는 열거형
enum class EMD_SessionRollbackOperation : uint8
{
    // Session 생성 실패에 대한 롤백
    CreateSession,

    // Session 참여 실패에 대한 롤백
    JoinSession,

    // 게임 시작 후 맵 이동 실패에 대한 롤백
    StartGame
};

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_MultiplayManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

public:
    // Session 생성 완료 시 실행되는 이벤트
    // Parameters : (bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnCreateMDSessionCompleteSignature OnCreateSessionComplete;

    // Session 시작과 지정 맵 이동 요청이 완료되었을 때 결과를 전달하는 이벤트
    // Parameters : (bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnStartMDGameCompleteSignature OnStartGameComplete;

    // 진행 중인 게임 Session 종료와 지정 맵 이동 결과 전달 이벤트
    // Parameters : (bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnEndMDGameCompleteSignature OnEndGameComplete;

    // Session 검색 완료 시 실행되는 이벤트
    // Parameters : (const TArray<FBlueprintSessionResult>& FindResults, bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnFindMDSessionCompleteSignature OnFindSessionsComplete;

    // Session 참여 완료 시 실행되는 이벤트
    // Parameters : (bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnJoinMDSessionCompleteSignature OnJoinSessionComplete;

    // Session 제거 완료 시 실행되는 이벤트
    // Parameters : (bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnDestroyMDSessionCompleteSignature OnDestroySessionComplete;

    // Session 초대 수락에 대한 처리 완료 시 실행되는 이벤트
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Steam")
    FOnMDSessionInviteAcceptedCompleteSignature OnSessionInviteAcceptedComplete;

protected:
    // Create Session 실행 시 적용되는 기본 맵
    UPROPERTY(Transient)
    TSoftObjectPtr<UWorld> DefaultSessionMap;

    // Online Subsystem의 Session 모듈
    IOnlineSessionPtr SessionInterface;

    // Online Subsystem의 External UI 모듈
    // 서비스 플랫폼에서 제공하는 공용 UI 관련
    IOnlineExternalUIPtr ExternalUIInterface;

    // Session 비동기 작업의 중복 실행과 완료 이벤트 수명을 관리하기 위한 핸들
    FDelegateHandle SessionDelegateHandle;

public:
    // 비동기 Listen Server 호스팅 절차를 시작하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void CreateSession(const FString& ServerName, int32 MaxPlayerCount, const FMD_SessionCreateParams& SessionCreateParams);

    // 지정 맵과 URL 옵션을 사용하여 기존 Session의 게임 시작 및 모든 접속자의 맵 이동 절차를 실행하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void StartSessionByMap(const FString& MapPackagePath, const FString& TravelOptions);

    // 호스트가 진행 중인 Session을 종료 상태로 전환하고 모든 접속자를 지정 맵으로 이동시키는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void EndSession(const FString& MapPackagePath);

    // 현재 GameSession이 시작되어 진행 중인지 반환하는 함수
    UFUNCTION(BlueprintPure, Category = "Marine Demolition|Session")
    bool IsSessionInProgress();

    // 지정 PlayerState가 현재 Session 소유자인지 반환하는 함수
    bool IsSessionHostPlayer(APlayerState* PlayerState);

    // 최대 MaxSearchResultCount개의 Session을 검색하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void FindSessions(int32 MaxSearchResultCount, const FMD_SessionFindParams& SessionFindParams);

    // Index 순서에 해당하는 Session에 참여하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void JoinSession(const FBlueprintSessionResult& OnlineSessionSearchResult);

    // 기존 Session을 제거하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void DestroySession(FName SessionName = NAME_None);
    //void DestroySession();

    // 서비스 플랫폼에서 제공하는 친구 초대 UI를 표시하는 함수
    UFUNCTION(BlueprintCallable, Category = "Steam Session")
    bool ShowInviteUI();

    // 서비스 플랫폼에서 제공하는 친구 목록 UI를 표시하는 함수
    UFUNCTION(BlueprintCallable, Category = "Steam Session")
    bool ShowFriendsUI();

    // 현재 생성하거나 참여한 방의 코드를 반환하는 함수
    UFUNCTION(BlueprintPure, Category = "Marine Demolition|Session")
    FString GetCurrentRoomCode() const;

    // (Only for Debug) 문자열에서 Room Code를 추출하는 함수 (Widget Blueprint Graph 전용)
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    FMD_RoomCode ParseRoomCode(FString StringRoomCode);
    
protected:
	// 로컬 화면과 서버가 소유한 각 클라이언트 화면에 실제 로딩 시작을 전달하는 공통 처리
	void BeginLoadingScreenForTravel(EMD_LoadingContext Context);

	// 로컬 인트로가 화면을 덮은 뒤에 블로킹 트래블/연결 종료를 실행한다.
	void RunAfterLoadingIntro(TFunction<void()>&& Callback);

	// Create/Start/EndGame 세 경로가 공유하는 인트로 게이트 + ServerTravel + 실패 처리
	void ServerTravelAfterLoadingIntro(const FString& TravelURL, const FString& FailureMessage,
		TFunction<void(UMD_MultiplayManager&)>&& OnFailed, TFunction<void(UMD_MultiplayManager&)>&& OnStarted);

	// 인트로 뒤에 NetDriver 연결을 끊고 DestroySession 완료를 알린다.
	void DisconnectAfterLoadingIntro();

	UMD_LoadingScreenSubsystem* GetLoadingScreen() const;

    // 프로그램 종료 시 진행 중인 작업을 마무리하고 실제 온라인 세션을 제거하는 함수
    void CleanupSessionOnShutdown();

    // Online Session Interface 객체를 초기화하는 함수
    void InitOnlineSessionInterface();

    // Local Player의 Online상에서의 ID를 반환하는 함수
    TSharedPtr<const FUniqueNetId> GetLocalPlayerNetID() const;

    // 요청된 Session을 실제로 생성하는 함수
    void CreateSessionInternal(const FString& ServerName, int32 MaxPlayerCount, const FMD_SessionCreateParams& SessionCreateParams);

    // 요청된 Session에 실제로 참여하는 함수
    void JoinSessionInternal(const FOnlineSessionSearchResult& SessionSearchResult);

    // 검증된 맵 광고 경로와 실제 이동 URL을 사용하여 Session 시작 절차를 실행하는 함수
    void StartSessionInternal(const FString& MapPackagePath, const FString& TravelURL);

    // 추가 참가를 닫은 GameSession에 실제 시작 요청을 전달하는 함수
    void RequestStartSession(const FString& TravelURL);

    // 게임 시작 준비 실패 이후 로비의 추가 참가 허용 상태를 복구하는 함수
    void RecoverStartGameJoinability();

    // Online Session Interface에서 Create Session을 완료하였을 때 호출되는 함수
    void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, const FString SessionMapPackagePath);

    // Online Session Interface에서 Start Session을 완료하면 호스트의 지정 URL 이동을 처리하는 함수
    void HandleStartSessionComplete(FName SessionName, bool bWasSuccessful, const FString TravelURL);

    // 게임 시작 전 추가 참가 차단 설정이 완료되면 실제 Session 시작을 요청하는 함수
    void HandleStartGameJoinabilityUpdateComplete(FName SessionName, bool bWasSuccessful, const FString TravelURL);

    // Online Session Interface에서 End Session을 완료하면 로비의 추가 참가 허용 설정을 요청하는 함수
    void HandleEndSessionComplete(FName SessionName, bool bWasSuccessful, const FString MapPackagePath);

    // 게임 종료 후 추가 참가 허용 설정이 완료되면 호스트의 지정 맵 이동을 처리하는 함수
    void HandleEndGameJoinabilityUpdateComplete(FName SessionName, bool bWasSuccessful, const FString MapPackagePath);

    // EndSession 맵 이동 실패 이후 Ended Session을 진행 상태로 복구하는 함수
    void RecoverEndGameSession(FName SessionName);

    // 게임 종료 복구용 세션 설정 갱신이 완료되면 Session 재시작을 요청하는 함수
    void HandleRecoverEndGameSessionSettingsComplete(FName UpdatedSessionName, bool bWasSuccessful, FName SessionName);

    // 현재 맵 정보가 복구된 Ended Session을 진행 상태로 되돌리는 함수
    void RequestRecoverEndGameSessionStart(FName SessionName);

    // Online Session Interface에서 Find Sessions을 완료하였을 때 호출되는 함수
    void HandleFindSessionsComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch);

    // Online Session Interface에서 Join Session을 완료하였을 때 호출되는 함수
    void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

    // Online Session Interface에서 Destroy Session을 완료하였을 때 호출되는 함수
    void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

    // 생성, 참여 또는 게임 시작 실패 후 남은 NamedSession을 외부 제거 이벤트 없이 정리하는 함수
    void RollbackSession(FName SessionName, EMD_SessionRollbackOperation RollbackOperation);

    // 내부 NamedSession 제거 완료 후 기존 작업의 실패 이벤트를 전달하는 함수
    void HandleRollbackSessionComplete(FName SessionName, bool bWasSuccessful, EMD_SessionRollbackOperation RollbackOperation);

    // 롤백을 요청한 생성, 참여 또는 게임 시작 작업에 최종 실패를 전달하는 함수
    void BroadcastCompleteSessionRollback(EMD_SessionRollbackOperation RollbackOperation);

    // Online Session Interface에서 Session 초대에 대하여 수락 처리를 완료하였을 때 호출되는 함수
    void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

    const bool IsValidMapPackagePath(const FString& MapPackagePath) const;
};
