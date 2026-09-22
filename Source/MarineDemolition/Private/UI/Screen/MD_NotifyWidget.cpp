
#include "UI/Screen/MD_NotifyWidget.h"

#include "Components/TextBlock.h"
#include "UI/MD_CommonButtonBase.h"

void UMD_NotifyWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	// 저장된 각 알림 요소를 공통 Setter 경로로 Designer와 런타임 화면에 반영한다.
	SetNotifyContent(NotifyTitle, NotifyDescription);
	SetButtonType(ButtonType);
}

void UMD_NotifyWidget::NativeOnDeactivated()
{
	// 재사용되는 알림 인스턴스에 이전 취소 구독이 남지 않도록 정리한다.
	OnCancelRequested.Clear();

	// 재사용되는 알림 인스턴스에 이전 확인 구독이 남지 않도록 정리한다.
	OnConfirmRequested.Clear();

	Super::NativeOnDeactivated();
}

void UMD_NotifyWidget::SetNotifyContent(FText InTitle, FText InDescription)
{
	// 제목 전용 Setter로 저장과 화면 반영을 처리한다.
	SetNotifyTitle(InTitle);

	// 설명 전용 Setter로 저장과 화면 반영을 처리한다.
	SetNotifyDescription(InDescription);
}

void UMD_NotifyWidget::SetNotifyTitle(FText InTitle)
{
	// 전달받은 제목을 표시 데이터로 저장한다.
	NotifyTitle = InTitle;

	// 제목 위젯이 있으면 변경된 제목을 적용한다.
	if (IsValid(Txt_Title) == true)
	{
		Txt_Title->SetText(NotifyTitle);

		// 빈 제목은 레이아웃에서 숨기고 내용이 있으면 다시 표시
		Txt_Title->SetVisibility(NotifyTitle.IsEmpty() == true ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
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

		// 빈 설명은 레이아웃에서 숨기고 내용이 있으면 다시 표시
		Txt_Description->SetVisibility(NotifyDescription.IsEmpty() == true ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void UMD_NotifyWidget::SetButtonType(int32 InButtonType)
{
	// 전달받은 버튼 조합을 표시 데이터로 저장한다.
	ButtonType = InButtonType;

	// 변경된 버튼 조합을 화면에 반영한다.
	ApplyButtonType();
}

void UMD_NotifyWidget::ApplyButtonType()
{
	// 취소 버튼 바인딩의 유효성 확인
	if (IsValid(Btn_Cancel) == true)
	{
		// 취소 버튼 표시 여부를 결정하는 플래그 검사 결과 저장
		bool bShowCancel = EnumHasAnyFlags(static_cast<EMD_NotifyButtonType>(ButtonType), EMD_NotifyButtonType::Cancel);

		// 취소 플래그가 선택된 경우 표시하고 해제된 경우 레이아웃에서 제외
		Btn_Cancel->SetVisibility(bShowCancel == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 확인 버튼 바인딩의 유효성 확인
	if (IsValid(Btn_Confirm) == true)
	{
		// 확인 버튼 표시 여부를 결정하는 플래그 검사 결과 저장
		bool bShowConfirm = EnumHasAnyFlags(static_cast<EMD_NotifyButtonType>(ButtonType), EMD_NotifyButtonType::Confirm);

		// 확인 플래그가 선택된 경우 표시하고 해제된 경우 레이아웃에서 제외
		Btn_Confirm->SetVisibility(bShowConfirm == true ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}
