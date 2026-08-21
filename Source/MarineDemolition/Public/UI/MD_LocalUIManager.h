// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "MD_LocalUIManager.generated.h"

class UCommonActivatableWidget;
class UMD_WidgetStack;
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
	TMap<FGameplayTag, TObjectPtr<UMD_WidgetStack>> LayerStacks;

	// 프로젝트 설정과 PlayerController 교체에서 PrimaryLayout을 생성할 클래스를 보관한다.
	UPROPERTY(Transient)
	TSubclassOf<UUserWidget> PrimaryLayoutClass;

	// 동시에 진행 중인 Async UI 요청이 PrimaryLayout 입력을 차단한 횟수를 보관한다.
	UPROPERTY(Transient)
	int32 AsyncInputSuspensionCount = 0;

	// 첫 Async 입력 차단 전에 PrimaryLayout이 사용하던 활성 상태를 보관한다.
	UPROPERTY(Transient)
	bool bPrimaryLayoutWasEnabled = true;

	// World 전환 전에 시작된 Async UI 요청을 식별할 세대 번호를 보관한다.
	UPROPERTY(Transient)
	uint32 WorldGeneration = 0;

	// World 종료 알림을 Deinitialize에서 정확히 해제할 delegate handle을 보관한다.
	FDelegateHandle WorldBeginTearDownHandle;

public:
	// 지정한 클래스로 LocalPlayer 전용 PrimaryLayout을 한 번만 생성한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	bool InitializePrimaryLayout(TSubclassOf<UUserWidget> InPrimaryLayoutClass);

	// PrimaryLayout 내부 Stack을 표준 UI 레이어 태그와 연결한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	bool RegisterLayer(FGameplayTag LayerTag, UMD_WidgetStack* LayerStack);

	// PrimaryLayout 재구성 시 더 이상 유효하지 않은 Stack 연결을 해제한다.
	UFUNCTION(BlueprintCallable, Category = "MarineDemolition|UI")
	void UnregisterLayer(FGameplayTag LayerTag, UMD_WidgetStack* LayerStack);

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
	UCommonActivatableWidget* GetActiveScreen(FGameplayTag LayerTag) const;

	// 현재 생성된 PrimaryLayout을 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|UI")
	FORCEINLINE UUserWidget* GetPrimaryLayout() const { return PrimaryLayout; }

	// PrimaryLayout이 생성되어 플레이어 화면에 연결되었는지 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|UI")
	bool IsPrimaryLayoutReady() const;

	// 지정한 레이어에 유효한 Common UI Stack이 등록되어 있는지 반환한다.
	UFUNCTION(BlueprintPure, Category = "MarineDemolition|UI")
	bool IsLayerRegistered(FGameplayTag LayerTag) const;

	// Stack 활성화 전에 화면 초기화 함수를 실행하며 화면을 Push한다.
	UCommonActivatableWidget* PushScreenToLayerInitialized(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> ScreenClass, TFunctionRef<void(UCommonActivatableWidget&)> InitializeScreen);

	// Async 화면 로드 중 PrimaryLayout의 중복 입력을 차단한다.
	void SuspendInputForAsyncRequest();

	// 완료된 Async 요청의 입력 차단을 해제하고 기존 활성 상태를 복구한다.
	void ResumeInputForAsyncRequest();

	// 현재 World에 속한 Async UI 요청이 저장할 세대 번호를 반환한다.
	FORCEINLINE uint32 GetWorldGeneration() const { return WorldGeneration; }

	// Async UI 요청이 현재 World 세대에 속하는지 반환한다.
	FORCEINLINE bool IsWorldGenerationCurrent(uint32 InWorldGeneration) const { return InWorldGeneration == WorldGeneration; }

protected:
	// Project Settings의 PrimaryLayout 클래스를 로드한다.
	void LoadPrimaryLayoutFromSettings();

	// 저장된 클래스를 사용하여 현재 PlayerController에 PrimaryLayout을 생성한다.
	bool CreatePrimaryLayout();

	// 기존 PrimaryLayout과 그 Layout이 등록한 모든 Stack 참조를 제거한다.
	void RemovePrimaryLayout();

	// 소유한 World가 종료되기 전에 화면과 비동기 입력 상태를 정리한다.
	void HandleWorldBeginTearDown(UWorld* World);

	// World 객체를 참조할 수 있는 화면을 상위 레이어부터 모두 제거한다.
	void ClearAllLayers();

	// 요청 태그가 프로젝트에서 표준 UI 레이어인지 확인한다.
	bool IsSupportedLayerTag(FGameplayTag LayerTag) const;

	// 등록된 Stack 중 요청 태그에 해당하는 유효한 Stack을 반환한다.
	UMD_WidgetStack* FindLayerStack(FGameplayTag LayerTag) const;
};
