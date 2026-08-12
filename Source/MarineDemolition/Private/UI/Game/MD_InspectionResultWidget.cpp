// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_InspectionResultWidget.h"
#include "Components/TextBlock.h"
#include "Game/MD_GameStateBase.h"
#include "TimerManager.h"
#include "Engine/World.h"

void UMD_InspectionResultWidget::NativeConstruct()
{
    Super::NativeConstruct();

    SetVisibility(ESlateVisibility::Collapsed);

    TryBindToGameState();
}

void UMD_InspectionResultWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
        World->GetTimerManager().ClearTimer(HideTimerHandle);

        if (AMD_GameStateBase* MDGameState = World->GetGameState<AMD_GameStateBase>())
        {
            MDGameState->OnInspectionReport.RemoveDynamic(
                this, &UMD_InspectionResultWidget::HandleInspectionReport);
        }
    }

    Super::NativeDestruct();
}

void UMD_InspectionResultWidget::TryBindToGameState()
{
    UWorld* World = GetWorld();

    if (!IsValid(World))
    {
        return;
    }

    AMD_GameStateBase* MDGameState = World->GetGameState<AMD_GameStateBase>();

    if (!IsValid(MDGameState))
    {
        World->GetTimerManager().SetTimer(
            BindRetryTimerHandle,
            this,
            &UMD_InspectionResultWidget::TryBindToGameState,
            0.5f,
            false
        );
        return;
    }

    MDGameState->OnInspectionReport.AddDynamic(
        this, &UMD_InspectionResultWidget::HandleInspectionReport);

    // 구독 전에 이미 결과가 복제돼 있으면 즉시 표시 (늦은 접속·위젯 재생성)
    if (MDGameState->GetInspectionReport().bValid)
    {
        HandleInspectionReport(MDGameState->GetInspectionReport());
    }
}

void UMD_InspectionResultWidget::HandleInspectionReport(const FMD_InspectionReport& Report)
{
    if (!IsValid(ResultText))
    {
        return;
    }

    ResultText->SetText(FText::FromString(FString::Printf(
        TEXT("Day%d 점검  %d / %d\n%s"),
        Report.Day,
        Report.Score,
        Report.Threshold,
        *UEnum::GetDisplayValueAsText(Report.Result).ToString())));

    SetVisibility(ESlateVisibility::HitTestInvisible);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            HideTimerHandle,
            this,
            &UMD_InspectionResultWidget::HideResult,
            DisplaySeconds,
            false
        );
    }
}

void UMD_InspectionResultWidget::HideResult()
{
    SetVisibility(ESlateVisibility::Collapsed);
}
