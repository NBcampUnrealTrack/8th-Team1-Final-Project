// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Foundation/MD_AsyncAction_PushScreenToLayer.h"

#include "CommonActivatableWidget.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UI/MD_LocalUIManager.h"
#include "Util/MD_LogUtility.h"

void UMD_AsyncAction_PushScreenToLayer::Activate()
{
	// 활성화 검증 과정에서 사용할 객체를 조건부로 조회한다.
	ULocalPlayer* OwningLocalPlayer = OwningPlayer.IsValid() == true ? OwningPlayer->GetLocalPlayer() : nullptr;
	UMD_LocalUIManager* ResolvedUIManager = IsValid(OwningLocalPlayer) == true ? OwningLocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	FSoftObjectPath ScreenClassPath = ScreenClass.ToSoftObjectPath();
	UWorld* ResolvedRequestWorld = OwningPlayer.IsValid() == true ? OwningPlayer->GetWorld() : nullptr;

	// Factory에서 저장한 Player가 유효한 로컬 Controller인지 함께 확인한다.
	if (OwningPlayer.IsValid() == false || OwningPlayer->IsLocalController() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("PushScreenToLayerAsync에는 유효한 Local Owning Player가 필요합니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 초기화 과정에서 조회한 LocalPlayer가 유효한지 확인한다.
	if (IsValid(OwningLocalPlayer) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("PushScreenToLayerAsync에는 유효한 LocalPlayer가 필요합니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 초기화 과정에서 조회한 Local UI Manager가 유효한지 확인한다.
	if (IsValid(ResolvedUIManager) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("LocalPlayer에 MD_LocalUIManager Subsystem이 생성되지 않았습니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 유효한 Manager의 PrimaryLayout 준비 상태와 Layer 등록 상태를 함께 검증한다.
	if (ResolvedUIManager->IsPrimaryLayoutReady() == false || ResolvedUIManager->IsLayerRegistered(LayerTag) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("PrimaryLayout이 준비되고 요청한 UI Layer가 등록되어야 합니다. Layer=%s"), *LayerTag.ToString());
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 비어 있거나 잘못된 Soft Class 경로를 Asset Manager에 전달하지 않는다.
	if (ScreenClassPath.IsValid() == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("PushScreenToLayerAsync에는 유효한 CommonActivatableWidget Screen Class가 필요합니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 초기화 과정에서 조회한 요청 시작 World가 유효한지 확인한다.
	if (IsValid(ResolvedRequestWorld) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("Owning Player에는 UI 요청을 시작할 유효한 World가 필요합니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// Async proxy가 모든 종료 경로까지 유지되도록 플레이어의 GameInstance에 등록한다.
	UGameInstance* RegisteredGameInstance = RegisteredWithGameInstance.Get();
	if (IsValid(RegisteredGameInstance) == false)
	{
		RegisterWithGameInstance(OwningPlayer.Get());
	}

	// 검증한 Manager와 World를 비동기 완료 시점의 재검사에 사용하도록 저장한다.
	UIManager = ResolvedUIManager;
	RequestWorld = ResolvedRequestWorld;

	// World 포인터만으로 감지하기 어려운 전환도 폐기할 수 있도록 Manager의 현재 세대를 저장한다.
	RequestWorldGeneration = ResolvedUIManager->GetWorldGeneration();

	// 옵션이 활성화된 요청은 로드 중 PrimaryLayout의 Click과 탐색 입력을 차단한다.
	if (bBlockInputDuringPush == true)
	{
		ResolvedUIManager->BlockInputDuringPush();
		bInputBlockedDuringPush = true;
	}

	// Asset Manager의 Streamable Manager를 통해 Soft Widget Class를 비동기로 로드한다.
	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	FStreamableDelegate LoadCompleteDelegate = FStreamableDelegate::CreateUObject(this, &UMD_AsyncAction_PushScreenToLayer::HandleScreenClassLoaded);
	StreamingHandle = StreamableManager.RequestAsyncLoad(ScreenClassPath, MoveTemp(LoadCompleteDelegate));
	if (StreamingHandle.IsValid() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("Screen Class 비동기 로드 요청을 시작하지 못했습니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
	}
}

void UMD_AsyncAction_PushScreenToLayer::BeginDestroy()
{
	// 정상 종료 전에 파괴되는 경우 delegate 없이 로드와 입력 차단 자원만 정리한다.
	if (bFinished == false)
	{
		bFinished = true;
		CancelStreaming();
		ReleaseInputBlock();
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
	FinishRequest(EMDAsyncScreenPushResult::Canceled);
}

UMD_AsyncAction_PushScreenToLayer* UMD_AsyncAction_PushScreenToLayer::PushScreenToLayerAsync(
	APlayerController* InOwningPlayer,
	FGameplayTag Layer,
	TSoftClassPtr<UCommonActivatableWidget> InScreenClass,
	bool bBlockInputDuringPush,
	FName CancellationKey)
{
	// Blueprint가 delegate를 연결한 뒤 Activate할 수 있도록 Async proxy를 생성한다.
	UMD_AsyncAction_PushScreenToLayer* AsyncRequest = NewObject<UMD_AsyncAction_PushScreenToLayer>();

	// 실행 시점 검증과 비동기 로드에 사용할 요청 값을 proxy에 저장한다.
	AsyncRequest->OwningPlayer = InOwningPlayer;
	AsyncRequest->LayerTag = Layer;
	AsyncRequest->ScreenClass = InScreenClass;
	AsyncRequest->bBlockInputDuringPush = bBlockInputDuringPush;
	AsyncRequest->CancellationKey = CancellationKey;

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
		FinishRequest(EMDAsyncScreenPushResult::Canceled);
		return;
	}

	// Travel로 World가 교체된 요청은 새 World의 UI Stack에 적용하지 않는다.
	UWorld* InitialRequestWorld = RequestWorld.Get();
	if (IsValid(InitialRequestWorld) == false)
	{
		FinishRequest(EMDAsyncScreenPushResult::Canceled);
		return;
	}

	// 현재 Controller의 World를 요청 시작 시 저장한 World와 비교한다.
	UWorld* CurrentPlayerWorld = ResolvedOwningPlayer->GetWorld();
	if (IsValid(CurrentPlayerWorld) == false || CurrentPlayerWorld != InitialRequestWorld)
	{
		FinishRequest(EMDAsyncScreenPushResult::Canceled);
		return;
	}

	// 요청 시작 시 조회한 Local UI Manager가 여전히 유지되는지 확인한다.
	UMD_LocalUIManager* ResolvedUIManager = UIManager.Get();
	if (IsValid(ResolvedUIManager) == false)
	{
		FinishRequest(EMDAsyncScreenPushResult::Canceled);
		return;
	}

	// World 전환이나 Layout 구성 변경이 발생한 요청을 한 번에 취소한다.
	if (ResolvedUIManager->IsWorldGenerationCurrent(RequestWorldGeneration) == false
		|| ResolvedUIManager->IsPrimaryLayoutReady() == false
		|| ResolvedUIManager->IsLayerRegistered(LayerTag) == false)
	{
		FinishRequest(EMDAsyncScreenPushResult::Canceled);
		return;
	}

	// 비동기 로드가 끝난 Soft Class 포인터의 유효성을 직접 확인한다.
	if (ScreenClass.IsValid() == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("Screen Class를 로드하지 못했습니다."));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 유효성 검사를 통과한 Soft Class에서 로드된 UClass를 가져온다.
	UClass* LoadedScreenClass = ScreenClass.Get();

	// 잘못된 Soft Class가 Common UI Stack에 전달되지 않도록 기반 클래스를 검증한다.
	if (LoadedScreenClass->IsChildOf(UCommonActivatableWidget::StaticClass()) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("Screen Class는 CommonActivatableWidget을 상속해야 합니다. Screen=%s"), *GetNameSafe(LoadedScreenClass));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}
	TSubclassOf<UCommonActivatableWidget> LoadedActivatableClass = LoadedScreenClass;

	// 같은 클래스가 레이어 최상단에 활성화되어 있으면 연속 클릭에 의한 중복 Push를 거부한다.
	UCommonActivatableWidget* ActiveScreen = ResolvedUIManager->GetActiveScreen(LayerTag);
	if (IsValid(ActiveScreen) == true && ActiveScreen->GetClass() == LoadedScreenClass)
	{
		FinishRequest(EMDAsyncScreenPushResult::Canceled);
		return;
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

	// 등록된 Stack이 화면을 정상적으로 생성했는지 검증한다.
	if (IsValid(PushedScreen) == false)
	{
		UE_LOG(MarineDemolition, Error, TEXT("등록된 UI Layer에 Screen을 Push하지 못했습니다. Layer=%s Screen=%s"), *LayerTag.ToString(), *GetNameSafe(LoadedScreenClass));
		FinishRequest(EMDAsyncScreenPushResult::Failure);
		return;
	}

	// 활성화 요청까지 완료된 화면을 성공 경로로 전달한다.
	FinishRequest(EMDAsyncScreenPushResult::Success, PushedScreen);
}

void UMD_AsyncAction_PushScreenToLayer::HandleBeforePush(UCommonActivatableWidget& Screen)
{
	// 종료되지 않은 요청만 활성화 전 초기화 이벤트를 전달한다.
	if (bFinished == false)
	{
		BeforePush.Broadcast(&Screen);
	}
}

void UMD_AsyncAction_PushScreenToLayer::FinishRequest(EMDAsyncScreenPushResult Result, UCommonActivatableWidget* Screen)
{
	// 성공, 실패, 취소 중 먼저 도착한 경로만 종료 처리를 소유한다.
	if (bFinished == true)
	{
		return;
	}
	bFinished = true;

	// 요청 결과에 맞는 비동기 자원 정리와 Blueprint 이벤트를 수행한다.
	switch (Result)
	{
	case EMDAsyncScreenPushResult::Success:
		// 완료된 로드 Handle 참조를 해제한 뒤 성공 화면을 전달한다.
		StreamingHandle.Reset();
		AfterPush.Broadcast(Screen);
		break;

	case EMDAsyncScreenPushResult::Failure:
		// 실패한 요청의 비동기 로드를 정리하고 Blueprint에 실패 신호를 전달한다.
		CancelStreaming();
		Failed.Broadcast();
		break;

	case EMDAsyncScreenPushResult::Canceled:
		// 취소된 요청의 로드를 중단한 뒤 식별 키를 전달한다.
		CancelStreaming();
		Canceled.Broadcast(CancellationKey);
		break;

	default:
		// 정의되지 않은 종료 결과를 프로젝트 로그 카테고리에 기록한다.
		UE_LOG(MarineDemolition, Error, TEXT("정의되지 않은 Async Screen Push 종료 결과입니다. Result=%d"), static_cast<int32>(Result));
		break;
	}

	// 결과 이벤트 처리 이후 입력을 복구하고 proxy 수명을 종료한다.
	ReleaseInputBlock();
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

void UMD_AsyncAction_PushScreenToLayer::ReleaseInputBlock()
{
	// 입력을 차단하지 않았거나 이미 복구한 요청은 Manager 카운트를 변경하지 않는다.
	if (bInputBlockedDuringPush == false)
	{
		return;
	}
	bInputBlockedDuringPush = false;

	// 요청을 시작한 Local UI Manager가 유지되는 경우에만 입력 차단을 해제한다.
	UMD_LocalUIManager* ResolvedUIManager = UIManager.Get();
	if (IsValid(ResolvedUIManager) == true)
	{
		ResolvedUIManager->UnblockInputAfterPush();
	}
}
