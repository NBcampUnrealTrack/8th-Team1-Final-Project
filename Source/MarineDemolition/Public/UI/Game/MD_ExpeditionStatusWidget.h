// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ExpeditionStatusWidget.generated.h"

class UTextBlock;

// HUD 원정 상태 패널. 누적 평가 점수와 오늘 점검 기준을 표시
UCLASS()
class MARINEDEMOLITION_API UMD_ExpeditionStatusWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> ScoreText;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> GoalText;

    // 경고 상태 표시용(아이콘·텍스트 등 아무 위젯). WBP에 없어도 무방
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> WarningIndicator;

    FTimerHandle UpdateStatusTimerHandle;

    void UpdateStatusText();
};
