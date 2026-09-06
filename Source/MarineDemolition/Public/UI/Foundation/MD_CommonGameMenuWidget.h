// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonActivatableWidget.h"
#include "MD_CommonGameMenuWidget.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_CommonGameMenuWidget : public UMD_CommonActivatableWidget
{
	GENERATED_BODY()
	
public:
	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;
};
