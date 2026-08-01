// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
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

    // 무지지 붕괴 점수 (기획 05: -200)
    UPROPERTY(EditDefaultsOnly, Category = "Score")
    int32 StrutCollapseScoreDelta = -200;
};
