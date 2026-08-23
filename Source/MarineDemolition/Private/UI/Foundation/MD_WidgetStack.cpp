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
		if (IsValid(MDWidget) == false)
		{
			continue;
		}

		// 화면에 설정된 World Travel 제거 정책을 가져온다.
		if (MDWidget->ShouldRemoveOnWorldTravel() == true)
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
		ClearWidgetsAndResetPool();
		return;
	}

	// 가장 위쪽 유지 화면으로 즉시 전환하며 그 위의 제거 대상 화면을 정리한다.
	int32 RetainedSwitcherIndex = HighestRetainedWidgetIndex + 1;
	SetSwitcherIndex(RetainedSwitcherIndex, true);

	// 유지 화면 아래에 섞여 있는 제거 대상 화면을 개별적으로 정리한다.
	for (int32 WidgetIndex = HighestRetainedWidgetIndex - 1; WidgetIndex >= 0; WidgetIndex--)
	{
		// 프로젝트 설정이 없는 현재 위치의 화면은 기존 동작처럼 Travel 시 제거한다.
		UMD_CommonActivatableWidget* MDWidget = Cast<UMD_CommonActivatableWidget>(Widgets[WidgetIndex]);
		if (IsValid(MDWidget) == false)
		{
			RemoveWidget(*MDWidget);
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

	// 제거된 화면이 새 World에서 재사용되지 않도록 비활성 풀을 초기화한다.
	GeneratedWidgetsPool.ResetPool();
}

void UMD_WidgetStack::ClearWidgetsAndResetPool()
{
	// 전환 완료를 기다리지 않고 빈 슬롯으로 이동하여 표시 중인 화면을 Stack에서 제거한다.
	SetSwitcherIndex(0, true);

	// 비활성 화면 UObject와 Slate 캐시 참조를 모두 해제하여 다음 Push에서 재생성을 보장한다.
	GeneratedWidgetsPool.ResetPool();
}
