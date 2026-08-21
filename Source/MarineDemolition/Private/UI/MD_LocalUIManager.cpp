// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MD_LocalUIManager.h"

#include "Blueprint/GameViewportSubsystem.h"
#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Util/MD_LogUtility.h"

void UMD_LocalUIManager::Deinitialize()
{
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

	// 기존 PrimaryLayout과 Stack 상태를 유지하면서 Owning PlayerController만 갱신한다.
	if (IsValid(PrimaryLayout) == true)
	{
		PrimaryLayout->SetOwningPlayer(NewPlayerController);
		return;
	}

	// 이전에 지정된 Layout 클래스가 없으면 명시적 초기화 요청을 기다린다.
	if (IsValid(PrimaryLayoutClass) == false)
	{
		return;
	}

	// 기존 Layout이 없는 경우에만 새 PlayerController를 통해 PrimaryLayout을 생성한다.
	bool bCreateResult = CreatePrimaryLayout();
	if (bCreateResult == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("Local UI Manager가 PlayerController 교체 후 PrimaryLayout을 복원하지 못했습니다."));
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

bool UMD_LocalUIManager::RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetStack* LayerStack)
{
	// 표준 UI 레이어가 아닌 태그가 임의 Stack으로 등록되는 것을 막는다.
	bool bIsSupportedTag = IsSupportedLayerTag(LayerTag);
	if (bIsSupportedTag == false)
	{
		return false;
	}

	// 실제 Stack이 없는 Blueprint 등록 요청을 거부한다.
	if (IsValid(LayerStack) == false)
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

void UMD_LocalUIManager::UnregisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetStack* LayerStack)
{
	// 현재 등록된 Stack을 조회하여 오래된 Layout이 새 연결을 지우는 것을 막는다.
	UCommonActivatableWidgetStack* RegisteredStack = FindLayerStack(LayerTag);
	if (RegisteredStack != LayerStack)
	{
		return;
	}

	// 동일한 Stack 연결만 레이어 맵에서 제거한다.
	LayerStacks.Remove(LayerTag);
}

UCommonActivatableWidget* UMD_LocalUIManager::PushScreenToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> ScreenClass)
{
	// 생성할 화면 클래스가 없으면 Stack을 변경하지 않는다.
	if (IsValid(ScreenClass) == false)
	{
		return nullptr;
	}

	// PrimaryLayout이 등록한 대상 레이어 Stack을 찾는다.
	UCommonActivatableWidgetStack* LayerStack = FindLayerStack(LayerTag);
	if (IsValid(LayerStack) == false)
	{
		return nullptr;
	}

	// Common UI Stack이 화면 생성과 활성화 수명을 소유하도록 클래스로 Push한다.
	UCommonActivatableWidget* PushedScreen = LayerStack->AddWidget(ScreenClass);
	return PushedScreen;
}

bool UMD_LocalUIManager::PopScreenFromLayer(FGameplayTag LayerTag)
{
	// 대상 레이어에 현재 표시 중인 화면이 있는지 확인한다.
	UCommonActivatableWidget* ActiveScreen = GetActiveScreen(LayerTag);
	if (IsValid(ActiveScreen) == false)
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
	UCommonActivatableWidgetStack* LayerStack = FindLayerStack(LayerTag);
	if (IsValid(LayerStack) == false)
	{
		return false;
	}

	// Stack이 소유한 화면을 Common UI 컨테이너의 정리 경로로 모두 제거한다.
	LayerStack->ClearWidgets();

	// 레이어 정리가 수행되었음을 호출자에게 알린다.
	return true;
}

UCommonActivatableWidget* UMD_LocalUIManager::GetActiveScreen(FGameplayTag LayerTag)
{
	// 등록된 레이어 Stack을 조회한다.
	UCommonActivatableWidgetStack* LayerStack = FindLayerStack(LayerTag);
	if (IsValid(LayerStack) == false)
	{
		return nullptr;
	}

	// Stack 최상단에서 현재 활성화된 화면을 저장한다.
	UCommonActivatableWidget* ActiveScreen = LayerStack->GetActiveWidget();
	return ActiveScreen;
}

bool UMD_LocalUIManager::IsPrimaryLayoutReady()
{
	// Layout이 존재하는지 먼저 확인한다.
	if (IsValid(PrimaryLayout) == false)
	{
		return false;
	}

	// GameViewport 관리자를 통해 실제 등록 상태를 확인한다.
	UGameViewportSubsystem* GameViewportManager = UGameViewportSubsystem::Get();
	if (IsValid(GameViewportManager) == false)
	{
		return false;
	}

	// 관리자의 등록 여부를 결과 변수에 저장하여 반환한다.
	bool bIsWidgetAdded = GameViewportManager->IsWidgetAdded(PrimaryLayout);
	return bIsWidgetAdded;
}

bool UMD_LocalUIManager::CreatePrimaryLayout()
{
	// 생성에 필요한 Layout 클래스가 지정되어 있는지 확인한다.
	if (IsValid(PrimaryLayoutClass) == false)
	{
		return false;
	}

	// 전역 0번 플레이어 대신 이 서브시스템을 소유한 LocalPlayer를 사용한다.
	ULocalPlayer* OwningLocalPlayer = GetLocalPlayer();
	if (IsValid(OwningLocalPlayer) == false)
	{
		return false;
	}

	// 해당 LocalPlayer에 연결된 PlayerController를 생성 전 확인하여 올바른 플레이어 컨텍스트를 보장한다.
	APlayerController* OwningPlayerController = OwningLocalPlayer->GetPlayerController(GetWorld());
	if (IsValid(OwningPlayerController) == false)
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

	// Engine 전역 GameViewport 관리자를 통해 World 수명과 분리된 화면 등록을 수행한다.
	UGameViewportSubsystem* GameViewportManager = UGameViewportSubsystem::Get();
	if (IsValid(GameViewportManager) == false)
	{
		return false;
	}

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

	// GameViewport 관리자가 등록한 화면과 LocalPlayer 슬롯을 함께 제거한다.
	UGameViewportSubsystem* GameViewportManager = UGameViewportSubsystem::Get();
	if (IsValid(GameViewportManager) == true)
	{
		GameViewportManager->RemoveWidget(PrimaryLayout);
	}
	else
	{
		// Engine 종료 단계에서 관리자를 얻지 못하면 Widget 자체 제거 경로를 사용한다.
		PrimaryLayout->RemoveFromParent();
	}

	// 가비지 컬렉션이 이전 World의 Layout을 회수할 수 있도록 강한 참조를 해제한다.
	PrimaryLayout = nullptr;
}

bool UMD_LocalUIManager::IsSupportedLayerTag(FGameplayTag LayerTag)
{
	// 유효하지 않은 GameplayTag 요청을 조기에 거부한다.
	bool bIsValidTag = LayerTag.IsValid();
	if (bIsValidTag == false)
	{
		return false;
	}

	// UI.Layer의 하위 태그인지 확인한다.
	bool bIsLayerTag = LayerTag.MatchesTag(MDGameplayTags::UI_Layer);

	return bIsLayerTag;
}

UCommonActivatableWidgetStack* UMD_LocalUIManager::FindLayerStack(FGameplayTag LayerTag)
{
	// 허용되지 않은 태그는 레이어 맵을 조회하지 않는다.
	bool bIsSupportedTag = IsSupportedLayerTag(LayerTag);
	if (bIsSupportedTag == false)
	{
		return nullptr;
	}

	// TMap에서 레이어 Stack을 값으로 조회한다.
	UCommonActivatableWidgetStack* FoundStack = LayerStacks.FindRef(LayerTag);
	if (IsValid(FoundStack) == false)
	{
		return nullptr;
	}

	// 유효한 Common UI Stack을 호출자에게 반환한다.
	return FoundStack;
}
