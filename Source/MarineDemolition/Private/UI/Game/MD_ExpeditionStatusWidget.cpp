// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_ExpeditionStatusWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UMD_ExpeditionStatusWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UWorld* World = GetWorld();

    if (!IsValid(World))
    {
        return;
    }

    UpdateStatusText();

    World->GetTimerManager().SetTimer(
        UpdateStatusTimerHandle,
        this,
        &UMD_ExpeditionStatusWidget::UpdateStatusText,
        0.1f,
        true
    );
}

void UMD_ExpeditionStatusWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(UpdateStatusTimerHandle);
    }

    Super::NativeDestruct();
}

void UMD_ExpeditionStatusWidget::UpdateStatusText()
{
    if (!IsValid(ScoreText) || !IsValid(GoalText))
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
        return;
    }

    ScoreText->SetText(FText::AsNumber(MDGameState->GetScore()));

    // 경고 표시는 문자열 대신 별도 위젯 토글 (레이아웃 유지)
    if (IsValid(WarningIndicator))
    {
        WarningIndicator->SetVisibility(MDGameState->IsInspectionWarned()
            ? ESlateVisibility::HitTestInvisible
            : ESlateVisibility::Collapsed);
    }

    const int32 Threshold =
        MDGameState->GetInspectionThreshold(MDGameState->GetCurrentDay());

    // 점검 없는 날(Day1)은 기준 표시 생략
    if (Threshold <= 0)
    {
        GoalText->SetText(FText::FromString(TEXT("-")));
        return;
    }

    GoalText->SetText(FText::AsNumber(Threshold));
}
