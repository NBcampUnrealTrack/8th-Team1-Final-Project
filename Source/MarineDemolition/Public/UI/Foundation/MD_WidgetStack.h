// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "MD_WidgetStack.generated.h"

/** World Travel 제거 정책을 적용하는 프로젝트 전용 Stack이다. */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API UMD_WidgetStack : public UCommonActivatableWidgetStack
{
	GENERATED_BODY()

public:
	void RemoveWidgetsOnWorldTravel();

	// 전환 대기 화면의 취소와 이전 화면 활성화 복원을 포함한 제거
	void RemoveWidgetAndRestore(UCommonActivatableWidget* Widget);

protected:
	virtual void OnWidgetAddedToList(UCommonActivatableWidget& AddedWidget) override;

private:
	void ResetForWorldTravel();
	void ResetTransitionState();

	bool bVerifyNextWidgetActivation = false;
};
