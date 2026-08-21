// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Foundation/MD_AsyncAction_PushScreenToLayer.h"

#include "CommonActivatableWidget.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UI/MD_LocalUIManager.h"
#include "Util/MD_LogUtility.h"

void UMD_AsyncAction_PushScreenToLayer::Activate()
{
	// Factory에서 저장한 Owning Player가 요청 실행 시점에도 유효한지 확인한다.
	APlayerController* ResolvedOwningPlayer = OwningPlayer.Get();
	if (IsValid(ResolvedOwningPlayer) == false)
	{
		FinishFailure(EMDUIPushFailure::InvalidOwningPlayer, NSLOCTEXT("MDCommonUI", "InvalidOwningPlayer", "유효한 Owning Player가 필요합니다."));
		return;
	}

	// 원격 Controller가 로컬 UI Stack을 조작하지 못하도록 요청을 거부한다.
	bool bIsLocalController = ResolvedOwningPlayer->IsLocalController();
	if (bIsLocalController == false)
	{
		FinishFailure(EMDUIPushFailure::NotLocalPlayer, NSLOCTEXT("MDCommonUI", "NotLocalPlayer", "로컬 PlayerController만 UI 화면을 열 수 있습니다."));
		return;
	}

	// Async proxy가 모든 종료 경로까지 유지되도록 플레이어의 GameInstance에 등록한다.
	UGameInstance* RegisteredGameInstance = RegisteredWithGameInstance.Get();
	if (IsValid(RegisteredGameInstance) == false)
	{
		RegisterWithGameInstance(ResolvedOwningPlayer);
	}

	// 요청한 Controller에 연결된 LocalPlayer를 조회한다.
	ULocalPlayer* OwningLocalPlayer = ResolvedOwningPlayer->GetLocalPlayer();
	if (IsValid(OwningLocalPlayer) == false)
	{
		FinishFailure(EMDUIPushFailure::NotLocalPlayer, NSLOCTEXT("MDCommonUI", "LocalPlayerUnavailable", "Owning Player에 연결된 LocalPlayer가 없습니다."));
		return;
	}

	// 해당 LocalPlayer의 중앙 UI Manager를 조회한다.
	UMD_LocalUIManager* ResolvedUIManager = OwningLocalPlayer->GetSubsystem<UMD_LocalUIManager>();
	if (IsValid(ResolvedUIManager) == false)
	{
		FinishFailure(EMDUIPushFailure::SubsystemUnavailable, NSLOCTEXT("MDCommonUI", "SubsystemUnavailable", "Local UI Manager를 찾을 수 없습니다."));
		return;
	}
	UIManager = ResolvedUIManager;

	// PrimaryLayout이 화면에 연결되기 전에 들어온 요청을 명확히 실패 처리한다.
	bool bIsLayoutReady = ResolvedUIManager->IsPrimaryLayoutReady();
	if (bIsLayoutReady == false)
	{
		FinishFailure(EMDUIPushFailure::LayoutNotReady, NSLOCTEXT("MDCommonUI", "LayoutNotReady", "PrimaryLayout이 아직 준비되지 않았습니다."));
		return;
	}

	// 등록되지 않은 GameplayTag가 비동기 로드까지 진행되지 않도록 조기에 거부한다.
	bool bIsLayerRegistered = ResolvedUIManager->IsLayerRegistered(LayerTag);
	if (bIsLayerRegistered == false)
	{
		FinishFailure(EMDUIPushFailure::UnknownLayer, NSLOCTEXT("MDCommonUI", "UnknownLayer", "요청한 UI Layer가 PrimaryLayout에 등록되지 않았습니다."));
		return;
	}

	// 비어 있거나 잘못된 Soft Class 경로를 Asset Manager에 전달하지 않는다.
	FSoftObjectPath ScreenClassPath = ScreenClass.ToSoftObjectPath();
	bool bIsScreenClassPathValid = ScreenClassPath.IsValid();
	if (bIsScreenClassPathValid == false)
	{
		FinishFailure(EMDUIPushFailure::InvalidScreenClass, NSLOCTEXT("MDCommonUI", "InvalidScreenClass", "유효한 CommonActivatableWidget Screen Class가 필요합니다."));
		return;
	}

	// 로드 완료 시 World가 바뀌었는지 비교할 시작 World를 약한 참조로 저장한다.
	UWorld* ResolvedRequestWorld = ResolvedOwningPlayer->GetWorld();
	if (IsValid(ResolvedRequestWorld) == false)
	{
		FinishFailure(EMDUIPushFailure::InvalidOwningPlayer, NSLOCTEXT("MDCommonUI", "OwningWorldUnavailable", "Owning Player의 World를 찾을 수 없습니다."));
		return;
	}
	RequestWorld = ResolvedRequestWorld;

	// World 포인터만으로 감지하기 어려운 전환도 폐기할 수 있도록 Manager의 현재 세대를 저장한다.
	RequestWorldGeneration = ResolvedUIManager->GetWorldGeneration();

	// 옵션이 활성화된 요청은 로드 중 PrimaryLayout의 Click과 탐색 입력을 차단한다.
	if (bSuspendInputUntilComplete == true)
	{
		ResolvedUIManager->SuspendInputForAsyncRequest();
		bInputSuspended = true;
	}

	// Asset Manager의 Streamable Manager를 통해 Soft Widget Class를 비동기로 로드한다.
	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	FStreamableDelegate LoadCompleteDelegate = FStreamableDelegate::CreateUObject(this, &UMD_AsyncAction_PushScreenToLayer::HandleScreenClassLoaded);
	StreamingHandle = StreamableManager.RequestAsyncLoad(ScreenClassPath, MoveTemp(LoadCompleteDelegate));
	if (StreamingHandle.IsValid() == false)
	{
		FinishFailure(EMDUIPushFailure::ClassLoadFailed, NSLOCTEXT("MDCommonUI", "ClassLoadRequestFailed", "Screen Class 비동기 로드 요청을 시작하지 못했습니다."));
	}
}

void UMD_AsyncAction_PushScreenToLayer::BeginDestroy()
{
	// 정상 종료 전에 파괴되는 경우 delegate 없이 로드와 입력 차단 자원만 정리한다.
	if (bFinished == false)
	{
		bFinished = true;
		CancelStreaming();
		ReleaseInputSuspension();
	}

	// 기반 Blueprint Async Action의 파괴 처리를 이어서 실행한다.
	Super::BeginDestroy();
}

void UMD_AsyncAction_PushScreenToLayer::Cancel()
{
	// 이미 성공, 실패 또는 취소된 요청은 종료 이벤트를 다시 발생시키지 않는다.
	if (bFinished == true)
	{
		return;
	}

	// 명시적 Cancel은 로드 작업과 입력 차단을 정리한 뒤 Canceled만 전달한다.
	FinishCanceled();
}

UMD_AsyncAction_PushScreenToLayer* UMD_AsyncAction_PushScreenToLayer::PushScreenToLayerAsync(
	APlayerController* InOwningPlayer,
	FGameplayTag Layer,
	TSoftClassPtr<UCommonActivatableWidget> InScreenClass,
	bool bInSuspendInputUntilComplete,
	FName InRequestKey)
{
	// Blueprint가 delegate를 연결한 뒤 Activate할 수 있도록 Async proxy를 생성한다.
	UMD_AsyncAction_PushScreenToLayer* AsyncRequest = NewObject<UMD_AsyncAction_PushScreenToLayer>();

	// 실행 시점 검증과 비동기 로드에 사용할 요청 값을 proxy에 저장한다.
	AsyncRequest->OwningPlayer = InOwningPlayer;
	AsyncRequest->LayerTag = Layer;
	AsyncRequest->ScreenClass = InScreenClass;
	AsyncRequest->bSuspendInputUntilComplete = bInSuspendInputUntilComplete;
	AsyncRequest->RequestKey = InRequestKey;

	// 유효한 플레이어 요청은 Activate 이전부터 GameInstance가 proxy 수명을 보장하도록 등록한다.
	if (IsValid(InOwningPlayer) == true)
	{
		AsyncRequest->RegisterWithGameInstance(InOwningPlayer);
	}

	// 생성된 proxy를 Blueprint Async 노드에 반환한다.
	return AsyncRequest;
}

void UMD_AsyncAction_PushScreenToLayer::HandleScreenClassLoaded()
{
	// Cancel과 로드 완료가 같은 프레임에 경쟁해도 완료 경로를 다시 실행하지 않는다.
	if (bFinished == true)
	{
		return;
	}

	// 로드 중 제거된 LocalPlayer 요청은 화면을 생성하지 않고 취소 처리한다.
	APlayerController* ResolvedOwningPlayer = OwningPlayer.Get();
	if (IsValid(ResolvedOwningPlayer) == false)
	{
		FinishCanceled();
		return;
	}

	// Travel로 World가 교체된 요청은 새 World의 UI Stack에 적용하지 않는다.
	UWorld* InitialRequestWorld = RequestWorld.Get();
	if (IsValid(InitialRequestWorld) == false)
	{
		FinishFailure(EMDUIPushFailure::StaleRequest, NSLOCTEXT("MDCommonUI", "StaleRequestWorld", "요청을 시작한 World가 더 이상 유효하지 않습니다."));
		return;
	}

	// 현재 Controller의 World를 요청 시작 시 저장한 World와 비교한다.
	UWorld* CurrentPlayerWorld = ResolvedOwningPlayer->GetWorld();
	if (IsValid(CurrentPlayerWorld) == false)
	{
		FinishFailure(EMDUIPushFailure::StaleRequest, NSLOCTEXT("MDCommonUI", "CurrentWorldUnavailable", "현재 Player World가 유효하지 않습니다."));
		return;
	}
	if (CurrentPlayerWorld != InitialRequestWorld)
	{
		FinishFailure(EMDUIPushFailure::StaleRequest, NSLOCTEXT("MDCommonUI", "WorldChangedDuringLoad", "Screen Class 로드 중 World가 변경되어 요청을 폐기했습니다."));
		return;
	}

	// 요청 시작 시 조회한 Local UI Manager가 여전히 유지되는지 확인한다.
	UMD_LocalUIManager* ResolvedUIManager = UIManager.Get();
	if (IsValid(ResolvedUIManager) == false)
	{
		FinishCanceled();
		return;
	}

	// 요청 시작 이후 World teardown이 발생했다면 새 World의 Stack에 이전 요청을 적용하지 않는다.
	bool bIsCurrentWorldGeneration = ResolvedUIManager->IsWorldGenerationCurrent(RequestWorldGeneration);
	if (bIsCurrentWorldGeneration == false)
	{
		FinishFailure(EMDUIPushFailure::StaleRequest, NSLOCTEXT("MDCommonUI", "WorldGenerationChangedDuringLoad", "Screen Class 로드 중 World 전환이 시작되어 요청을 폐기했습니다."));
		return;
	}

	// 로드 중 Layout이나 Layer가 제거된 경우 이전 화면 구조에 Push하지 않는다.
	bool bIsLayoutReady = ResolvedUIManager->IsPrimaryLayoutReady();
	if (bIsLayoutReady == false)
	{
		FinishFailure(EMDUIPushFailure::LayoutNotReady, NSLOCTEXT("MDCommonUI", "LayoutRemovedDuringLoad", "Screen Class 로드 중 PrimaryLayout이 제거되었습니다."));
		return;
	}
	bool bIsLayerRegistered = ResolvedUIManager->IsLayerRegistered(LayerTag);
	if (bIsLayerRegistered == false)
	{
		FinishFailure(EMDUIPushFailure::UnknownLayer, NSLOCTEXT("MDCommonUI", "LayerRemovedDuringLoad", "Screen Class 로드 중 대상 UI Layer가 제거되었습니다."));
		return;
	}

	// 완료된 Soft Class가 실제 UClass로 해석되었는지 확인한다.
	UClass* LoadedScreenClass = ScreenClass.Get();
	if (IsValid(LoadedScreenClass) == false)
	{
		FinishFailure(EMDUIPushFailure::ClassLoadFailed, NSLOCTEXT("MDCommonUI", "ClassLoadFailed", "Screen Class를 로드하지 못했습니다."));
		return;
	}

	// 잘못된 Soft Class가 Common UI Stack에 전달되지 않도록 기반 클래스를 검증한다.
	bool bIsActivatableWidgetClass = LoadedScreenClass->IsChildOf(UCommonActivatableWidget::StaticClass());
	if (bIsActivatableWidgetClass == false)
	{
		FinishFailure(
			EMDUIPushFailure::InvalidScreenClass,
			NSLOCTEXT("MDCommonUI", "NotActivatableWidgetClass", "Screen Class가 CommonActivatableWidget을 상속하지 않습니다."));
		return;
	}
	TSubclassOf<UCommonActivatableWidget> LoadedActivatableClass = LoadedScreenClass;

	// 같은 클래스가 레이어 최상단에 활성화되어 있으면 연속 클릭에 의한 중복 Push를 거부한다.
	UCommonActivatableWidget* ActiveScreen = ResolvedUIManager->GetActiveScreen(LayerTag);
	if (IsValid(ActiveScreen) == true)
	{
		UClass* ActiveScreenClass = ActiveScreen->GetClass();
		if (ActiveScreenClass == LoadedScreenClass)
		{
			FinishFailure(EMDUIPushFailure::DuplicateRejected, NSLOCTEXT("MDCommonUI", "DuplicateRejected", "같은 Screen Class가 이미 대상 Layer에서 활성화되어 있습니다."));
			return;
		}
	}

	// Async proxy가 활성화 전 초기화 콜백까지 살아 있는지 안전하게 확인할 약한 참조를 만든다.
	TWeakObjectPtr<UMD_AsyncAction_PushScreenToLayer> WeakRequest = this;

	// Stack의 초기화 callback에서 Before Push를 실행한 뒤 같은 호출 안에서 화면을 활성화한다.
	UCommonActivatableWidget* PushedScreen = ResolvedUIManager->PushScreenToLayerInitialized(
		LayerTag,
		LoadedActivatableClass,
		[WeakRequest](UCommonActivatableWidget& Screen)
		{
			UMD_AsyncAction_PushScreenToLayer* ResolvedRequest = WeakRequest.Get();
			if (IsValid(ResolvedRequest) == true)
			{
				ResolvedRequest->HandleBeforePush(Screen);
			}
		});

	// Before Push에서 요청이 취소됐다면 방금 추가된 화면을 즉시 표준 Deactivate 경로로 닫는다.
	if (bFinished == true)
	{
		if (IsValid(PushedScreen) == true)
		{
			PushedScreen->DeactivateWidget();
		}
		return;
	}

	// Stack이 화면을 만들지 못한 경우 활성화 성공 이벤트를 발생시키지 않는다.
	if (IsValid(PushedScreen) == false)
	{
		FinishFailure(EMDUIPushFailure::PushFailed, NSLOCTEXT("MDCommonUI", "PushFailed", "대상 UI Layer에 Screen을 Push하지 못했습니다."));
		return;
	}

	// 활성화 요청까지 완료된 화면을 성공 경로로 전달한다.
	FinishSuccess(PushedScreen);
}

void UMD_AsyncAction_PushScreenToLayer::HandleBeforePush(UCommonActivatableWidget& Screen)
{
	// 종료되지 않은 요청만 활성화 전 초기화 이벤트를 전달한다.
	if (bFinished == false)
	{
		BeforePush.Broadcast(&Screen);
	}
}

void UMD_AsyncAction_PushScreenToLayer::FinishSuccess(UCommonActivatableWidget* Screen)
{
	// 성공, 실패, 취소 중 먼저 도착한 경로만 종료 처리를 소유한다.
	if (bFinished == true)
	{
		return;
	}
	bFinished = true;

	// 완료된 로드 Handle 참조를 해제한 뒤 성공 화면을 전달한다.
	StreamingHandle.Reset();
	AfterPush.Broadcast(Screen);

	// 성공 callback 이후 입력을 복구하고 GameInstance의 proxy 참조를 해제한다.
	ReleaseInputSuspension();
	SetReadyToDestroy();
}

void UMD_AsyncAction_PushScreenToLayer::FinishFailure(EMDUIPushFailure Failure, FText Message)
{
	// 성공, 실패, 취소 중 먼저 도착한 경로만 종료 처리를 소유한다.
	if (bFinished == true)
	{
		return;
	}
	bFinished = true;

	// 실패한 요청의 비동기 로드와 화면 입력 차단을 정리한다.
	CancelStreaming();

	// 로그에서 플레이어, Layer, 클래스, 요청 키와 실패 메시지를 함께 확인할 수 있게 기록한다.
	APlayerController* ResolvedOwningPlayer = OwningPlayer.Get();
	FString OwningPlayerName = GetNameSafe(ResolvedOwningPlayer);
	FString LayerName = LayerTag.ToString();
	FSoftObjectPath ResolvedScreenClassPath = ScreenClass.ToSoftObjectPath();
	FString ScreenClassPath = ResolvedScreenClassPath.ToString();
	FString RequestKeyName = RequestKey.ToString();
	FString FailureMessage = Message.ToString();
	UE_LOG(
		MarineDemolition,
		Warning,
		TEXT("Async UI Push 실패: Player=%s Layer=%s Screen=%s RequestKey=%s Reason=%s"),
		*OwningPlayerName,
		*LayerName,
		*ScreenClassPath,
		*RequestKeyName,
		*FailureMessage);

	// Blueprint 실패 처리 이후 입력을 복구하고 proxy 수명을 종료한다.
	Failed.Broadcast(Failure, Message);
	ReleaseInputSuspension();
	SetReadyToDestroy();
}

void UMD_AsyncAction_PushScreenToLayer::FinishCanceled()
{
	// 성공, 실패, 취소 중 먼저 도착한 경로만 종료 처리를 소유한다.
	if (bFinished == true)
	{
		return;
	}
	bFinished = true;

	// 취소된 요청의 로드 Handle을 중단한 뒤 Blueprint에 요청 키를 전달한다.
	CancelStreaming();
	Canceled.Broadcast(RequestKey);

	// 취소 callback 이후 입력을 복구하고 proxy 수명을 종료한다.
	ReleaseInputSuspension();
	SetReadyToDestroy();
}

void UMD_AsyncAction_PushScreenToLayer::CancelStreaming()
{
	// 유효한 비동기 로드만 취소하여 완료 delegate가 뒤늦게 실행되지 않도록 한다.
	if (StreamingHandle.IsValid() == true)
	{
		StreamingHandle->CancelHandle();
		StreamingHandle.Reset();
	}
}

void UMD_AsyncAction_PushScreenToLayer::ReleaseInputSuspension()
{
	// 입력을 차단하지 않았거나 이미 복구한 요청은 Manager 카운트를 변경하지 않는다.
	if (bInputSuspended == false)
	{
		return;
	}
	bInputSuspended = false;

	// 요청을 시작한 Local UI Manager가 유지되는 경우에만 입력 차단을 해제한다.
	UMD_LocalUIManager* ResolvedUIManager = UIManager.Get();
	if (IsValid(ResolvedUIManager) == true)
	{
		ResolvedUIManager->ResumeInputForAsyncRequest();
	}
}
