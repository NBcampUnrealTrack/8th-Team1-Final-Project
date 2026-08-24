// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ExpeditionStatusWidget.generated.h"

class UTextBlock;
class UProgressBar;

// HUD 원정 상태 패널. 누적 평가 점수와 오늘 점검 기준을 표시
UCLASS()
class MARINEDEMOLITION_API UMD_ExpeditionStatusWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    virtual void NativeDestruct() override;

private:
    // 현재 누적 평가 점수를 표시한다.
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> ScoreText;

    // 누적 평가 점수 글자의 외곽 그림자를 표시한다.
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> ScoreText_Shadow;

    // 현재 일차의 목표 점수를 표시한다.
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> GoalText;

    // 목표 점수 글자의 외곽 그림자를 표시한다.
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> GoalText_Shadow;

    // 목표 점수 대비 현재 누적 점수의 달성률을 표시한다.
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> ScoreProgress;

    // 원정 상태 표시를 주기적으로 갱신하는 타이머를 보관한다.
    FTimerHandle UpdateStatusTimerHandle;

    // 현재 GameState의 누적 점수와 일차별 목표 점수를 UI에 반영한다.
    void UpdateStatusText();

    // 본문과 그림자 TextBlock에 같은 내용을 적용한다.
    void SetTextPair(UTextBlock* TextWidget, UTextBlock* ShadowWidget, FText Text);
};
