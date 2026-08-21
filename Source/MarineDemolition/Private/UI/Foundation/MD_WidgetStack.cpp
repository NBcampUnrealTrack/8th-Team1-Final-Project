// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Foundation/MD_WidgetStack.h"

void UMD_WidgetStack::ClearWidgetsAndResetPool()
{
	// 전환 완료를 기다리지 않고 빈 슬롯으로 이동하여 표시 중인 화면을 Stack에서 제거한다.
	SetSwitcherIndex(0, true);

	// 비활성 화면 UObject와 Slate 캐시 참조를 모두 해제하여 다음 Push에서 재생성을 보장한다.
	GeneratedWidgetsPool.ResetPool();
}
