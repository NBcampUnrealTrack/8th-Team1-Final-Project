// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MDTimeWidget.generated.h"

class UTextBlock;

UCLASS()
class MARINEDEMOLITION_API UMDTimeWidget : public UUserWidget
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
