// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_GameModeBase.generated.h"

class AMD_CuttableWall;
class AMD_GameStateBase;
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

    // 정식 하루 마감 진입점. 18:00 자동·모선 인터랙트·MD_EndDay 공용, 하루 1회만 동작
    void RequestEndDay();

    virtual void RestartPlayer(AController* NewPlayer) override;

    virtual void Logout(AController* Exiting) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

 //TimeAlarm
private:
    FTimerHandle CheckGameTimeTimerHandle;

    // 새 게임 또는 불러오기 데이터를 World에 적용하는 함수
    void ApplyPendingGameDataOperation();

    void CheckGameTimeEvents();

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

    // 판정 결과에 따라 다음 날 진입·클리어·게임오버로 분기
    void RouteAfterDay(EMD_InspectionResult Result);

    // 지연 타이머가 호출. PendingResult에 따라 실제 전환 실행
    void ExecuteDayTransition();

    void HandleContractCleared();

    void HandleGameOver();

    // 대상 레벨로 리슨 트래블. 미지정이면 안전하게 현재 레벨 리스타트
    void TravelToLevel(const TSoftObjectPtr<UWorld>& Level);

    // 계약 시작(Day1 마감) 시점의 인원수를 GameState에 고정
    void CaptureContractCrew();

    bool DoesSiteUseCurrentWorld(UMD_SiteDefinition* Site, UWorld* World) const;

    TSoftObjectPtr<UWorld> MakeWorldAssetReference(UWorld* World) const;

    // FinalizeDay 중복 실행 방지 플래그. 레벨 재시작 시 초기화
    bool bDayFinalized = false;

    // ExecuteDayTransition이 참조할 직전 판정 결과
    EMD_InspectionResult PendingResult = EMD_InspectionResult::NotApplicable;

    // 마감 확정 후 출발 연출을 보여줄 시간
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    float DepartTravelDelay = 3.f;

    // 클리어/게임오버 결과 화면을 보여줄 시간
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    float ResultTravelDelay = 8.f;

    // 클리어 시 이동할 레벨(모선/다음 현장 대기). BP GameMode에서 지정
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    TSoftObjectPtr<UWorld> ClearedLevel;

    // 게임오버 시 이동할 레벨(타이틀). BP GameMode에서 지정
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    TSoftObjectPtr<UWorld> GameOverLevel;

    FTimerHandle DepartTravelTimerHandle;

    // 같은 레벨 재로드 → 복원 경로에서 Day+1 진입
    void TravelToNextDay();

    // 18:00 정산 시점 요소 일괄 적용 (복귀·샘플 납품/미회수·현장 미수거)
    void FinalizeDailyScore();

    // Day2/3/4 점검 판정 + 경고/게임오버 상태 적용
    EMD_InspectionResult EvaluateInspection();

 //Day Persistence
private:
    // 18:00 마감 또는 트래블 직전에 스냅샷 기록
    void CaptureDaySnapshot();

    // 다음 틱에 스냅샷 복원 후 Merchant Day 동기화 (전 액터 BeginPlay 완료 보장)
    void InitializeExpeditionDay();

    void RestoreDaySnapshot();

    /** GameState CurrentDay와 MerchantSubsystem BeginDay를 맞춘다. 서버 전용. */
    void SyncMerchantToGameDay(int32 Day);

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

    UFUNCTION(Exec)
    void MD_SetCrew(int32 Crew);

    UFUNCTION(Exec)
    void MD_SetWarned(int32 bWarned);
};
