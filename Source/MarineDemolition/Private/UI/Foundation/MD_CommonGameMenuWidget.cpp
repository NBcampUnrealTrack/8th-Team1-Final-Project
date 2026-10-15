// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Foundation/MD_CommonGameMenuWidget.h"

#include "CommonActivatableWidgetSwitcher.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Settings/MD_AudioSettingCategory.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_GraphicsSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "UI/Foundation/MD_AsyncAction_PushScreenToLayer.h"
#include "UI/Screen/MD_NotifyWidget.h"
#include "UI/Screen/Settings/MD_SettingsAudioWidget.h"
#include "UI/Screen/Settings/MD_SettingsControlWidget.h"
#include "UI/Screen/Settings/MD_SettingsGraphicsWidget.h"
#include "UObject/UnrealType.h"

void UMD_CommonGameMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 입력 모드를 변경할 로컬 플레이어 컨트롤러를 가져온다.
	APlayerController* OwningPlayerController = GetOwningPlayer();
	if (IsValid(OwningPlayerController) == true)
	{
		// 게임 입력을 차단하고 메뉴 UI만 입력을 받도록 설정한다.
		FInputModeUIOnly InputMode = FInputModeUIOnly();
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		OwningPlayerController->SetInputMode(InputMode);

		OwningPlayerController->SetShowMouseCursor(true);
	}
}

void UMD_CommonGameMenuWidget::NativeOnDeactivated()
{
	// 소유 설정 화면 종료 후 도착하는 알림 응답과 로드 완료의 차단
	ResetPendingSettingsNotify(true);
	Super::NativeOnDeactivated();
}

void UMD_CommonGameMenuWidget::NativeDestruct()
{
	// 화면 제거 시 남아 있는 알림 요청과 버튼 구독의 해제
	ResetPendingSettingsNotify(true);
	Super::NativeDestruct();
}

bool UMD_CommonGameMenuWidget::HasPendingSettingsChanges()
{
	// 현재 메뉴가 표시하는 설정의 기준값 조회
	UMD_SettingsManager* SettingsManager = UMD_SettingsManager::Get(this);
	// 미리보기나 종료 중인 화면의 잘못된 변경 판정 방지
	if (IsValid(SettingsManager) == false || IsValid(WS_Settings) == false)
	{
		// 비교할 설정 또는 화면이 없는 상태의 변경 없음 반환
		return false;
	}

	// 구조체의 모든 설정 필드를 포함하는 부작용 없는 비교
	auto HaveSettingsChanged = [](UScriptStruct* SettingsType, void* CurrentSettings, void* EditedSettings)
	{
		// 키 배열과 장치 ID를 포함한 전체 설정 속성 순회
		for (TFieldIterator<FProperty> PropertyIterator(SettingsType); PropertyIterator; ++PropertyIterator)
		{
			// 현재 비교 대상 속성의 타입 정보 조회
			FProperty* SettingProperty = *PropertyIterator;
			// 슬라이더 정규화 왕복 오차를 허용할 실수 속성 확인
			FFloatProperty* FloatProperty = CastField<FFloatProperty>(SettingProperty);
			// 실수 설정에 한정한 근사값 비교
			if (FloatProperty != nullptr)
			{
				// 화면 표시 변환 오차를 제외한 실제 실수값 변경 확인
				if (FMath::IsNearlyEqual(FloatProperty->GetPropertyValue_InContainer(CurrentSettings),
					FloatProperty->GetPropertyValue_InContainer(EditedSettings), UE_KINDA_SMALL_NUMBER) == false)
				{
					// 실수 설정의 미적용 변경 반환
					return true;
				}
			}
			// 키와 배열 및 나머지 설정 타입의 값 비교
			else if (SettingProperty->Identical_InContainer(CurrentSettings, EditedSettings) == false)
			{
				// 현재 속성의 미적용 변경 반환
				return true;
			}
		}
		// 모든 설정 필드가 같은 상태의 변경 없음 반환
		return false;
	};

	// 카테고리 선택 매크로와 같은 활성 페이지 기준 조회
	UWidget* ActiveSettingsWidget = WS_Settings->GetActiveWidget();
	// 선택된 그래픽 탭의 실제 컨트롤 선택값 검사
	if (UMD_SettingsGraphicsWidget* GraphicsWidget = Cast<UMD_SettingsGraphicsWidget>(ActiveSettingsWidget); IsValid(GraphicsWidget) == true)
	{
		// 그래픽 편집값의 적용 및 되돌리기 기준 조회
		UMD_GraphicsSettingCategory* GraphicsCategory = SettingsManager->GetGraphicsSettings();
		// 초기화된 카테고리에 한정한 비교
		if (IsValid(GraphicsCategory) == true)
		{
			// 실제 적용 없이 현재 그래픽 선택값 수집
			FMD_GraphicsSettings EditedSettings = GraphicsWidget->GetPendingSettings();
			// 선택된 그래픽 카테고리의 미적용 변경 여부 반환
			return HaveSettingsChanged(FMD_GraphicsSettings::StaticStruct(), &GraphicsCategory->Settings, &EditedSettings);
		}
	}
	// 선택된 오디오 탭의 장치 선택과 음량 및 입력 모드 검사
	else if (UMD_SettingsAudioWidget* AudioWidget = Cast<UMD_SettingsAudioWidget>(ActiveSettingsWidget); IsValid(AudioWidget) == true)
	{
		// 오디오 편집값의 적용 및 되돌리기 기준 조회
		UMD_AudioSettingCategory* AudioCategory = SettingsManager->GetAudioSettings();
		// 초기화된 카테고리에 한정한 비교
		if (IsValid(AudioCategory) == true)
		{
			// 장치 전환 없이 현재 오디오 선택값 수집
			FMD_AudioUserSettings EditedSettings = AudioWidget->GetPendingSettings();
			// 선택된 오디오 카테고리의 미적용 변경 여부 반환
			return HaveSettingsChanged(FMD_AudioUserSettings::StaticStruct(), &AudioCategory->Settings, &EditedSettings);
		}
	}
	// 선택된 조작 탭의 키와 감도 및 시점 반전 검사
	else if (UMD_SettingsControlWidget* ControlWidget = Cast<UMD_SettingsControlWidget>(ActiveSettingsWidget); IsValid(ControlWidget) == true)
	{
		// 조작 편집값의 적용 및 되돌리기 기준 조회
		UMD_ControlSettingCategory* ControlCategory = SettingsManager->GetControlSettings();
		// 초기화된 카테고리에 한정한 비교
		if (IsValid(ControlCategory) == true)
		{
			// 입력 매핑 변경 없이 현재 조작 선택값 수집
			FMD_ControlSettings EditedSettings = ControlWidget->GetPendingSettings();
			// 선택된 조작 카테고리의 미적용 변경 여부 반환
			return HaveSettingsChanged(FMD_ControlSettings::StaticStruct(), &ControlCategory->Settings, &EditedSettings);
		}
	}
	// 선택된 설정 페이지 또는 비교 기준이 없는 상태의 변경 없음 반환
	return false;
}

void UMD_CommonGameMenuWidget::ShowPendingSettingsNotify(FMD_SettingsNotifyAction ConfirmAction, FMD_SettingsNotifyAction CancelAction)
{
	// 닫힌 소유 화면과 이미 진행 중인 요청의 콜백 덮어쓰기 방지
	if (IsActivated() == false || bPendingSettingsNotifyActive == true)
	{
		// 기존 알림과 호출부 상태의 보존
		return;
	}
	// 현재 화면에 대응하는 로컬 UI 소유자 조회
	APlayerController* OwningPlayer = GetOwningPlayer();
	// 소유자가 없는 미리보기 화면의 비동기 요청 차단
	if (IsValid(OwningPlayer) == false)
	{
		// 유효한 UI 소유자 없는 요청의 종료
		return;
	}
	// 즉시 발생할 수 있는 실패 콜백에 앞선 요청 상태 확정
	bPendingSettingsNotifyActive = true;
	// 새 요청의 응답 전달 상태 초기화
	bPendingSettingsNotifyResponded = false;
	// 확인 입력에 연결할 호출부 동작의 수명 보관
	PendingSettingsConfirmAction = ConfirmAction;
	// 취소 입력에 연결할 호출부 동작의 수명 보관
	PendingSettingsCancelAction = CancelAction;
	// 기존 태그 등록과 비동기 화면 로드 경로의 재사용
	PendingSettingsNotifyRequest = UMD_AsyncAction_PushScreenToLayer::PushScreenToLayerAutoAsync(
		OwningPlayer, FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Layer.GameMenu.Notify")), false), true);
	// 생성 실패 시 사용자 응답 없는 요청 상태 복구
	if (IsValid(PendingSettingsNotifyRequest) == false)
	{
		// 확인 및 취소 동작을 실행하지 않는 실패 정리
		ResetPendingSettingsNotify(false);
		// 생성되지 않은 요청의 실행 차단
		return;
	}
	// 화면 활성화 이전의 알림 초기화 연결
	PendingSettingsNotifyRequest->BeforePush.AddDynamic(this, &ThisClass::HandlePendingSettingsNotifyBeforePush);
	// 성공 시 완료된 비동기 요청의 참조 해제 연결
	PendingSettingsNotifyRequest->AfterPush.AddDynamic(this, &ThisClass::HandlePendingSettingsNotifyAfterPush);
	// 표시 실패 시 사용자 동작과 분리된 정리 연결
	PendingSettingsNotifyRequest->Failed.AddDynamic(this, &ThisClass::HandlePendingSettingsNotifyFailed);
	// 로드 취소 시 사용자 동작과 분리된 정리 연결
	PendingSettingsNotifyRequest->Canceled.AddDynamic(this, &ThisClass::HandlePendingSettingsNotifyCanceled);
	// 모든 응답 연결 이후 비동기 화면 표시 요청의 시작
	PendingSettingsNotifyRequest->Activate();
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyBeforePush(UCommonActivatableWidget* Screen)
{
	// 태그에 등록된 화면의 알림 인터페이스 확인
	UMD_NotifyWidget* NotifyWidget = Cast<UMD_NotifyWidget>(Screen);
	// 다른 화면 타입의 잘못된 표시와 오래된 요청의 차단
	if (IsValid(NotifyWidget) == false || bPendingSettingsNotifyActive == false || IsActivated() == false)
	{
		// 비동기 요청 취소를 통한 생성 중 화면의 정리
		ResetPendingSettingsNotify(true);
		// 알림 초기화의 중단
		return;
	}
	// 닫힘 완료까지 응답 대상을 유지할 알림 참조 보관
	PendingSettingsNotifyWidget = NotifyWidget;
	// 기존 설정 변경 알림의 제목과 설명 반영
	NotifyWidget->SetNotifyContent(NSLOCTEXT("MDSettings", "PendingSettingsTitle", "설정이 저장되지 않았습니다."),
		NSLOCTEXT("MDSettings", "PendingSettingsDescription", "변경 사항을 저장하시겠습니까?"));
	// 풀에서 재사용된 알림에도 확인과 취소 버튼을 모두 표시
	NotifyWidget->SetButtonType(static_cast<int32>(EMD_NotifyButtonType::Confirm | EMD_NotifyButtonType::Cancel));
	// 확인 입력을 호출부 동작에 전달할 단일 응답 경로 연결
	NotifyWidget->OnConfirmRequested.AddUniqueDynamic(this, &ThisClass::HandlePendingSettingsNotifyConfirmed);
	// 취소 버튼과 알림 뒤로가기의 공통 응답 경로 연결
	NotifyWidget->OnCancelRequested.AddUniqueDynamic(this, &ThisClass::HandlePendingSettingsNotifyDeclined);
	// 애니메이션 완료와 외부 닫힘에 공통으로 적용할 상태 정리 연결
	PendingSettingsNotifyClosedHandle = NotifyWidget->OnDeactivated().AddUObject(this, &ThisClass::HandlePendingSettingsNotifyClosed, NotifyWidget);
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyAfterPush(UCommonActivatableWidget* Screen)
{
	// 활성 알림의 응답 대기 상태를 유지하며 완료된 로드 참조만 해제
	ReleasePendingSettingsNotifyRequest();
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyFailed()
{
	// 로드 실패를 사용자 취소 선택과 구분한 요청 정리
	ResetPendingSettingsNotify(true);
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyCanceled(FName CancellationKey)
{
	// 로드 취소를 사용자 취소 선택과 구분한 요청 정리
	ResetPendingSettingsNotify(true);
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyConfirmed()
{
	// 확인 버튼에 지정한 호출부 동작의 단일 실행
	ExecutePendingSettingsNotifyAction(true);
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyDeclined()
{
	// 취소 버튼에 지정한 호출부 동작의 단일 실행
	ExecutePendingSettingsNotifyAction(false);
}

void UMD_CommonGameMenuWidget::HandlePendingSettingsNotifyClosed(UMD_NotifyWidget* ClosedWidget)
{
	// 이전 알림의 닫힘 콜백이 새 요청을 정리하지 않도록 대상 확인
	if (PendingSettingsNotifyWidget != ClosedWidget)
	{
		// 재진입으로 교체된 요청 상태의 보존
		return;
	}
	// 이미 닫힌 알림의 반복 비활성화 없는 요청 정리
	ResetPendingSettingsNotify(false);
}

void UMD_CommonGameMenuWidget::ExecutePendingSettingsNotifyAction(bool bConfirmed)
{
	// 이미 처리되었거나 종료된 알림 응답의 무시
	if (bPendingSettingsNotifyActive == false || bPendingSettingsNotifyResponded == true)
	{
		// 닫기 애니메이션 중 반복 입력의 차단
		return;
	}
	// 호출부에서 화면을 닫거나 다시 요청하기 전 응답 완료 확정
	bPendingSettingsNotifyResponded = true;
	// 호출부 실행 중 정리되어도 유지할 선택된 동작의 복사
	FMD_SettingsNotifyAction SelectedAction = bConfirmed == true ? PendingSettingsConfirmAction : PendingSettingsCancelAction;
	// 처리한 확인 동작의 보관 참조 해제
	PendingSettingsConfirmAction.Unbind();
	// 처리한 취소 동작의 보관 참조 해제
	PendingSettingsCancelAction.Unbind();
	// 알림이 유지되는 동안 추가 버튼 구독의 차단
	if (IsValid(PendingSettingsNotifyWidget) == true)
	{
		// 확인 버튼의 반복 응답 구독 해제
		PendingSettingsNotifyWidget->OnConfirmRequested.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyConfirmed);
		// 취소 버튼의 반복 응답 구독 해제
		PendingSettingsNotifyWidget->OnCancelRequested.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyDeclined);
	}
	// Blueprint에서 지정한 저장 및 이동 등의 후속 동작 실행
	SelectedAction.ExecuteIfBound();
}

void UMD_CommonGameMenuWidget::ReleasePendingSettingsNotifyRequest()
{
	// 취소 콜백 재진입과 분리할 기존 요청의 보관
	UMD_AsyncAction_PushScreenToLayer* Request = PendingSettingsNotifyRequest;
	// 이후 콜백에서 완료된 요청을 다시 사용하지 않도록 참조 해제
	PendingSettingsNotifyRequest = nullptr;
	// 현재 요청이 남아 있는 경우에 한정한 구독 정리
	if (IsValid(Request) == true)
	{
		// 활성화 전 초기화 구독 해제
		Request->BeforePush.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyBeforePush);
		// 성공 콜백 구독 해제
		Request->AfterPush.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyAfterPush);
		// 실패 콜백 구독 해제
		Request->Failed.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyFailed);
		// 취소 콜백 구독 해제
		Request->Canceled.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyCanceled);
		// 미완료 요청에 한정한 로드와 입력 차단의 해제
		Request->Cancel();
	}
}

void UMD_CommonGameMenuWidget::ResetPendingSettingsNotify(bool bCloseNotify)
{
	// 닫힘 재진입과 분리할 기존 알림 참조 보관
	UMD_NotifyWidget* NotifyWidget = PendingSettingsNotifyWidget;
	// 외부 닫힘 구독의 개별 해제를 위한 기존 핸들 보관
	FDelegateHandle ClosedHandle = PendingSettingsNotifyClosedHandle;
	// 비활성화 중 기존 알림 참조의 재사용 차단
	PendingSettingsNotifyWidget = nullptr;
	// 종료된 알림의 구독 핸들 초기화
	PendingSettingsNotifyClosedHandle.Reset();
	// 새 요청을 받을 수 있도록 진행 상태 초기화
	bPendingSettingsNotifyActive = false;
	// 종료된 요청의 응답 상태 초기화
	bPendingSettingsNotifyResponded = false;
	// 소유 화면 종료 후 확인 동작 실행의 차단
	PendingSettingsConfirmAction.Unbind();
	// 소유 화면 종료 후 취소 동작 실행의 차단
	PendingSettingsCancelAction.Unbind();
	// 비동기 로드와 입력 차단 및 요청 구독의 정리
	ReleasePendingSettingsNotifyRequest();
	// 현재 알림이 유지되는 경우에 한정한 구독 정리
	if (IsValid(NotifyWidget) == true)
	{
		// 종료된 요청의 확인 구독 해제
		NotifyWidget->OnConfirmRequested.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyConfirmed);
		// 종료된 요청의 취소 구독 해제
		NotifyWidget->OnCancelRequested.RemoveDynamic(this, &ThisClass::HandlePendingSettingsNotifyDeclined);
		// 외부 닫힘 처리 중 반복 진입을 방지할 구독 해제
		NotifyWidget->OnDeactivated().Remove(ClosedHandle);
		// 소유 화면 종료 시 남은 알림의 표준 닫기 처리
		if (bCloseNotify == true && NotifyWidget->IsActivated() == true)
		{
			// 닫는 소유 화면의 알림을 함께 비활성화
			NotifyWidget->DeactivateWidget();
		}
	}
}
