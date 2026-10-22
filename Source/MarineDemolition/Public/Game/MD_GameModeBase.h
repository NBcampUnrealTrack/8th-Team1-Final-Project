// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "Types/Travel/MD_TravelTypes.h"
#include "Types/UI/MD_LoadingScreenTypes.h"
#include "MD_GameModeBase.generated.h"

class AMD_CuttableWall;
class AMD_GameStateBase;
class AMD_PlayerController;
class UMD_MerchantSubsystem;
class UMD_SiteDefinition;
class UWorld;

/**
 *
 */
UCLASS()
class MARINEDEMOLITION_API AMD_GameModeBase : public AGameModeBase
{
	GENERATED_BODY()
	
public:
    AMD_GameModeBase();

    // 전체 플레이어의 정산 화면을 닫고 로비로 이동
    void FinishDayEndReport();

    // 작업 마감 콘솔의 종료 진입점. 정산 → 보고서 표시 → 로비
    void RequestEndDay(FGameplayTag ReportWidgetTag = FGameplayTag());

    virtual void StartPlay() override;
    virtual void RestartPlayer(AController* NewPlayer) override;
    virtual void GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList) override;
    bool IsStartPlayWaitingForMapRegistration() const { return bStartPlayWaitingForMapRegistration; }
    void FinishMapRegistration();

    virtual void Logout(AController* Exiting) override;

    virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // 현장 맵을 떠나거나 정산할 때 해제할 대출 이자 예약
    FTimerHandle LoanInterestTimerHandle;

    // 현장 진입 9분 뒤 첫 징수, 이후 3분 간격으로 서버 이자 계산 예약 (6분 유예 + 3분)
    // 현재 부채가 없으면 해제하고 부채가 새로 생기면 초기 대기부터 예약하는 상태 갱신
    void StartLoanInterestTimer(UMD_MerchantSubsystem* Merchant);

    // 맵 진입 기준 첫 이자 징수 시각(초). 대출·경제 회의 계산기: 6분 유예 후 9분에 첫 징수
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    float LoanFirstInterestDelaySeconds = 540.f;

    // 첫 징수 이후 이자 징수 주기(초)
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    float LoanInterestIntervalSeconds = 180.f;

    // 현재 미상환 잔액에 주기 이자를 적용하는 서버 콜백
    void CheckGameTimeEvents();

    // 참여 또는 퇴장한 플레이어의 이름을 각 소유 클라이언트에 전달
    void NotifyPlayerJoinAndExit(AController* ChangedPlayer, bool bIsJoin);

 //TimeAlarm
private:
    // 출발 요청 URL에서 검증한 계약 난이도를 GameState 생성 이후 적용하기 위한 임시 상태
    EMD_Difficulty RequestedContractDifficulty = EMD_Difficulty::Standard;

    // 직접 맵 실행을 출발 난이도 지정 계약과 구분하기 위한 옵션 존재 여부
    bool bHasRequestedContractDifficulty = false;

    // 출발 요청 URL에서 검증한 계약 인원수를 GameState 생성 이후 적용하기 위한 임시 상태
    int32 RequestedContractCrewCount = 0;

    // 직접 맵 실행을 출발 인원 지정 계약과 구분하기 위한 옵션 존재 여부
    bool bHasRequestedContractCrewCount = false;

    // 서버 GameState에 검증된 출발 난이도와 인원수를 계약 상태로 반영하는 처리
    void ApplyRequestedContractSettings();

    bool bStartPlayWaitingForMapRegistration = false;
    TArray<TWeakObjectPtr<AController>> PendingMapRegistrationPlayers;

    void FinalizeReturnStatuses();


 //Demolition Score
private:
    UFUNCTION()
    void HandleWallDemolished(AMD_CuttableWall* Wall, AActor* Cutter);

 //Contract Settlement
private:
    // 마감 정산 시퀀스. 계약당 1회만 실행
    void FinalizeDay();

    // 확정 보고서를 모든 플레이어에게 표시한 뒤 로비 이동 예약
    void ShowFinalReport(FGameplayTag ReportWidgetTag);

    // 계약 종료 처리 후 로비로 이동. 마감의 유일한 종착점
    void HandleContractCleared();

    // 팀 잔액이 음수가 되면 정산 보고서 없이 즉시 계약을 끝내고 로비로 보낸다 (2026-10-14 결정)
    void HandleBankruptcy();

    // 게임 종료 세션 상태를 갱신한 뒤 대상 로비 레벨로 이동하는 함수
    void TravelToLevel(const TSoftObjectPtr<UWorld>& Level);

    bool DoesSiteUseCurrentWorld(UMD_SiteDefinition* Site, UWorld* World) const;

public:
    // 맵별 계약 목록과 호환 후보에서 해당 월드의 현장을 찾는다. 에디터에서 CDO로 호출해도 된다
    UMD_SiteDefinition* FindExpeditionSiteForWorld(UWorld* World) const;

private:

    TSoftObjectPtr<UWorld> MakeWorldAssetReference(UWorld* World) const;

    // FinalizeDay 중복 실행 방지 플래그. 레벨 재시작 시 초기화
    bool bDayFinalized = false;

    // 마감 후 이동할 로비 레벨. BP GameMode에서 지정 (구 ClearedLevel)
    UPROPERTY(EditDefaultsOnly, Category = "Contract")
    TSoftObjectPtr<UWorld> LobbyLevel;

    FTimerHandle DepartTravelTimerHandle;

    // 세션 절차를 거치지 않는 직접 ServerTravel 전에 모든 소유 클라이언트에 로딩 화면을 요청한다.
    void PreparePlayersForTravel(EMD_LoadingContext Context);

    // 마감 시점의 인원 복귀 점수와 현장 미수거 점수를 적용하는 처리
    void FinalizeDailyScore();

 //Expedition Contract
private:
    // 다음 틱에 월드 준비를 기다린 뒤 계약 시작 (전 액터 BeginPlay 완료 보장)
    void InitializeExpeditionDay();

    // 현재 맵의 현장으로 새 계약을 시작하고 착수금 지급
    void BeginExpeditionContract();

    UMD_SiteDefinition* ResolveExpeditionSite() const;

    /** 현재 맵을 키로 사용하여 현장 데이터를 직접 선택하기 위한 맵별 계약 목록 */
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    TMap<TSoftObjectPtr<UWorld>, TObjectPtr<UMD_SiteDefinition>> ExpeditionSitesByMap;

    /** 기존 BP 설정과 단일 현장 테스트를 유지하기 위한 호환 후보 */
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    TObjectPtr<UMD_SiteDefinition> ExpeditionSite;

    /** 착수금 대출. 계약 최초 1회 지급되며 같은 금액이 부채에 가산된다 (V2 기획: 초기 도구 4종 가격 합 1,300) */
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    int32 ExpeditionAdvancePayment = 1300;

    // 테스트용 콘솔 커맨드: 정식 마감 경로(RequestEndDay) 즉시 호출
    UFUNCTION(Exec)
    void MD_EndDay();

    // 테스트용 콘솔 커맨드: 점검 상태 강제 세팅 (서버 권한에서만 동작)
    UFUNCTION(Exec)
    void MD_AddScore(int32 Delta);

    UFUNCTION(Exec)
    void MD_SetCrew(int32 Crew);
};
