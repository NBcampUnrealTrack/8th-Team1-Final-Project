// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_GameModeBase.generated.h"

class AMD_CuttableWall;

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

 //TimeAlarm
private:
    FTimerHandle CheckGameTimeTimerHandle;

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
    // 18:00 자동 마감과 MD_EndDay 공용 하루 마감 시퀀스. 하루 1회만 실행
    void FinalizeDay();

    // FinalizeDay 중복 실행 방지 플래그. 레벨 재시작 시 초기화
    bool bDayFinalized = false;

    // 마감 확정 후 출발 연출을 보여줄 시간
    UPROPERTY(EditDefaultsOnly, Category = "Day")
    float DepartTravelDelay = 3.f;

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

    // 다음 틱에 스냅샷 복원 (전 액터 BeginPlay 완료 보장)
    void RestoreDaySnapshot();

    // 테스트용 콘솔 커맨드: 정식 마감 경로(RequestEndDay) 즉시 호출
    UFUNCTION(Exec)
    void MD_EndDay();
};
