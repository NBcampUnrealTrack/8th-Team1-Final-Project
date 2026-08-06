// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MD_GameInstance.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_GameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init() override;

	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;
};
