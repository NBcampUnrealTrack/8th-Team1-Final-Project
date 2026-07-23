// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MDGameModeBase.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API AMDGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
	

protected:
    virtual void BeginPlay() override;

 //TimeAlarm
private:
    FTimerHandle CheckGameTimeTimerHandle;

    bool bWithdrawalNoticeLogged = false;
    bool bWithdrawalWarningLogged = false;
    bool bShipDepartureLogged = false;

    void CheckGameTimeEvents();
};
