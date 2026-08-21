// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "MD_LocalUIManager.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;
class UUserWidget;

/** LocalPlayer별 PrimaryLayout과 Common UI 레이어를 소유하는 중앙 관리자다. */
UCLASS()
class MARINEDEMOLITION_API UMD_LocalUIManager : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

protected:
	// 현재 LocalPlayer 화면에 직접 삽입된 유일한 최상위 레이아웃을 보관한다.
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PrimaryLayout;

	// PrimaryLayout이 제공한 GameplayTag별 Common UI Stack을 보관한다.
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UCommonActivatableWidgetStack>> LayerStacks;

	// 프로젝트 설정과 PlayerController 교체에서 PrimaryLayout을 생성할 클래스를 보관한다.
	UPROPERTY(Transient)
	TSubclassOf<UUserWidget> PrimaryLayoutClass;

public:
	// 지정한 클래스로 LocalPlayer 전용 PrimaryLayout을 한 번만 생성한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	bool InitializePrimaryLayout(TSubclassOf<UUserWidget> InPrimaryLayoutClass);

	// PrimaryLayout 내부 Stack을 표준 UI 레이어 태그와 연결한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	bool RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetStack* LayerStack);

	// PrimaryLayout 재구성 시 더 이상 유효하지 않은 Stack 연결을 해제한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	void UnregisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetStack* LayerStack);

	// 지정한 레이어 Stack에 CommonActivatableWidget 화면을 Push한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI", meta = (DeterminesOutputType = "ScreenClass"))
	UCommonActivatableWidget* PushScreenToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> ScreenClass);

	// 지정한 레이어의 최상단 화면을 Deactivate하여 Stack에서 Pop한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	bool PopScreenFromLayer(FGameplayTag LayerTag);

	// 지정한 레이어의 모든 화면을 제거한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	bool ClearLayer(FGameplayTag LayerTag);

	// 지정한 레이어의 현재 활성 화면을 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|UI")
	UCommonActivatableWidget* GetActiveScreen(FGameplayTag LayerTag);

	// 현재 생성된 PrimaryLayout을 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|UI")
	FORCEINLINE UUserWidget* GetPrimaryLayout() { return PrimaryLayout; }

	// PrimaryLayout이 생성되어 플레이어 화면에 연결되었는지 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|UI")
	bool IsPrimaryLayoutReady();

protected:
	// Project Settings의 PrimaryLayout 클래스를 로드하고 준비된 플레이어에 Layout을 생성한다.
	void LoadPrimaryLayoutFromSettings();

	// 저장된 클래스를 사용하여 현재 PlayerController에 PrimaryLayout을 생성한다.
	bool CreatePrimaryLayout();

	// 기존 PrimaryLayout과 그 Layout이 등록한 모든 Stack 참조를 제거한다.
	void RemovePrimaryLayout();

	// 요청 태그가 프로젝트에서 표준 UI 레이어인지 확인한다.
	bool IsSupportedLayerTag(FGameplayTag LayerTag);

	// 등록된 Stack 중 요청 태그에 해당하는 유효한 Stack을 반환한다.
	UCommonActivatableWidgetStack* FindLayerStack(FGameplayTag LayerTag);
};
