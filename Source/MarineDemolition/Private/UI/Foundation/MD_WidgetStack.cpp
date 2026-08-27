// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Foundation/MD_WidgetStack.h"
#include "UI/MD_CommonActivatableWidget.h"

void UMD_WidgetStack::RemoveWidgetsOnWorldTravel()
{
	// 제거 과정에서 원본 배열이 변경되어도 안전하도록 현재 화면 목록을 복사한다.
	TArray<TObjectPtr<UCommonActivatableWidget>> Widgets = WidgetList;

	// Travel 이후 유지할 화면 중 Stack에서 가장 위에 있는 위치를 저장한다.
	int32 HighestRetainedWidgetIndex = INDEX_NONE;

	// 가장 위쪽 유지 화면을 찾으면 그 위 화면은 모두 제거 대상임이 보장된다.
	for (int32 WidgetIndex = Widgets.Num() - 1; WidgetIndex >= 0; WidgetIndex--)
	{
		// 현재 위치의 화면이 프로젝트 제거 설정을 가진 화면인지 확인한다.
		UMD_CommonActivatableWidget* MDWidget = Cast<UMD_CommonActivatableWidget>(Widgets[WidgetIndex]);
		if (!IsValid(MDWidget))
		{
			continue;
		}

		// 화면에 설정된 World Travel 제거 정책을 가져온다.
		if (MDWidget->ShouldRemoveOnWorldTravel())
		{
			continue;
		}

		// 가장 위쪽 유지 화면의 위치를 저장하고 검색을 종료한다.
		HighestRetainedWidgetIndex = WidgetIndex;
		break;
	}

	// 유지할 화면이 없으면 기존 전체 초기화 경로를 사용한다.
	if (HighestRetainedWidgetIndex == INDEX_NONE)
	{
		ResetForWorldTravel();
		return;
	}

	// 가장 위쪽 유지 화면으로 즉시 전환하며 그 위의 제거 대상 화면을 정리한다.
	const int32 RetainedSwitcherIndex = HighestRetainedWidgetIndex + 1;
	SetSwitcherIndex(RetainedSwitcherIndex, true);
	ResetTransitionState();

	// 유지 화면 아래에 섞여 있는 제거 대상 화면을 개별적으로 정리한다.
	for (int32 WidgetIndex = HighestRetainedWidgetIndex - 1; WidgetIndex >= 0; WidgetIndex--)
	{
		// 프로젝트 설정이 없는 현재 위치의 화면은 기존 동작처럼 Travel 시 제거한다.
		UCommonActivatableWidget* Widget = Widgets[WidgetIndex].Get();
		UMD_CommonActivatableWidget* MDWidget = Cast<UMD_CommonActivatableWidget>(Widget);
		if (IsValid(MDWidget) == false)
		{
			if (IsValid(Widget) == true)
			{
				RemoveWidget(*Widget);
			}
			continue;
		}

		// 화면에 설정된 World Travel 제거 정책을 가져온다.
		if (MDWidget->ShouldRemoveOnWorldTravel() == false)
		{
			continue;
		}

		// 비활성 제거 대상 화면을 Stack에서 즉시 제거한다.
		RemoveWidget(*MDWidget);
	}
}

void UMD_WidgetStack::OnWidgetAddedToList(UCommonActivatableWidget& AddedWidget)
{
	const bool bShouldVerifyActivation = bVerifyNextWidgetActivation;
	bVerifyNextWidgetActivation = false;

	Super::OnWidgetAddedToList(AddedWidget);

	if (bShouldVerifyActivation == false || MySwitcher.IsValid() == false || GetActiveWidget() != nullptr)
	{
		return;
	}

	SetSwitcherIndex(MySwitcher->GetNumWidgets() - 1, true);
	ResetTransitionState();

	ensure(GetActiveWidget() != nullptr);
}

void UMD_WidgetStack::ResetForWorldTravel()
{
	bVerifyNextWidgetActivation = false;

	SetSwitcherIndex(0, true);

	if (WidgetList.IsEmpty() == false)
	{
		const TArray<TObjectPtr<UCommonActivatableWidget>> RemainingWidgets = WidgetList;
		for (UCommonActivatableWidget* Widget : RemainingWidgets)
		{
			if (IsValid(Widget) == true)
			{
				RemoveWidget(*Widget);
			}
		}
	}

	ResetTransitionState();

	if (ensure(WidgetList.IsEmpty()) == false)
	{
		return;
	}

	GeneratedWidgetsPool.ResetPool();
	bVerifyNextWidgetActivation = true;
}

void UMD_WidgetStack::ResetTransitionState()
{
	SetTransitionDuration(GetTransitionDuration());
}
