// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MD_TimeWidget.h"
#include "Components/TextBlock.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UMD_TimeWidget::NativeConstruct()
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
        &UMD_TimeWidget::UpdateTimeText,
        0.1f,
        true
    );
}

void UMD_TimeWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(UpdateTimeTimerHandle);
    }

    Super::NativeDestruct();
}

void UMD_TimeWidget::UpdateTimeText()
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

    const AMD_GameStateBase* MDGameState =
        World->GetGameState<AMD_GameStateBase>();

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
