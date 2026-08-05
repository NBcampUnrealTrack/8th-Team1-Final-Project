// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Score/MD_ScoreTypes.h"
#include "MD_InspectionResultWidget.generated.h"

class UTextBlock;

// Day 점검 결과 통지. GameState의 OnInspectionReport를 받아 잠시 표시 후 숨김
UCLASS()
class MARINEDEMOLITION_API UMD_InspectionResultWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // 결과 표시 유지 시간(초)
    UPROPERTY(EditDefaultsOnly, Category = "Inspection")
    float DisplaySeconds = 8.f;

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> ResultText;

    FTimerHandle BindRetryTimerHandle;

    FTimerHandle HideTimerHandle;

    // GameState가 아직 없을 수 있어 생길 때까지 재시도 후 구독
    void TryBindToGameState();

    UFUNCTION()
    void HandleInspectionReport(const FMD_InspectionReport& Report);

    void HideResult();
};
