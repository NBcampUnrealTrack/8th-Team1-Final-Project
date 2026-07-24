// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_TimeWidget.generated.h"

class UTextBlock;

UCLASS()
class MARINEDEMOLITION_API UMD_TimeWidget : public UUserWidget
{
	GENERATED_BODY()
	
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TimeText;

    FTimerHandle UpdateTimeTimerHandle;

    void UpdateTimeText();
};
