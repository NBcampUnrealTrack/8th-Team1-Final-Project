// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MD_UISettings.h"

#include "Blueprint/UserWidget.h"

FName UMD_UISettings::GetCategoryName() const
{
	// 프로젝트의 게임 관련 설정 아래에 Common UI 항목을 표시한다.
	FName ResolvedCategoryName = FName(TEXT("Game"));
	return ResolvedCategoryName;
}

FName UMD_UISettings::GetSectionName() const
{
	// Project Settings에서 식별할 고정 UI 섹션 이름을 반환한다.
	FName ResolvedSectionName = FName(TEXT("MD Common UI"));
	return ResolvedSectionName;
}

#if WITH_EDITOR
FText UMD_UISettings::GetSectionText() const
{
	// 에디터의 Project Settings 목록에 표시할 UI 섹션 이름을 제공한다.
	FText SectionText = NSLOCTEXT("MDCommonUI", "SettingsSection", "MD Common UI");
	return SectionText;
}

FText UMD_UISettings::GetSectionDescription() const
{
	// 설정 화면에서 PrimaryLayout 항목의 사용 목적을 안내한다.
	FText SectionDescription = NSLOCTEXT(
		"MDCommonUI",
		"SettingsSectionDescription",
		"LocalPlayer별 Common UI PrimaryLayout 클래스를 설정합니다.");
	return SectionDescription;
}
#endif

const UMD_UISettings* UMD_UISettings::Get()
{
	// Config 시스템이 관리하는 읽기 전용 기본 UI 설정을 조회한다.
	const UMD_UISettings* UISettings = GetDefault<UMD_UISettings>();
	return UISettings;
}

TSubclassOf<UUserWidget> UMD_UISettings::LoadPrimaryLayoutClass() const
{
	// 소프트 클래스 경로를 런타임에 사용할 Widget 클래스로 동기 로드한다.
	UClass* LoadedPrimaryLayoutClass = InitialPrimaryLayoutClass.LoadSynchronous();

	// 로드 결과를 타입이 제한된 PrimaryLayout 클래스로 변환한다.
	TSubclassOf<UUserWidget> LoadedLayoutClass = LoadedPrimaryLayoutClass;
	return LoadedLayoutClass;
}
