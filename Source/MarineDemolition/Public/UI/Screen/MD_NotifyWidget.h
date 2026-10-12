
#pragma once

#include "CoreMinimal.h"
#include "UI/Foundation/MD_CommonGameMenuWidget.h"
#include "MD_NotifyWidget.generated.h"

class UButton;
class UTextBlock;

// 알림 화면에서 독립적으로 선택 가능한 버튼 플래그
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EMD_NotifyButtonType : uint8
{
	// 모든 버튼을 숨기는 빈 선택 상태
	None = 0 UMETA(Hidden),

	// 확인 버튼 표시 여부를 제어하는 플래그
	Confirm = 1 << 0 UMETA(DisplayName = "확인"),

	// 취소 버튼 표시 여부를 제어하는 플래그
	Cancel = 1 << 1 UMETA(DisplayName = "취소")
};

ENUM_CLASS_FLAGS(EMD_NotifyButtonType);

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

	virtual void NativeOnDeactivated() override;

public:
	// 취소 버튼 입력을 외부 소유자에게 전달한다.
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "MD|Notify")
	FMD_NotifyCancelRequested OnCancelRequested;

	// 확인 버튼 입력을 외부 소유자에게 전달한다.
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "MD|Notify")
	FMD_NotifyConfirmRequested OnConfirmRequested;

protected:
	// 알림 화면에 표시할 제목 기본값을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNotifyTitle, Category = "Appearance")
	FText NotifyTitle = NSLOCTEXT("MDNotify", "DefaultTitle", "알림 타이틀");

	// 알림 화면에 표시할 설명 기본값을 저장한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNotifyDescription, Category = "Appearance")
	FText NotifyDescription = NSLOCTEXT("MDNotify", "DefaultDescription", "알림 내용");

	// 디자이너에서 중복 선택 가능한 버튼 표시 플래그와 기본 양쪽 표시 상태
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetButtonType, Category = "Appearance", meta = (Bitmask, BitmaskEnum = "/Script/MarineDemolition.EMD_NotifyButtonType"))
	int32 ButtonType = static_cast<int32>(EMD_NotifyButtonType::Confirm | EMD_NotifyButtonType::Cancel);

	// 알림 제목을 표시하는 WBP 텍스트 영역을 바인딩한다.
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Title;

	// 알림 설명을 표시하는 WBP 텍스트 영역을 바인딩한다.
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Description;

	// 취소 입력을 받는 기본 UMG 버튼 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Cancel;

	// 확인 입력을 받는 기본 UMG 버튼 바인딩
	UPROPERTY(BlueprintReadOnly, Category = "MD|Notify", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Confirm;

public:
	// 제목과 설명 및 선택된 버튼 플래그의 일괄 반영
	UFUNCTION(BlueprintCallable, Category = "MD|Notify")
	void SetNotifyContent(FText InTitle, FText InDescription);

	// 알림 제목을 갱신한다.
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "MD|Notify")
	void SetNotifyTitle(FText InTitle);

	// 알림 설명을 갱신한다.
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "MD|Notify")
	void SetNotifyDescription(FText InDescription);

	// Blueprint의 비트마스크 입력에 따른 버튼 표시 플래그 갱신
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "MD|Notify")
	void SetButtonType(UPARAM(meta = (Bitmask, BitmaskEnum = "/Script/MarineDemolition.EMD_NotifyButtonType")) int32 InButtonType);

protected:
	// 개별 버튼 플래그에 따른 표시 및 레이아웃 제외 상태 반영
	void ApplyButtonType();
};
