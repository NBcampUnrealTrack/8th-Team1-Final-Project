
#include "UI/Screen/MD_NotifyWidget.h"

#include "Components/TextBlock.h"
#include "UI/MD_CommonButtonBase.h"

void UMD_NotifyWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// 저장된 각 알림 요소를 공통 Setter 경로로 Designer와 런타임 화면에 반영한다.
	SetNotifyContent(NotifyTitle, NotifyDescription, ButtonType);
}

void UMD_NotifyWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// 취소 버튼이 있으면 중복 없이 취소 처리 함수를 연결한다.
	if (IsValid(Btn_Cancel) == true)
	{
		// 공통 버튼 클릭 이벤트의 기존 연결을 정리한 뒤 취소 처리를 연결한다.
		auto& CancelClickedEvent = Btn_Cancel->OnClicked();
		CancelClickedEvent.RemoveAll(this);
		CancelClickedEvent.AddUObject(this, &UMD_NotifyWidget::Cancel);
	}

	// 확인 버튼이 있으면 중복 없이 확인 처리 함수를 연결한다.
	if (IsValid(Btn_Confirm) == true)
	{
		// 공통 버튼 클릭 이벤트의 기존 연결을 정리한 뒤 확인 처리를 연결한다.
		auto& ConfirmClickedEvent = Btn_Confirm->OnClicked();
		ConfirmClickedEvent.RemoveAll(this);
		ConfirmClickedEvent.AddUObject(this, &UMD_NotifyWidget::HandleConfirmClicked);
	}
}

void UMD_NotifyWidget::NativeOnDeactivated()
{
	// 취소 버튼의 현재 활성화 주기 연결을 정리한다.
	if (IsValid(Btn_Cancel) == true)
	{
		// 현재 알림 인스턴스가 등록한 취소 클릭 처리를 제거한다.
		auto& CancelClickedEvent = Btn_Cancel->OnClicked();
		CancelClickedEvent.RemoveAll(this);
	}

	// 확인 버튼의 현재 활성화 주기 연결을 정리한다.
	if (IsValid(Btn_Confirm) == true)
	{
		// 현재 알림 인스턴스가 등록한 확인 클릭 처리를 제거한다.
		auto& ConfirmClickedEvent = Btn_Confirm->OnClicked();
		ConfirmClickedEvent.RemoveAll(this);
	}

	// 재사용되는 알림 인스턴스에 이전 취소 구독이 남지 않도록 정리한다.
	OnCancelRequested.Clear();

	// 재사용되는 알림 인스턴스에 이전 확인 구독이 남지 않도록 정리한다.
	OnConfirmRequested.Clear();

	Super::NativeOnDeactivated();
}

void UMD_NotifyWidget::SetNotifyContent(FText InTitle, FText InDescription, EMD_NotifyButtonType InButtonType)
{
	// 제목 전용 Setter로 저장과 화면 반영을 처리한다.
	SetNotifyTitle(InTitle);

	// 설명 전용 Setter로 저장과 화면 반영을 처리한다.
	SetNotifyDescription(InDescription);

	// 버튼 타입 전용 Setter로 저장과 표시 상태 반영을 처리한다.
	SetButtonType(InButtonType);
}

void UMD_NotifyWidget::SetNotifyTitle(FText InTitle)
{
	// 전달받은 제목을 표시 데이터로 저장한다.
	NotifyTitle = InTitle;

	// 제목 위젯이 있으면 변경된 제목을 적용한다.
	if (IsValid(Txt_Title) == true)
	{
		Txt_Title->SetText(NotifyTitle);
	}
}

void UMD_NotifyWidget::SetNotifyDescription(FText InDescription)
{
	// 전달받은 설명을 표시 데이터로 저장한다.
	NotifyDescription = InDescription;

	// 설명 위젯이 있으면 변경된 설명을 적용한다.
	if (IsValid(Txt_Description) == true)
	{
		Txt_Description->SetText(NotifyDescription);
	}
}

void UMD_NotifyWidget::SetButtonType(EMD_NotifyButtonType InButtonType)
{
	// 전달받은 버튼 조합을 표시 데이터로 저장한다.
	ButtonType = InButtonType;

	// 변경된 버튼 조합을 화면에 반영한다.
	ApplyButtonType();
}

void UMD_NotifyWidget::ApplyButtonType()
{
	// 취소 버튼이 있으면 기본 표시 상태로 초기화한다.
	if (IsValid(Btn_Cancel) == true)
	{
		Btn_Cancel->SetVisibility(ESlateVisibility::Visible);
	}

	// 확인 버튼이 있으면 기본 표시 상태로 초기화한다.
	if (IsValid(Btn_Confirm) == true)
	{
		Btn_Confirm->SetVisibility(ESlateVisibility::Visible);
	}

	// 확인 버튼만 남기는 타입이면 취소 버튼을 레이아웃에서 제거한다.
	if (ButtonType == EMD_NotifyButtonType::ConfirmOnly)
	{
		if (IsValid(Btn_Cancel) == true)
		{
			Btn_Cancel->SetVisibility(ESlateVisibility::Collapsed);
		}

		return;
	}

	// 취소 버튼만 남기는 타입이면 확인 버튼을 레이아웃에서 제거한다.
	if (ButtonType == EMD_NotifyButtonType::CancelOnly)
	{
		if (IsValid(Btn_Confirm) == true)
		{
			Btn_Confirm->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UMD_NotifyWidget::Cancel()
{
	// 취소 요청을 구독 중인 외부 소유자에게 전달한다.
	OnCancelRequested.Broadcast();

	// 취소 입력 처리가 끝나면 알림 화면을 항상 비활성화한다.
	DeactivateWidget();
}

void UMD_NotifyWidget::HandleConfirmClicked()
{
	// 확인 요청을 구독 중인 외부 소유자에게 전달한다.
	OnConfirmRequested.Broadcast();

	// 확인 입력 처리가 끝나면 알림 화면을 항상 비활성화한다.
	DeactivateWidget();
}
