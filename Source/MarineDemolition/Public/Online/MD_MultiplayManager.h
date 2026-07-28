// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"      // Online Subsystem의 Session 관련 모듈 참조
#include "Interfaces/OnlineExternalUIInterface.h"   // Online SubSystem의 플랫폼 제공 UI 관련 모듈 참조
#include "MD_MultiplayManager.generated.h"

struct FBlueprintSessionResult;

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

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_CreateSessionParams
{
    GENERATED_BODY()

public:
    // Session Hosting 방식
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 bIsDedicatedServer : 1 = false;

    // Session 생성 시 최초로 로드되는 맵
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UWorld> InitialSessionMap;

public:
    inline const bool IsValid() const { return InitialSessionMap.IsNull() == false; }
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
    // Parameters : (bool bIsSuccess)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnCreateMDSessionCompleteSignature OnCreateSessionComplete;

    // Session 검색 완료 시 실행되는 이벤트
    // Parameters : (const TArray<FMD_SessionInfo>& FindResults, bool bIsSuccess)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnFindMDSessionCompleteSignature OnFindSessionsComplete;

    // Session 참여 완료 시 실행되는 이벤트
    // Parameters : (bool bIsSuccess)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnJoinMDSessionCompleteSignature OnJoinSessionComplete;

    // Session 제거 완료 시 실행되는 이벤트
    // Parameters : (bool bIsSuccess)
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Session")
    FOnDestroyMDSessionCompleteSignature OnDestroySessionComplete;

    // Session 초대 수락에 대한 처리 완료 시 실행되는 이벤트
    UPROPERTY(BlueprintAssignable, Category = "Marine Demolition|Steam")
    FOnMDSessionInviteAcceptedCompleteSignature OnSessionInviteAcceptedComplete;

protected:
    // Online Subsystem의 Session 모듈
    IOnlineSessionPtr SessionInterface;

    // Online Subsystem의 External UI 모듈
    // 서비스 플랫폼에서 제공하는 공용 UI 관련
    IOnlineExternalUIPtr ExternalUIInterface;

    // Destroy Session이 완료된 이후 실행되는 이벤트(클래스 내부 C++ 전용)
    FOnPostDestroyMDSessionCompleteDelegate OnPostDestroySessionComplete;

public:
    // ServerName이름으로 최대 MaxPlayerCounts명의 플레이어가 접속할 수 있는 새로운 Session을 생성하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void CreateSession(const FString& ServerName, int32 MaxPlayerCount, FMD_CreateSessionParams CreateSessionParams);

    // 최대 MaxSearchResultCount개의 Session을 검색하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void FindSessions(int32 MaxSearchResultCount);

    // Index 순서에 해당하는 Session에 참여하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void JoinSessionBySessionResult(const FBlueprintSessionResult& OnlineSessionSearchResult);

    // 방 코드를 검색하여 해당 Session에 참여하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void JoinSessionByRoomCode(const struct FMD_RoomCode& RoomCode);

    // 기존 Session을 제거하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void DestroySession();

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
    void CreateSessionInternal(const FString& ServerName, int32 MaxPlayerCount, FMD_CreateSessionParams CreateSessionParams);

    // 요청된 Session에 실제로 참여하는 함수
    void JoinSessionInternal(const FOnlineSessionSearchResult& SessionResult);

    // Online Session Interface에서 Create Session을 완료하였을 때 호출되는 함수
    void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful, FMD_CreateSessionParams CreateSessionParams);

    // Online Session Interface에서 Find Sessions을 완료하였을 때 호출되는 함수
    void HandleFindSessionsComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch);

    // 방 코드 Session 검색을 완료하였을 때 호출되는 함수
    void HandleFindSessionByRoomCodeComplete(bool bWasSuccessful, TSharedPtr<FOnlineSessionSearch> SessionSearch);

    // Online Session Interface에서 Join Session을 완료하였을 때 호출되는 함수
    void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

    // Online Session Interface에서 Destroy Session을 완료하였을 때 호출되는 함수
    void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

    // Online Session Interface에서 Session 초대에 대하여 수락 처리를 완료하였을 때 호출되는 함수
    void HandleSessionUserInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);
};
