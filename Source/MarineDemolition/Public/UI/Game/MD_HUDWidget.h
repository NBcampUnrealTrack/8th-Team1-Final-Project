// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonActivatableWidget.h"
#include "MD_HUDWidget.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_HUDWidget : public UMD_CommonActivatableWidget
{
	GENERATED_BODY()

public:
	// GameHUD가 최상단 화면일 때 게임플레이 입력을 유지한다.
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
