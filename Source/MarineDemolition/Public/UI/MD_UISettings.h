// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "MD_UISettings.generated.h"

class UCommonActivatableWidget;
class UUserWidget;

/** Project Settings에서 LocalPlayer PrimaryLayout 클래스를 지정하는 UI 설정이다. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "MD Common UI"))
class MARINEDEMOLITION_API UMD_UISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override;

	virtual FName GetSectionName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;

	virtual FText GetSectionDescription() const override;
#endif

public:
	// LocalPlayerSubsystem 시작 시 생성할 PrimaryLayout Widget Blueprint 클래스를 지정한다.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Primary Layout")
	TSoftClassPtr<UUserWidget> InitialPrimaryLayoutClass;

	// 화면 요청 태그별로 생성할 ActivatableWidget 소프트 클래스를 지정한다.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Widget Map", meta = (Categories = "UI"))
	TMap<FGameplayTag, TSoftClassPtr<UCommonActivatableWidget>> WidgetClassMap;

public:
	// 읽기 전용 프로젝트 기본 UI 설정을 반환한다.
	static const UMD_UISettings* Get();

	// 설정된 PrimaryLayout 클래스를 동기 로드하여 서브시스템에 전달한다.
	TSubclassOf<UUserWidget> LoadPrimaryLayoutClass() const;
};
