// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MD_CommonActivatableWidget.h"
#include "MD_CommonGameMenuWidget.generated.h"

class UCommonActivatableWidgetSwitcher;
class UMD_AsyncAction_PushScreenToLayer;
class UMD_NotifyWidget;

// 호출부에서 지정한 알림 버튼의 후속 동작 전달
DECLARE_DYNAMIC_DELEGATE(FMD_SettingsNotifyAction);

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_CommonGameMenuWidget : public UMD_CommonActivatableWidget
{
	GENERATED_BODY()
	
public:
	virtual void NativeOnActivated() override;

protected:
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

protected:
	// 현재 선택된 설정 페이지를 제공하는 선택적 위젯 연결
	UPROPERTY(BlueprintReadOnly, Category = "MD|Settings", meta = (BindWidgetOptional))
	TObjectPtr<UCommonActivatableWidgetSwitcher> WS_Settings;

	// 로드 완료 또는 취소까지 유지할 알림 생성 요청
	UPROPERTY(Transient)
	TObjectPtr<UMD_AsyncAction_PushScreenToLayer> PendingSettingsNotifyRequest;

	// 응답과 닫기 애니메이션 완료를 추적할 알림 인스턴스
	UPROPERTY(Transient)
	TObjectPtr<UMD_NotifyWidget> PendingSettingsNotifyWidget;

	// 확인 버튼 응답 시 실행할 호출부 동작
	UPROPERTY(Transient)
	FMD_SettingsNotifyAction PendingSettingsConfirmAction;

	// 취소 버튼 또는 알림 뒤로가기 시 실행할 호출부 동작
	UPROPERTY(Transient)
	FMD_SettingsNotifyAction PendingSettingsCancelAction;

	// 알림 외부 닫힘 구독을 개별 해제하기 위한 핸들
	FDelegateHandle PendingSettingsNotifyClosedHandle;

	// 로드 시작부터 알림 닫힘까지 중복 요청을 제한할 상태
	bool bPendingSettingsNotifyActive = false;

	// 닫기 애니메이션 중 반복 응답 전달을 차단할 상태
	bool bPendingSettingsNotifyResponded = false;

public:
	// 현재 선택된 설정 카테고리에 남은 미적용 변경 여부 반환
	UFUNCTION(BlueprintCallable, Category = "MD|Settings")
	bool HasPendingSettingsChanges();

	// 변경사항 알림 표시와 호출별 확인 및 취소 동작 연결
	UFUNCTION(BlueprintCallable, Category = "MD|Settings")
	void ShowPendingSettingsNotify(FMD_SettingsNotifyAction ConfirmAction, FMD_SettingsNotifyAction CancelAction);

	// 알림 요청 또는 닫기 처리 중인 상태의 직접 반환
	UFUNCTION(BlueprintPure, Category = "MD|Settings")
	FORCEINLINE bool IsPendingSettingsNotifyActive() { return bPendingSettingsNotifyActive; }

protected:
	// 알림 활성화 전에 내용과 버튼 응답의 초기화
	UFUNCTION()
	void HandlePendingSettingsNotifyBeforePush(UCommonActivatableWidget* Screen);

	// 생성 완료 후 비동기 요청 참조의 해제
	UFUNCTION()
	void HandlePendingSettingsNotifyAfterPush(UCommonActivatableWidget* Screen);

	// 표시 실패 시 사용자 선택 없이 요청 상태의 정리
	UFUNCTION()
	void HandlePendingSettingsNotifyFailed();

	// 로드 취소 시 사용자 취소 동작과 분리된 정리
	UFUNCTION()
	void HandlePendingSettingsNotifyCanceled(FName CancellationKey);

	// 확인 입력의 단일 응답 전달
	UFUNCTION()
	void HandlePendingSettingsNotifyConfirmed();

	// 취소 입력의 단일 응답 전달
	UFUNCTION()
	void HandlePendingSettingsNotifyDeclined();

	// 닫기 애니메이션 완료 또는 외부 닫힘 후 상태의 정리
	void HandlePendingSettingsNotifyClosed(UMD_NotifyWidget* ClosedWidget);

	// 호출부 동작 실행 전 중복 응답과 재진입의 차단
	void ExecutePendingSettingsNotifyAction(bool bConfirmed);

	// 비동기 구독 해제와 미완료 로드의 취소
	void ReleasePendingSettingsNotifyRequest();

	// 소유 화면 종료 또는 알림 종료에 따른 요청과 구독의 정리
	void ResetPendingSettingsNotify(bool bCloseNotify);
};
