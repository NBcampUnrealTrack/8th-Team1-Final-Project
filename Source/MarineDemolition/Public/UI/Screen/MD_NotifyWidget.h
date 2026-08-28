
#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "MD_NotifyWidget.generated.h"

class UMD_CommonButtonBase;
class UTextBlock;

// 알림 화면에 표시할 버튼 조합을 정의한다.
UENUM(BlueprintType)
enum class EMD_NotifyButtonType : uint8
{
	// 취소와 확인 버튼을 모두 표시한다.
	ConfirmAndCancel UMETA(DisplayName = "취소 및 확인"),

	// 확인 버튼만 표시한다.
	ConfirmOnly UMETA(DisplayName = "확인만"),

	// 취소 버튼만 표시한다.
	CancelOnly UMETA(DisplayName = "취소만")
};

// 알림 화면의 취소 입력을 외부에 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_NotifyCancelRequested);

// 알림 화면의 확인 입력을 외부에 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMD_NotifyConfirmRequested);

// 공통 알림 화면의 콘텐츠와 버튼 상태를 관리한다.
UCLASS(Abstract, Blueprintable)
class MARINEDEMOLITION_API UMD_NotifyWidget : public UMD_CommonGameMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void NativePreConstruct() override;

	// 알림 화면이 활성화될 때 버튼 입력을 다시 연결한다.
	virtual void NativeOnActivated() override;

	virtual void NativeOnDeactivated() override;

public:
	// 취소 버튼 입력을 외부 소유자에게 전달한다.
	UPROPERTY(BlueprintAssignable, Category = "MD|Notify")
	FMD_NotifyCancelRequested OnCancelRequested;

	// 확인 버튼 입력을 외부 소유자에게 전달한다.
	UPROPERTY(BlueprintAssignable, Category = "MD|Notify")
	FMD_NotifyConfirmRequested OnConfirmRequested;

protected:
	// 알림 화면에 표시할 제목 기본값을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Notify")
	FText NotifyTitle = NSLOCTEXT("MDNotify", "DefaultTitle", "알림 타이틀");

	// 알림 화면에 표시할 설명 기본값을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Notify")
	FText NotifyDescription = NSLOCTEXT("MDNotify", "DefaultDescription", "알림 내용");

	// 알림 화면에 남길 버튼 조합을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Notify")
	EMD_NotifyButtonType ButtonType = EMD_NotifyButtonType::ConfirmAndCancel;

	// 알림 제목을 표시하는 WBP 텍스트 영역을 바인딩한다.
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Title;

	// 알림 설명을 표시하는 WBP 텍스트 영역을 바인딩한다.
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Description;

	// 취소 입력을 받는 WBP 버튼을 바인딩한다.
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Cancel;

	// 확인 입력을 받는 WBP 버튼을 바인딩한다.
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UMD_CommonButtonBase> Btn_Confirm;

public:
	// 제목과 설명, 버튼 조합을 한 번에 갱신한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Notify")
	void SetNotifyContent(FText InTitle, FText InDescription, EMD_NotifyButtonType InButtonType);

	// 알림 제목을 갱신한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Notify")
	void SetNotifyTitle(FText InTitle);

	// 알림 설명을 갱신한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Notify")
	void SetNotifyDescription(FText InDescription);

	// 표시할 버튼 조합을 갱신한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Notify")
	void SetButtonType(EMD_NotifyButtonType InButtonType);

	// Blueprint와 취소 버튼에서 알림 취소 처리를 실행한다.
	UFUNCTION(BlueprintCallable, Category = "MD|Notify")
	void Cancel();

protected:
	// 선택한 버튼 조합에 따라 각 버튼 표시 상태를 갱신한다.
	void ApplyButtonType();

	// 확인 버튼 입력을 공통 확인 요청으로 변환한다.
	UFUNCTION()
	void HandleConfirmClicked();
};
