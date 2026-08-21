// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "MD_AsyncAction_PushScreenToLayer.generated.h"

class UCommonActivatableWidget;
class UMD_LocalUIManager;
struct FStreamableHandle;

/** Async 화면 Push 요청이 실패한 원인을 Blueprint에 전달한다. */
UENUM(BlueprintType)
enum class EMDUIPushFailure : uint8
{
	InvalidOwningPlayer UMETA(DisplayName = "Invalid Owning Player"),
	NotLocalPlayer UMETA(DisplayName = "Not Local Player"),
	SubsystemUnavailable UMETA(DisplayName = "Subsystem Unavailable"),
	LayoutNotReady UMETA(DisplayName = "Layout Not Ready"),
	UnknownLayer UMETA(DisplayName = "Unknown Layer"),
	InvalidScreenClass UMETA(DisplayName = "Invalid Screen Class"),
	ClassLoadFailed UMETA(DisplayName = "Class Load Failed"),
	DuplicateRejected UMETA(DisplayName = "Duplicate Rejected"),
	PushFailed UMETA(DisplayName = "Push Failed"),
	StaleRequest UMETA(DisplayName = "Stale Request"),
	Canceled UMETA(DisplayName = "Canceled")
};

// 활성화 전과 Push 완료 시 생성된 화면을 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDAsyncScreenPushDelegate, UCommonActivatableWidget*, Screen);

// 실패 코드와 아티스트가 확인할 수 있는 메시지를 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMDAsyncScreenPushFailedDelegate, EMDUIPushFailure, Failure, FText, Message);

// 취소된 요청을 구분할 수 있도록 요청 키를 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDAsyncScreenPushCanceledDelegate, FName, RequestKey);

/** Soft 화면 클래스를 로드하고 LocalPlayer의 Common UI 레이어에 Push하는 취소 가능 Async Action이다. */
UCLASS(meta = (ExposedAsyncProxy = "AsyncRequest"))
class MARINEDEMOLITION_API UMD_AsyncAction_PushScreenToLayer : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	virtual void Activate() override;

	virtual void BeginDestroy() override;

public:
	// Stack이 화면을 활성화하기 전에 초기 데이터를 주입하는 이벤트다.
	UPROPERTY(BlueprintAssignable)
	FMDAsyncScreenPushDelegate BeforePush;

	// Stack Push와 활성화 요청이 완료된 화면을 전달하는 성공 이벤트다.
	UPROPERTY(BlueprintAssignable)
	FMDAsyncScreenPushDelegate AfterPush;

	// 검증, 로드 또는 Push 실패를 명확한 코드와 메시지로 전달한다.
	UPROPERTY(BlueprintAssignable)
	FMDAsyncScreenPushFailedDelegate Failed;

	// 명시적 취소나 LocalPlayer 제거로 종료된 요청을 전달한다.
	UPROPERTY(BlueprintAssignable)
	FMDAsyncScreenPushCanceledDelegate Canceled;

protected:
	// 요청한 LocalPlayer의 Controller를 World 수명에 영향을 주지 않는 약한 참조로 보관한다.
	TWeakObjectPtr<APlayerController> OwningPlayer;

	// 입력 차단과 Stack Push를 담당할 LocalPlayer UI Manager를 약한 참조로 보관한다.
	TWeakObjectPtr<UMD_LocalUIManager> UIManager;

	// 요청이 시작된 World를 저장하여 Travel 후 도착한 완료 콜백을 폐기한다.
	TWeakObjectPtr<UWorld> RequestWorld;

	// 요청이 시작된 UI World 세대를 저장하여 같은 World 객체를 재사용하는 전환도 감지한다.
	uint32 RequestWorldGeneration = 0;

	// 목적지 Common UI Stack을 선택할 표준 UI Layer 태그를 보관한다.
	FGameplayTag LayerTag;

	// Asset Manager가 비동기로 로드할 Activatable Widget 소프트 클래스를 보관한다.
	TSoftClassPtr<UCommonActivatableWidget> ScreenClass;

	// 취소 결과에서 논리적 요청을 식별할 선택적 키를 보관한다.
	FName RequestKey;

	// 비동기 로드가 끝날 때까지 PrimaryLayout 입력을 차단할지 보관한다.
	bool bSuspendInputUntilComplete = true;

	// Manager에 등록한 입력 차단을 정확히 한 번 복구하기 위해 상태를 보관한다.
	bool bInputSuspended = false;

	// 성공, 실패, 취소 중 하나만 한 번 발생하도록 종료 상태를 보관한다.
	bool bFinished = false;

	// 비동기 로드 취소와 완료 시 자원 해제에 사용할 Streamable Handle을 보관한다.
	TSharedPtr<FStreamableHandle> StreamingHandle;

public:
	// 지정한 플레이어의 Layer에 Soft Screen Class를 비동기로 Push하는 Blueprint 노드를 생성한다.
	UFUNCTION(
		BlueprintCallable,
		Category = "MD | UI | Navigation",
		meta = (BlueprintInternalUseOnly = "true", DisplayName = "Push UI Screen to Layer (Async)", GameplayTagFilter = "UI.Layer", AdvancedDisplay = "RequestKey"))
	static UMD_AsyncAction_PushScreenToLayer* PushScreenToLayerAsync(
		APlayerController* OwningPlayer,
		FGameplayTag Layer,
		TSoftClassPtr<UCommonActivatableWidget> ScreenClass,
		bool bSuspendInputUntilComplete = true,
		FName RequestKey = NAME_None);

	// 진행 중인 비동기 로드를 중단하고 Canceled 이벤트로 요청을 종료한다.
	UFUNCTION(BlueprintCallable, Category = "MD | UI | Navigation")
	void Cancel();

protected:
	// 로드 완료 후 요청 유효성을 다시 검사하고 활성화 전 초기화와 Stack Push를 수행한다.
	void HandleScreenClassLoaded();

	// Stack이 화면을 활성화하기 전에 Before Push 이벤트를 안전하게 전달한다.
	void HandleBeforePush(UCommonActivatableWidget& Screen);

	// 성공 이벤트를 전달하고 입력과 비동기 자원을 정리한다.
	void FinishSuccess(UCommonActivatableWidget* Screen);

	// 실패 이벤트를 한 번 전달하고 입력과 비동기 자원을 정리한다.
	void FinishFailure(EMDUIPushFailure Failure, FText Message);

	// 취소 이벤트를 한 번 전달하고 입력과 비동기 자원을 정리한다.
	void FinishCanceled();

	// 요청이 보유한 Streamable Handle을 취소하고 해제한다.
	void CancelStreaming();

	// Manager에 등록한 UI 입력 차단을 정확히 한 번 해제한다.
	void ReleaseInputSuspension();
};
