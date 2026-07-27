// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MD_GameModeBase.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API AMD_GameModeBase : public AGameModeBase
{
	GENERATED_BODY()
	

protected:
    virtual void BeginPlay() override;

 //TimeAlarm
private:
    FTimerHandle CheckGameTimeTimerHandle;

    void CheckGameTimeEvents();
};
