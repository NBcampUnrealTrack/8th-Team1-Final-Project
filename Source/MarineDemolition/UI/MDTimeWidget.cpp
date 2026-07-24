// Fill out your copyright notice in the Description page of Project Settings.


#include "MDTimeWidget.h"
#include "Components/TextBlock.h"
#include "MarineDemolition/Game/MDGameStateBase.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UMDTimeWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UWorld* World = GetWorld();

    if (!IsValid(World))
    {
        return;
    }

    UpdateTimeText();

    GetWorld()->GetTimerManager().SetTimer(
        UpdateTimeTimerHandle,
        this,
        &UMDTimeWidget::UpdateTimeText,
        0.1f,
        true
    );
}

void UMDTimeWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(UpdateTimeTimerHandle);
    }

    Super::NativeDestruct();
}

void UMDTimeWidget::UpdateTimeText()
{
    if (!IsValid(TimeText))
    {
        return;
    }

    const UWorld* World = GetWorld();

    if (!IsValid(World))
    {
        return;
    }

    const AMDGameStateBase* MDGameState =
        World->GetGameState<AMDGameStateBase>();

    if (!IsValid(MDGameState))
    {
        TimeText->SetText(FText::FromString(TEXT("--:--")));
        return;
    }

    const int32 CurrentHour =
        MDGameState->GetCurrentHour();

    const int32 CurrentMinute =
        MDGameState->GetCurrentMinute();

    const FString TimeString = FString::Printf(
        TEXT("%02d:%02d"),
        CurrentHour,
        CurrentMinute
    );

    TimeText->SetText(FText::FromString(TimeString));
}
