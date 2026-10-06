// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_ExpeditionStatusWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Game/MD_GameStateBase.h"

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
    // 현재 플레이 세션의 점수와 일차 정보를 가진 GameState를 조회한다.
    UWorld* World = GetWorld();
    AMD_GameStateBase* MDGameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
    if (IsValid(MDGameState) == false)
    {
        return;
    }

    // 누적 평가 점수를 새 위젯의 본문과 그림자에 동일하게 표시한다.
    int32 CurrentScore = MDGameState->GetScore();
    FText CurrentScoreText = FText::AsNumber(CurrentScore);
    SetTextPair(ScoreText, ScoreText_Shadow, CurrentScoreText);

    // 목표 점수(점검 기준)는 폐지됨. 정리 달성률 표시는 분모 확정 후 연결한다.
    SetTextPair(GoalText, GoalText_Shadow, FText::FromString(TEXT("-")));
    if (IsValid(ScoreProgress) == true)
    {
        ScoreProgress->SetPercent(0.0f);
    }
}

void UMD_ExpeditionStatusWidget::SetTextPair(UTextBlock* TextWidget, UTextBlock* ShadowWidget, FText Text)
{
    // 앞쪽 본문 TextBlock에 표시 내용을 적용한다.
    if (IsValid(TextWidget) == true)
    {
        TextWidget->SetText(Text);
    }

    // 뒤쪽 그림자 TextBlock에도 같은 표시 내용을 적용한다.
    if (IsValid(ShadowWidget) == true)
    {
        ShadowWidget->SetText(Text);
    }
}
