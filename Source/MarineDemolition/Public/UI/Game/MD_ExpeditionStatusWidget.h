// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MD_ExpeditionStatusWidget.generated.h"

class AGameStateBase;
class UMD_MerchantComponent;
class UTextBlock;
class UWorld;

// HUD 원정 상태 패널의 크레딧 기반 점수와 증감 알림
UCLASS()
class MARINEDEMOLITION_API UMD_ExpeditionStatusWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    virtual void NativeDestruct() override;

protected:
    // 점수 표시 레이아웃에 연결하는 현재 크레딧 본문
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ScoreText;

    // 본문과 동일한 현재 크레딧을 표시하는 그림자
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> ScoreText_Shadow;

    // 위젯 월드가 변경되어도 기존 구독을 해제할 원본 월드
    TWeakObjectPtr<UWorld> BoundWorld;

    // 서버와 클라이언트가 함께 조회하는 크레딧 복제 상태
    TWeakObjectPtr<UMD_MerchantComponent> BoundMerchantComponent;

    // GameState 준비와 교체 알림의 해제 식별자
    FDelegateHandle GameStateSetHandle;

    // 최초 표시를 증감에서 제외하고 중복 알림을 거르는 직전 크레딧
    int32 CachedCredits = 0;

protected:
    // 새 GameState의 크레딧 구독과 최초 표시 기준 설정
    void HandleGameStateSet(AGameStateBase* GameState);

    // 실제 크레딧 변화의 본문 반영과 Feed 전달
    UFUNCTION()
    void HandleMerchantStateChanged();

    // Blueprint Feed 대기열에 전달할 실제 크레딧 증감
    UFUNCTION(BlueprintImplementableEvent, Category = "MD|HUD")
    void OnCreditDelta(int32 Delta);

    // 본문과 그림자에 동일한 현재 크레딧 표시
    void SetTextPair(UTextBlock* TextWidget, UTextBlock* ShadowWidget, FText Text);
};
