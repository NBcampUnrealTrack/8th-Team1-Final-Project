// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MD_PlayerController.generated.h"

class UMD_HUDWidget;

UCLASS()
class MARINEDEMOLITION_API AMD_PlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMD_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UMD_HUDWidget> HUDWidgetInstance;
};
