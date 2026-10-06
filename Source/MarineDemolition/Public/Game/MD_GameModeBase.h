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

    // 전체 플레이어의 정산 화면을 닫고 기존 판정에 따른 이동 실행
    void FinishDayEndReport();

    // 수동 퇴근과 시간 종료 원인을 구분하는 하루 마감 진입점
    void RequestEndDay(EMD_DayEndReason EndReason = EMD_DayEndReason::Console, FGameplayTag ReportWidgetTag = FGameplayTag());

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

    // 현장 진입 10분 뒤부터 3분 간격으로 서버 이자 계산 예약
    void StartLoanInterestTimer();

    // 현재 미상환 잔액에 주기 이자를 적용하는 서버 콜백
    void CheckGameTimeEvents();

    // 참여 또는 퇴장한 플레이어의 이름을 각 소유 클라이언트에 전달
    void NotifyPlayerJoinAndExit(AController* ChangedPlayer, bool bIsJoin);

 //TimeAlarm
private:
    // 출발 요청 URL에서 검증한 계약 난이도를 GameState 생성 이후 적용하기 위한 임시 상태
    EMD_Difficulty RequestedContractDifficulty = EMD_Difficulty::Standard;

    // 직접 맵 실행과 Day 재시작을 출발 난이도 지정 계약과 구분하기 위한 옵션 존재 여부
    bool bHasRequestedContractDifficulty = false;

    // 출발 요청 URL에서 검증한 계약 인원수를 GameState 생성 이후 적용하기 위한 임시 상태
    int32 RequestedContractCrewCount = 0;

    // 직접 맵 실행과 Day 재시작을 출발 인원 지정 계약과 구분하기 위한 옵션 존재 여부
    bool bHasRequestedContractCrewCount = false;

    // 서버 GameState에 검증된 출발 난이도와 인원수를 계약 상태로 반영하는 처리
    void ApplyRequestedContractSettings();

    bool bStartPlayWaitingForMapRegistration = false;
    TArray<TWeakObjectPtr<AController>> PendingMapRegistrationPlayers;

    void FinalizeReturnStatuses();


 //Collapse Score
private:
    UFUNCTION()
    void HandleStrutCollapsed(AMD_CuttableWall* Wall, AActor* Cutter);

    UFUNCTION()
    void HandleWallDemolished(AMD_CuttableWall* Wall, AActor* Cutter);

 //Daily Score & Inspection
private:
    // 18:00 자동 마감과 MD_EndDay 공용 하루 마감 시퀀스. 하루 1회만 실행. 판정 결과 반환
    EMD_InspectionResult FinalizeDay();

    // 마감 원인을 보존하고 판정에 따른 다음 날 및 최종 결과 전환
    void RouteAfterDay(EMD_InspectionResult Result, EMD_DayEndReason EndReason, FGameplayTag ReportWidgetTag);

    // 지연 타이머가 호출. PendingResult에 따라 실제 전환 실행
    void ExecuteDayTransition();

    void HandleContractCleared();

    void HandleGameOver();

    // 게임 종료 세션 상태를 갱신한 뒤 대상 로비 레벨로 이동하는 함수
    void TravelToLevel(const TSoftObjectPtr<UWorld>& Level);

    bool DoesSiteUseCurrentWorld(UMD_SiteDefinition* Site, UWorld* World) const;

    TSoftObjectPtr<UWorld> MakeWorldAssetReference(UWorld* World) const;

    // FinalizeDay 중복 실행 방지 플래그. 레벨 재시작 시 초기화
    bool bDayFinalized = false;

    // ExecuteDayTransition이 참조할 직전 판정 결과
    EMD_InspectionResult PendingResult = EMD_InspectionResult::NotApplicable;

    // 클리어 시 이동할 레벨(모선/다음 현장 대기). BP GameMode에서 지정
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    TSoftObjectPtr<UWorld> ClearedLevel;

    // 게임오버 시 이동할 레벨(타이틀). BP GameMode에서 지정
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    TSoftObjectPtr<UWorld> GameOverLevel;

    FTimerHandle DepartTravelTimerHandle;

    // 같은 레벨 재로드 → 복원 경로에서 Day+1 진입
    void TravelToNextDay();

    // 세션 절차를 거치지 않는 직접 ServerTravel 전에 모든 소유 클라이언트에 로딩 화면을 요청한다.
    void PreparePlayersForTravel(EMD_LoadingContext Context);

    // 로딩 인트로가 화면을 완전히 덮은 뒤에 같은 레벨 재시작 트래블을 실행한다.
    void RestartLevelAfterLoadingIntro();

    // 18:00 정산 시점의 인원 복귀 점수와 현장 미수거 점수를 적용하는 처리
    void FinalizeDailyScore();

    // Day2/3/4 점검 판정 + 경고/게임오버 상태 적용
    EMD_InspectionResult EvaluateInspection();

 //Day Persistence
private:
    // 다음 틱에 월드 준비를 기다린 뒤 계약 동기화 (전 액터 BeginPlay 완료 보장)
    void InitializeExpeditionDay();

    // 서버 일차와 퀘스트를 동기화하고 명시적인 새 출항만 같은 현장의 계약 재구성
    void SyncMerchantToGameDay(int32 Day, bool bStartNewContract = false);

    UMD_SiteDefinition* ResolveExpeditionSite() const;

    UFUNCTION()
    void HandleGameDayChanged(int32 NewDay);

    /** 현재 맵을 키로 사용하여 현장 데이터를 직접 선택하기 위한 맵별 계약 목록 */
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    TMap<TSoftObjectPtr<UWorld>, TObjectPtr<UMD_SiteDefinition>> ExpeditionSitesByMap;

    /** 기존 BP 설정과 단일 현장 테스트를 유지하기 위한 호환 후보 */
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    TObjectPtr<UMD_SiteDefinition> ExpeditionSite;

    /** 표준 난이도 착수금 (`07` §2.1). 계약 최초 1회만 지급된다. */
    UPROPERTY(EditDefaultsOnly, Category = "MD|Merchant")
    int32 ExpeditionAdvancePayment = 500;

    // 테스트용 콘솔 커맨드: 정식 마감 경로(RequestEndDay) 즉시 호출
    UFUNCTION(Exec)
    void MD_EndDay();

    // 테스트용 콘솔 커맨드: 점검 상태 강제 세팅 (서버 권한에서만 동작)
    UFUNCTION(Exec)
    void MD_AddScore(int32 Delta);

    UFUNCTION(Exec)
    void MD_SetDay(int32 Day);

    // 현재 일자의 시간을 H시 00분으로 설정하는 호스트 콘솔 명령
    UFUNCTION(Exec)
    void MD_SetTime(int32 Hour);

    UFUNCTION(Exec)
    void MD_SetCrew(int32 Crew);

    UFUNCTION(Exec)
    void MD_SetWarned(int32 bWarned);
};
