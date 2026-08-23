// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MD_LocalUIManager.h"

#include "UI/Foundation/MD_WidgetStack.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "CommonActivatableWidget.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Util/MD_LogUtility.h"
#include "UI/MD_UISettings.h"

void UMD_LocalUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
	// 부모 서브시스템 초기화를 먼저 완료하여 LocalPlayer 접근 기반을 준비한다.
	Super::Initialize(Collection);

	// 프로젝트 설정에서 PrimaryLayout 클래스를 읽어 초기 UI 생성을 준비한다.
	LoadPrimaryLayoutFromSettings();

	// 소유한 World가 제거되기 전에 화면과 비동기 요청 상태를 정리하도록 알림을 등록한다.
	WorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UMD_LocalUIManager::HandleWorldBeginTearDown);
}

void UMD_LocalUIManager::LoadPrimaryLayoutFromSettings()
{
	// Project Settings에 등록된 Common UI 설정을 조회한다.
	const UMD_UISettings* UISettings = UMD_UISettings::Get();
	if (IsValid(UISettings) == false)
	{
		return;
	}

	// 설정된 소프트 클래스를 로드하여 World 전환 후에도 재사용할 클래스에 저장한다.
	PrimaryLayoutClass = UISettings->LoadPrimaryLayoutClass();

	// 실제 Layout 생성은 유효한 PlayerController가 전달되는 PlayerControllerChanged 시점에 진행한다.
}

void UMD_LocalUIManager::Deinitialize()
{
	// LocalPlayer Subsystem 종료 이후 World delegate가 호출되지 않도록 등록을 해제한다.
	FWorldDelegates::OnWorldBeginTearDown.Remove(WorldBeginTearDownHandle);
	WorldBeginTearDownHandle.Reset();

	// LocalPlayer 제거 전에 플레이어 화면과 Stack 참조를 정리한다.
	RemovePrimaryLayout();

	// 부모 서브시스템의 종료 처리를 이어서 실행한다.
	Super::Deinitialize();
}

void UMD_LocalUIManager::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	// 새 PlayerController가 아직 준비되지 않은 중간 상태면 기존 UI를 유지한다.
	if (IsValid(NewPlayerController) == false)
	{
		return;
	}

	// 기존 PrimaryLayout과 Stack 상태를 유지하면서 Owning PlayerController를 갱신한다.
	if (IsValid(PrimaryLayout) == true)
	{
		UE_LOG(MarineDemolition, Log, TEXT("Local UI Manager가 기존 PrimaryLayout의 Owning Player Controller를 변경하였습니다."));
		PrimaryLayout->SetOwningPlayer(NewPlayerController);

		// 기존 Layout이 Viewport에 남아 있으면 추가 등록 없이 변경 처리를 완료한다.
		if (IsPrimaryLayoutReady() == true)
		{
			return;
		}

		// 화면에서 제거된 Layout을 같은 LocalPlayer 영역에 다시 등록한다.
		// GameViewport 관리자를 검증하여 복원에 필요한 컨텍스트를 보장한다.
		UGameViewportSubsystem* GameViewportManager = UGameViewportSubsystem::Get();
		if (IsValid(GameViewportManager) == false)
		{
			return;
		}

		// 이후 World가 제거되어도 복원한 PrimaryLayout을 자동 제거하지 않도록 설정한다.
		FGameViewportWidgetSlot ViewportSlot;
		ViewportSlot.bAutoRemoveOnWorldRemoved = false;

		// 기존 Layout 인스턴스를 재사용하여 활성 화면과 레이어 Stack 상태를 보존한다.
		bool bRestoreResult = GameViewportManager->AddWidgetForPlayer(PrimaryLayout, GetLocalPlayer(), ViewportSlot);
		if (bRestoreResult == false)
		{
			UE_LOG(MarineDemolition, Warning, TEXT("Local UI Manager가 기존 PrimaryLayout의 Viewport 등록을 복원하지 못했습니다."));
		}
		return;
	}

	// 이전에 지정된 Layout 클래스가 없으면 명시적 초기화 요청을 기다린다.
	if (IsValid(PrimaryLayoutClass) == true)
	{
		// 기존 Layout이 없는 경우에만 새 PlayerController를 통해 PrimaryLayout을 생성한다.
		bool bCreateResult = CreatePrimaryLayout();
		if (bCreateResult == false)
		{
			UE_LOG(MarineDemolition, Warning, TEXT("Local UI Manager가 PlayerController 교체 후 PrimaryLayout을 복원하지 못했습니다."));
		}
		return;
	}
}

bool UMD_LocalUIManager::InitializePrimaryLayout(TSubclassOf<UUserWidget> InPrimaryLayoutClass)
{
	// 유효하지 않은 클래스가 기존 Layout 설정을 지우지 않도록 요청을 거부한다.
	if (IsValid(InPrimaryLayoutClass) == false)
	{
		return false;
	}

	// 이후 PlayerController 교체에서도 동일한 Layout을 복원할 수 있도록 클래스를 저장한다.
	PrimaryLayoutClass = InPrimaryLayoutClass;

	// 같은 클래스의 Layout이 이미 화면에 있으면 중복 생성 없이 성공 처리한다.
	bool bIsReadyResult = IsPrimaryLayoutReady();
	if (bIsReadyResult == true)
	{
		UClass* CurrentLayoutClass = PrimaryLayout->GetClass();
		if (CurrentLayoutClass == InPrimaryLayoutClass)
		{
			return true;
		}
	}

	// 다른 Layout이 남아 있으면 등록 Stack까지 함께 정리한다.
	RemovePrimaryLayout();

	// 현재 LocalPlayer의 PlayerController를 기준으로 Layout을 생성한다.
	bool bCreateResult = CreatePrimaryLayout();
	return bCreateResult;
}

bool UMD_LocalUIManager::RegisterLayer(FGameplayTag LayerTag, UMD_WidgetStack* LayerStack)
{
	// 표준 UI 레이어가 아니거나 실제 Stack이 없는 등록 요청을 한 번에 거부한다.
	if (IsSupportedLayerTag(LayerTag) == false || IsValid(LayerStack) == false)
	{
		return false;
	}

	// TMap의 Add 교체 동작을 사용하여 한 레이어에 최신 Stack 하나만 유지한다.
	if (LayerStacks.Contains(LayerTag) == true)
	{
		UnregisterLayer(LayerTag, LayerStacks[LayerTag]);
	}
	LayerStacks.Add(LayerTag, LayerStack);

	// 등록 성공을 호출자에게 전달한다.
	return true;
}

void UMD_LocalUIManager::UnregisterLayer(FGameplayTag LayerTag, UMD_WidgetStack* LayerStack)
{
	// 현재 등록된 Stack을 조회하여 오래된 Layout이 새 연결을 지우는 것을 막는다.
	UMD_WidgetStack* RegisteredStack = FindLayerStack(LayerTag);
	if (RegisteredStack != LayerStack)
	{
		return;
	}

	// 동일한 Stack 연결만 레이어 맵에서 제거한다.
	LayerStacks.Remove(LayerTag);
}

UCommonActivatableWidget* UMD_LocalUIManager::PushScreenToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> ScreenClass)
{
	// 별도 초기화가 없는 동기 요청도 공통 Stack Push 경로를 사용한다.
	UCommonActivatableWidget* PushedScreen = PushScreenToLayerInitialized(LayerTag, ScreenClass, [](UCommonActivatableWidget&) {});
	return PushedScreen;
}

bool UMD_LocalUIManager::PopScreenFromLayer(FGameplayTag LayerTag)
{
	// 대상 레이어에 현재 표시 중인 화면이 있는지 확인한다.
	UCommonActivatableWidget* ActiveScreen = GetActiveScreen(LayerTag);
	if (ActiveScreen == nullptr)
	{
		return false;
	}

	// Deactivate를 통해 Common UI Stack의 표준 자동 Pop 경로를 사용한다.
	ActiveScreen->DeactivateWidget();

	// Pop 요청이 전달되었음을 호출자에게 알린다.
	return true;
}

bool UMD_LocalUIManager::ClearLayer(FGameplayTag LayerTag)
{
	// 등록된 레이어가 없으면 제거할 화면도 없으므로 실패 처리한다.
	UMD_WidgetStack* LayerStack = FindLayerStack(LayerTag);
	if (LayerStack == nullptr)
	{
		return false;
	}

	// Stack이 소유한 화면을 Common UI 컨테이너의 정리 경로로 모두 제거한다.
	LayerStack->ClearWidgets();

	// 레이어 정리가 수행되었음을 호출자에게 알린다.
	return true;
}

UCommonActivatableWidget* UMD_LocalUIManager::GetActiveScreen(FGameplayTag LayerTag) const
{
	// 등록된 레이어 Stack을 조회한다.
	UMD_WidgetStack* LayerStack = FindLayerStack(LayerTag);
	if (LayerStack == nullptr)
	{
		return nullptr;
	}

	// Stack 최상단에서 현재 활성화된 화면을 저장한다.
	UCommonActivatableWidget* ActiveScreen = LayerStack->GetActiveWidget();
	return ActiveScreen;
}

bool UMD_LocalUIManager::IsPrimaryLayoutReady() const
{
	// Layout과 GameViewport 관리자를 함께 검증하여 등록 상태 조회의 안전성을 보장한다.
	UGameViewportSubsystem* GameViewportManager = UGameViewportSubsystem::Get();
	if (IsValid(PrimaryLayout) == false || IsValid(GameViewportManager) == false)
	{
		return false;
	}

	// 관리자의 등록 여부를 결과 변수에 저장하여 반환한다.
	bool bIsWidgetAdded = GameViewportManager->IsWidgetAdded(PrimaryLayout);
	return bIsWidgetAdded;
}

bool UMD_LocalUIManager::IsLayerRegistered(FGameplayTag LayerTag) const
{
	// 등록된 레이어 Stack을 조회하여 Async 요청 전에 목적지 준비 상태를 확인한다.
	UMD_WidgetStack* LayerStack = FindLayerStack(LayerTag);

	// 중앙 조회 함수가 검증한 Stack의 존재 여부를 등록 상태로 반환한다.
	bool bIsLayerRegistered = LayerStack != nullptr;
	return bIsLayerRegistered;
}

UCommonActivatableWidget* UMD_LocalUIManager::PushScreenToLayerInitialized(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> ScreenClass, TFunctionRef<void(UCommonActivatableWidget&)> InitializeScreen)
{
	// PrimaryLayout이 등록한 대상 레이어 Stack을 찾는다.
	UMD_WidgetStack* LayerStack = FindLayerStack(LayerTag);

	// 생성할 화면 클래스 또는 등록된 레이어 Stack이 없으면 Stack을 변경하지 않는다.
	if (LayerStack == nullptr || IsValid(ScreenClass) == false)
	{
		return nullptr;
	}

	if (LayerStack == nullptr)
	{
		return nullptr;
	}

	// Stack 활성화 전에 호출자가 화면 데이터를 초기화할 수 있는 Push 경로를 사용한다.
	UCommonActivatableWidget* PushedScreen = LayerStack->AddWidget(ScreenClass, InitializeScreen);
	return PushedScreen;
}

void UMD_LocalUIManager::SuspendInputForAsyncRequest()
{
	// 병렬 요청이 있어도 첫 요청에서만 기존 Layout 활성 상태를 저장한다.
	AsyncInputSuspensionCount++;
	if (AsyncInputSuspensionCount > 1 || IsValid(PrimaryLayout) == false)
	{
		return;
	}

	// 입력을 차단할 PrimaryLayout이 없으면 카운트만 유지하여 완료 경로를 맞춘다.
	if (IsValid(PrimaryLayout) == false)
	{
		return;
	}

	// 모든 자식 위젯의 Click과 탐색 입력을 차단하기 전에 기존 활성 상태를 보관한다.
	bPrimaryLayoutWasEnabled = PrimaryLayout->GetIsEnabled();
	PrimaryLayout->SetIsEnabled(false);
}

void UMD_LocalUIManager::ResumeInputForAsyncRequest()
{
	// 이미 모두 복구된 상태에서는 입력 활성 상태를 다시 변경하지 않는다.
	if (AsyncInputSuspensionCount <= 0)
	{
		return;
	}

	// 마지막 요청이 완료될 때까지 PrimaryLayout 입력 차단을 유지한다.
	AsyncInputSuspensionCount--;
	if (AsyncInputSuspensionCount > 0)
	{
		return;
	}

	// 유지 중인 Layout이 있으면 첫 요청 이전의 활성 상태로 복구한다.
	if (IsValid(PrimaryLayout) == true)
	{
		PrimaryLayout->SetIsEnabled(bPrimaryLayoutWasEnabled);
	}
}

bool UMD_LocalUIManager::CreatePrimaryLayout()
{
	// Layout 클래스를 함께 검증하여 생성에 필요한 기본 컨텍스트를 보장한다.
	if (IsValid(PrimaryLayoutClass) == false)
	{
		return false;
	}

	// PlayerController와 GameViewport 관리자를 함께 검증하여 생성과 등록 경로를 준비한다.
	ULocalPlayer* OwningLocalPlayer = GetLocalPlayer();
	APlayerController* OwningPlayerController = IsValid(OwningLocalPlayer) == true ? OwningLocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	UGameViewportSubsystem* GameViewportManager = UGameViewportSubsystem::Get();
	if (IsValid(OwningPlayerController) == false || IsValid(GameViewportManager) == false)
	{
		return false;
	}

	// PlayerController 경로로 올바른 LocalPlayer를 초기화하며 엔진 내부에서 GameInstance를 Outer로 사용한다.
	UUserWidget* CreatedLayout = CreateWidget<UUserWidget>(OwningPlayerController, PrimaryLayoutClass);
	if (IsValid(CreatedLayout) == false)
	{
		return false;
	}

	// 입력과 플레이어 컨텍스트가 현재 PlayerController를 사용하도록 OwningPlayer를 명시한다.
	CreatedLayout->SetOwningPlayer(OwningPlayerController);

	// World가 제거되어도 PrimaryLayout을 자동 제거하지 않는 전체 화면 슬롯을 구성한다.
	FGameViewportWidgetSlot ViewportSlot;
	ViewportSlot.bAutoRemoveOnWorldRemoved = false;

	// 분할 화면에서도 지정 LocalPlayer 영역에만 PrimaryLayout이 표시되도록 등록한다.
	bool bAddResult = GameViewportManager->AddWidgetForPlayer(CreatedLayout, OwningLocalPlayer, ViewportSlot);
	if (bAddResult == false)
	{
		return false;
	}

	// 성공적으로 삽입된 Layout을 수명 관리 대상으로 저장한다.
	PrimaryLayout = CreatedLayout;

	// 생성과 화면 삽입이 모두 끝났음을 반환한다.
	return true;
}

void UMD_LocalUIManager::RemovePrimaryLayout()
{
	// 이전 Layout이 등록한 Stack을 새 World에서 재사용하지 않도록 먼저 비운다.
	LayerStacks.Reset();

	// 제거할 Layout이 없는 경우에도 Stack 정리는 완료된 상태다.
	if (IsValid(PrimaryLayout) == false)
	{
		return;
	}

	// Widget 자체 제거 경로를 사용한다.
	PrimaryLayout->RemoveFromParent();

	// 가비지 컬렉션이 이전 World의 Layout을 회수할 수 있도록 강한 참조를 해제한다.
	PrimaryLayout = nullptr;
}

void UMD_LocalUIManager::HandleWorldBeginTearDown(UWorld* World)
{
	// 다른 PIE 인스턴스나 서버 World의 종료 알림은 이 LocalPlayer UI에 적용하지 않는다.
	UWorld* OwningWorld = GetWorld();
	if (World != OwningWorld)
	{
		return;
	}

	// 이전 World에서 시작된 Async UI 완료 콜백이 새 화면을 생성하지 못하도록 세대를 변경한다.
	WorldGeneration++;

	// 각 화면이 선택한 정책에 따라 이전 World에서 제거할 화면만 Stack에서 정리한다.
	RemoveWorldTravelWidgetsFromAllLayers();

	// Travel 이후 새 Async 요청이 이전 요청의 입력 차단 횟수를 이어받지 않도록 초기화한다.
	AsyncInputSuspensionCount = 0;

	// 새 World에서 생성할 Layout이 기본 활성 상태로 시작하도록 복구 기준을 초기화한다.
	bPrimaryLayoutWasEnabled = true;
}

void UMD_LocalUIManager::RemoveWorldTravelWidgetsFromAllLayers()
{
	// 각 레이어에서 World Travel 제거 정책이 활성화된 화면만 정리한다.
	for (const TPair<FGameplayTag, TObjectPtr<UMD_WidgetStack>>& Pair : LayerStacks)
	{
		if (IsValid(Pair.Value) == true)
		{
			// 유지 화면과 순서를 보존하면서 제거 대상 화면의 내부 풀 참조까지 해제한다.
			Pair.Value->RemoveWidgetsOnWorldTravel();
		}
	}
}

bool UMD_LocalUIManager::IsSupportedLayerTag(FGameplayTag LayerTag) const
{
	// UI.Layer의 하위 태그인지 확인한다.
	bool bIsLayerTag = LayerTag.MatchesTag(MDGameplayTags::UI_Layer);

	return bIsLayerTag;
}

UMD_WidgetStack* UMD_LocalUIManager::FindLayerStack(FGameplayTag LayerTag) const
{
	// 허용되지 않은 태그는 레이어 맵을 조회하지 않는다.
	bool bIsSupportedTag = IsSupportedLayerTag(LayerTag);
	if (bIsSupportedTag == false)
	{
		return nullptr;
	}

	// TMap에서 레이어 Stack을 값으로 조회한다.
	UMD_WidgetStack* FoundStack = LayerStacks.FindRef(LayerTag);

	// Common UI Stack을 호출자에게 반환한다.
	return FoundStack;
}
