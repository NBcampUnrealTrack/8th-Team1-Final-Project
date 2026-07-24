// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "MD_SessionTypes.h"
#include "MD_MultiplayManager.generated.h"

// Session 생성 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCreateMDSessionCompleteSignature, bool, bIsSuccess);
// Session 검색 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFindMDSessionCompleteSignature, const TArray<FMD_SessionInfo>&, FindResults, bool, bIsSuccess);
// Session 참여 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJoinMDSessionCompleteSignature, bool, bIsSuccess);
// Session 제거 완료 시 이벤트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDestroyMDSessionCompleteSignature, bool, bIsSuccess);

USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_CreateSessionParams
{
    GENERATED_BODY()

public:
    // Server(Session) 이름
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ServerName;

    // Session에 참여 가능한 최대 인원 수
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxPlayerCount = 0;
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

protected:
    // Online Session Interface
    IOnlineSessionPtr SessionInterface;

    // 현재 Session을 생성하는데 사용한 설정값
    TSharedPtr<FOnlineSessionSettings> SessionSettings;

    // 검색된 SessionSearchResult들을 저장하는 변수
    TSharedPtr<FOnlineSessionSearch> SessionSearch;

    // Online Session Interface의 Session 생성 완료 이벤트에 대한 함수 등록 및 제거를 담당하는 Handle 값을 저장하는 변수
    FDelegateHandle CreateSessionCompleteDelegateHandle;

    // Online Session Interface의 Session 검색 완료 이벤트에 대한 함수 등록 및 제거를 담당하는 Handle 값을 저장하는 변수
    FDelegateHandle FindSessionsCompleteDelegateHandle;

    // Online Session Interface의 Session 참가 완료 이벤트에 대한 함수 등록 및 제거를 담당하는 Handle 값을 저장하는 변수
    FDelegateHandle JoinSessionCompleteDelegateHandle;

    // Online Session Interface의 Session 제거 완료 이벤트에 대한 함수 등록 및 제거를 담당하는 Handle 값을 저장하는 변수
    FDelegateHandle DestroySessionCompleteDelegateHandle;

    // Create Session을 위하여 필요한 Parameter들을 저장하는 변수
    TSharedPtr<FMD_CreateSessionParams> PendingCreateSessionParams;

public:
    // ServerName이름으로 최대 MaxPlayerCounts명의 플레이어가 접속할 수 있는 새로운 Session을 생성하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void CreateSession(const FString& ServerName, int32 MaxPlayerCount);

    // 최대 MaxSearchResultCount개의 Session을 검색하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void FindSessions(int32 MaxSearchResultCount);

    // Index 순서에 해당하는 Session에 참여하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void JoinSession(int32 SessionIndex);

    // 기존 Session을 제거하는 함수
    UFUNCTION(BlueprintCallable, Category = "Marine Demolition|Session")
    void DestroySession();

protected:
    // Online Session Interface 객체를 초기화하는 함수
    void InitOnlineSessionInterface();

    // Local Player의 Online상에서의 ID를 반환하는 함수
    TSharedPtr<const FUniqueNetId> GetLocalPlayerNetID() const;

    // 요청된 Session을 실제로 생성하는 함수
    void CreateSessionInternal();

    // Online Session Interface에서 Create Session을 완료하였을 때 호출되는 함수
    void HandleCreateSessionComplete(FName SessionName, bool bIsSuccess);

    // Online Session Interface에서 Find Sessions을 완료하였을 때 호출되는 함수
    void HandleFindSessionsComplete(bool bIsSuccess);

    // Online Session Interface에서 Join Session을 완료하였을 때 호출되는 함수
    void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

    // Online Session Interface에서 Destroy Session을 완료하였을 때 호출되는 함수
    void HandleDestroySessionComplete(FName SessionName, bool bIsSuccess);
};
