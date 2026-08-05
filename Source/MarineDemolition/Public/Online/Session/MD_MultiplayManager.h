// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"      // Online Subsystem의 Session 관련 모듈 참조
#include "Interfaces/OnlineExternalUIInterface.h"   // Online SubSystem의 플랫폼 제공 UI 관련 모듈 참조
#include "MD_MultiplayManager.generated.h"

struct FBlueprintSessionResult;
struct FMD_SessionCreateParams;     // Hosting Session 설정값
struct FMD_SessionDestroyParams;    // Leav Session 설정 값
struct FMD_SessionFindParams;     // Find Session 설정 값

// Session 생성 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCreateMDSessionCompleteSignature, bool, bWasSuccessful);
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
    JoinSession
};

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_MultiplayManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

    // UMD_MultiplayManager 내부 로직 처리용 델리게이트
    DECLARE_DELEGATE_OneParam(FOnPostDestroyMDSessionCompleteDelegate, bool);
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

public:
    // Session 생성 완료 시 실행되는 이벤트
    // Parameters : (bool bWasSuccessful)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnCreateMDSessionCompleteSignature OnCreateSessionComplete;

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

    FDelegateHandle SessionDelegateHandle;

public:
    // 비동기 Listen Server 호스팅 절차를 시작하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void CreateSession(const FString& ServerName, int32 MaxPlayerCount, const FMD_SessionCreateParams& SessionCreateParams);

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
    // Online Session Interface 객체를 초기화하는 함수
    void InitOnlineSessionInterface();

    // Local Player의 Online상에서의 ID를 반환하는 함수
    TSharedPtr<const FUniqueNetId> GetLocalPlayerNetID() const;

    // 요청된 Session을 실제로 생성하는 함수
    void CreateSessionInternal(const FString& ServerName, int32 MaxPlayerCount, const FMD_SessionCreateParams& SessionCreateParams);

    // 요청된 Session에 실제로 참여하는 함수
    void JoinSessionInternal(const FOnlineSessionSearchResult& SessionSearchResult);

    // Online Session Interface에서 Create Session을 완료하였을 때 호출되는 함수
    void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, const FString SessionMapPackagePath);

    // Online Session Interface에서 Find Sessions을 완료하였을 때 호출되는 함수
    void HandleFindSessionsComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch);

    // Online Session Interface에서 Join Session을 완료하였을 때 호출되는 함수
    void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

    // Online Session Interface에서 Destroy Session을 완료하였을 때 호출되는 함수
    void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

    // 생성 또는 참여 실패 후 남은 NamedSession을 외부 제거 이벤트 없이 정리하는 함수
    void RollbackSession(FName SessionName, EMD_SessionRollbackOperation RollbackOperation);

    // 내부 NamedSession 제거 완료 후 기존 작업의 실패 이벤트를 전달하는 함수
    void HandleRollbackSessionComplete(FName SessionName, bool bWasSuccessful, EMD_SessionRollbackOperation RollbackOperation);

    // 롤백을 요청한 생성 또는 참여 작업에 최종 실패를 전달하는 함수
    void BroadcastCompleteSessionRollback(EMD_SessionRollbackOperation RollbackOperation);

    // Online Session Interface에서 Session 초대에 대하여 수락 처리를 완료하였을 때 호출되는 함수
    void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);

    const bool IsValidMapPackagePath(const FString& MapPackagePath) const;
};
