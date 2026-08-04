// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MD_SaveSlotName.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_SaveSlotName : public USaveGame
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save Slot")
	TArray<FString> SaveSlotNames;
};
