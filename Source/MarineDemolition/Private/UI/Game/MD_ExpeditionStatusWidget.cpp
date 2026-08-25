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

    // 현재 일차에 난이도와 인원 및 경고 배율이 반영된 목표 점수를 계산한다.
    int32 CurrentDay = MDGameState->GetCurrentDay();
    int32 Threshold = MDGameState->GetInspectionThreshold(CurrentDay);

    // 현재 일차의 목표 점수를 본문과 그림자에 동일하게 표시한다.
    FText ThresholdText = Threshold == 0 ? FText::FromString(TEXT("-")) : FText::AsNumber(Threshold);
    SetTextPair(GoalText, GoalText_Shadow, ThresholdText);

    if (IsValid(ScoreProgress) == true)
    {
        // 정수 나눗셈으로 목표 미달 진행률이 0이 되지 않도록 실수 비율을 계산한다.
        float ScorePercent = Threshold > 0 ? static_cast<float>(CurrentScore) / Threshold : 0;
        // 음수 점수와 목표 초과 점수를 진행 막대의 유효 범위로 보정한다.
        ScorePercent = FMath::Clamp(ScorePercent, 0.0f, 1.0f);
        ScoreProgress->SetPercent(ScorePercent);
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
