// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Game/MD_ExpeditionStatusWidget.h"

#include "Component/Merchant/MD_MerchantComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Game/MD_GameStateBase.h"

void UMD_ExpeditionStatusWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 위젯 재구성 전에 연결된 원본 월드 조회
    UWorld* PreviousWorld = BoundWorld.Get();
    // 이전 월드에 남은 중복 GameState 알림 제거
    if (IsValid(PreviousWorld) == true)
    {
        // 이번 위젯이 등록했던 알림만 해제
        PreviousWorld->GameStateSetEvent.Remove(GameStateSetHandle);
    }
    // 새 월드에 재사용하지 않을 이전 구독 식별자 정리
    GameStateSetHandle.Reset();
    // 현재 위젯의 GameState 준비 상태를 제공하는 월드 조회
    UWorld* World = GetWorld();
    // 종료 시 같은 월드에서 알림을 해제할 약한 참조 보관
    BoundWorld = World;
    // 유효한 월드에서만 GameState 수명 알림 구독
    if (IsValid(World) == true)
    {
        // 위젯보다 늦게 준비되거나 교체되는 GameState 연결
        GameStateSetHandle = World->GameStateSetEvent.AddUObject(this, &UMD_ExpeditionStatusWidget::HandleGameStateSet);
        // 이미 준비된 GameState의 크레딧을 최초 표시 기준으로 반영
        HandleGameStateSet(World->GetGameState());
    }
    else
    {
        // 월드가 없는 위젯에 남은 이전 원장 연결 정리
        HandleGameStateSet(nullptr);
    }
}

void UMD_ExpeditionStatusWidget::NativeDestruct()
{
    // 현재 조회 월드와 무관하게 실제 구독했던 월드 확인
    UWorld* World = BoundWorld.Get();
    // 제거된 위젯으로 GameState 알림이 전달되는 상황 방지
    if (IsValid(World) == true)
    {
        // 기존 월드의 이번 위젯 구독 해제
        World->GameStateSetEvent.Remove(GameStateSetHandle);
    }
    // 재사용 시 새로운 월드를 연결할 참조 정리
    BoundWorld.Reset();
    // 종료된 월드 구독의 식별자 정리
    GameStateSetHandle.Reset();
    // 크레딧 알림을 실제로 구독했던 복제 컴포넌트 조회
    UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
    // 다른 화면의 구독을 유지하는 이번 위젯 연결 해제
    if (IsValid(MerchantComponent) == true)
    {
        // 제거된 위젯으로 전달되는 크레딧 알림 차단
        MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &UMD_ExpeditionStatusWidget::HandleMerchantStateChanged);
    }
    // 재구성 시 이전 원장을 재사용하지 않는 참조 정리
    BoundMerchantComponent.Reset();
    // 다음 최초 표시에서 다시 설정할 증감 기준 정리
    CachedCredits = 0;

    Super::NativeDestruct();
}

void UMD_ExpeditionStatusWidget::HandleGameStateSet(AGameStateBase* GameState)
{
    // 새 원장 연결 전에 기존 크레딧 구독 대상 조회
    UMD_MerchantComponent* PreviousMerchantComponent = BoundMerchantComponent.Get();
    // 이전 GameState가 현재 점수 표시를 덮어쓰는 상황 방지
    if (IsValid(PreviousMerchantComponent) == true)
    {
        // 교체 전 원장에 남은 이번 위젯의 알림 해제
        PreviousMerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &UMD_ExpeditionStatusWidget::HandleMerchantStateChanged);
    }
    // 팀 공용 크레딧을 가진 현재 GameState 확인
    AMD_GameStateBase* MDGameState = Cast<AMD_GameStateBase>(GameState);
    // GameState가 없는 경우 사용할 빈 크레딧 연결 준비
    UMD_MerchantComponent* MerchantComponent = nullptr;
    // 유효한 GameState에서만 복제 원장 조회 허용
    if (IsValid(MDGameState) == true)
    {
        // 서버 원장의 클라이언트 복제 상태 조회
        MerchantComponent = MDGameState->GetMerchantComponent();
    }
    // 새 크레딧 알림 대상의 수명 추적
    BoundMerchantComponent = MerchantComponent;
    // 원장이 없는 경우 표시할 기본 점수 설정
    CachedCredits = 0;
    // 유효한 크레딧 상태에만 변경 알림 연결
    if (IsValid(MerchantComponent) == true)
    {
        // 동일 위젯의 중복 구독 없는 크레딧 변화 수신
        MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &UMD_ExpeditionStatusWidget::HandleMerchantStateChanged);
        // 전체 보유 크레딧을 획득 Feed로 처리하지 않는 최초 기준 설정
        CachedCredits = MerchantComponent->GetCredits();
    }
    // 최초 연결과 원장 교체 시 Feed 없이 현재 크레딧 표시
    SetTextPair(ScoreText, ScoreText_Shadow, FText::AsNumber(CachedCredits));
}

void UMD_ExpeditionStatusWidget::HandleMerchantStateChanged()
{
    // 현재 크레딧 변화를 전달한 복제 상태 조회
    UMD_MerchantComponent* MerchantComponent = BoundMerchantComponent.Get();
    // 종료되거나 교체된 원장 접근 차단
    if (IsValid(MerchantComponent) == false)
    {
        // 유효한 크레딧 상태가 없는 갱신 종료
        return;
    }
    // 서버에서 확정된 현재 보유 크레딧 조회
    int32 CurrentCredits = MerchantComponent->GetCredits();
    // 부채와 구매 목록 알림에 의한 중복 Feed 방지
    if (CurrentCredits == CachedCredits)
    {
        // 실제 점수 변화가 없는 알림 종료
        return;
    }
    // 이전 표시 이후 실제 반영된 크레딧 증감 계산
    int32 Delta = CurrentCredits - CachedCredits;
    // Feed 처리 중 새 변화가 발생해도 중복되지 않을 기준 갱신
    CachedCredits = CurrentCredits;
    // Feed와 같은 크레딧 소스를 사용하는 총액 반영
    SetTextPair(ScoreText, ScoreText_Shadow, FText::AsNumber(CurrentCredits));
    // 기존 Blueprint의 순차 표시 대기열로 실제 증감 전달
    OnCreditDelta(Delta);
}

void UMD_ExpeditionStatusWidget::SetTextPair(UTextBlock* TextWidget, UTextBlock* ShadowWidget, FText Text)
{
    // 현재 레이아웃에 연결된 본문 표시 대상 확인
    if (IsValid(TextWidget) == true)
    {
        // 점수 본문에 현재 크레딧 반영
        TextWidget->SetText(Text);
    }

    // 현재 레이아웃에 연결된 그림자 표시 대상 확인
    if (IsValid(ShadowWidget) == true)
    {
        // 본문과 동일한 크레딧의 그림자 표시
        ShadowWidget->SetText(Text);
    }
}
