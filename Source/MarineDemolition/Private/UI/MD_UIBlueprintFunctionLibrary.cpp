// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MD_UIBlueprintFunctionLibrary.h"

#include "UI/MD_UISettings.h"

TSoftClassPtr<UCommonActivatableWidget> UMD_UIBlueprintFunctionLibrary::GetWidgetClassByTag(FGameplayTag WidgetTag)
{
	// 조회 실패 시 Blueprint에 빈 소프트 클래스 참조를 반환할 기본값을 준비한다.
	TSoftClassPtr<UCommonActivatableWidget> WidgetClass;

	// 유효하지 않은 화면 요청 태그는 Project Settings Map에서 조회하지 않는다.
	if (WidgetTag.IsValid() == false)
	{
		return WidgetClass;
	}

	// 읽기 전용 Project Settings에서 태그별 Widget 클래스 등록 정보를 가져온다.
	const UMD_UISettings* UISettings = UMD_UISettings::Get();
	if (IsValid(UISettings) == false)
	{
		return WidgetClass;
	}

	// 정확히 일치하는 화면 요청 태그의 소프트 클래스를 Map에서 찾는다.
	const TSoftClassPtr<UCommonActivatableWidget>* RegisteredWidgetClass = UISettings->WidgetClassMap.Find(WidgetTag);
	if (RegisteredWidgetClass == nullptr)
	{
		return WidgetClass;
	}

	// 설정에 저장된 소프트 클래스 참조를 Blueprint 반환값에 복사한다.
	WidgetClass = *RegisteredWidgetClass;
	return WidgetClass;
}
