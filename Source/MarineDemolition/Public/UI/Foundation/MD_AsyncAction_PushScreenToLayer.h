// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "MD_AsyncAction_PushScreenToLayer.generated.h"

class UCommonActivatableWidget;
class UMD_LocalUIManager;
struct FStreamableHandle;

// Async 화면 Push 요청의 단일 종료 경로에서 전달할 결과를 구분한다.
enum class EMDAsyncScreenPushResult : uint8
{
	// 화면 Push가 완료되어 성공 이벤트를 전달해야 하는 결과다.
	Success,

	// 런타임 실패 정보와 실패 이벤트를 전달해야 하는 결과다.
	Failure,

	// 요청 식별 키와 취소 이벤트를 전달해야 하는 결과다.
	Canceled
};

// 활성화 전과 Push 완료 시 생성된 화면을 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDAsyncScreenPushDelegate, UCommonActivatableWidget*, Screen);

// 화면 Push 실패를 Blueprint에 신호로 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMDAsyncScreenPushFailedDelegate);

// 취소된 요청을 구분할 수 있도록 요청 키를 전달한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMDAsyncScreenPushCanceledDelegate, FName, CancellationKey);

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

	// 로그에 기록된 화면 Push 실패가 발생했음을 전달한다.
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

	// Asset Manager가 비동기로 로드할 Activatable Widget 소프트 클래스를 보관한다.
	TSoftClassPtr<UCommonActivatableWidget> ScreenClass;

	// 취소 결과에서 논리적 요청을 식별할 선택적 키를 보관한다.
	FName CancellationKey;

	// 비동기 로드가 끝날 때까지 PrimaryLayout 입력을 차단할지 보관한다.
	bool bBlockInputDuringPush = true;

	// Manager에 등록한 입력 차단을 정확히 한 번 복구하기 위해 상태를 보관한다.
	bool bInputBlockedDuringPush = false;

	// 성공, 실패, 취소 중 하나만 한 번 발생하도록 종료 상태를 보관한다.
	bool bFinished = false;

	// 비동기 로드 취소와 완료 시 자원 해제에 사용할 Streamable Handle을 보관한다.
	TSharedPtr<FStreamableHandle> StreamingHandle;

public:
	/**
	 * 지정한 플레이어의 UISettings Layer에 Soft Screen Class를 비동기로 Push하는 Blueprint 노드를 생성한다.
	 *
	 * @param OwningPlayer UI 화면을 소유하며 대상 LocalPlayer와 World를 결정할 로컬 PlayerController다.
	 * @param ScreenClass 비동기로 로드한 뒤 설정된 Layer에 생성할 CommonActivatableWidget 소프트 클래스다.
	 * @param bBlockInputDuringPush true이면 클래스 로드와 Push가 끝날 때까지 PrimaryLayout 입력을 차단한다.
	 * @param CancellationKey 취소된 요청을 Blueprint에서 식별할 때 사용하는 선택적 키다.
	 * @return 성공, 실패, 취소 및 활성화 전 초기화 이벤트를 제공하는 Async Action proxy다.
	 */
	UFUNCTION(
		BlueprintCallable,
		Category = "MD | UI | Navigation",
		meta = (BlueprintInternalUseOnly = "true", DisplayName = "Push UI Screen to Layer (Async)", AdvancedDisplay = "CancellationKey"))
	static UMD_AsyncAction_PushScreenToLayer* PushScreenToLayerAsync(
		APlayerController* OwningPlayer,
		TSoftClassPtr<UCommonActivatableWidget> ScreenClass,
		bool bBlockInputDuringPush = true,
		FName CancellationKey = NAME_None);

	// 진행 중인 비동기 로드를 중단하고 Canceled 이벤트로 요청을 종료한다.
	UFUNCTION(BlueprintCallable, Category = "MD | UI | Navigation")
	void Cancel();

protected:
	// 로드 완료 후 요청 유효성을 다시 검사하고 활성화 전 초기화와 Stack Push를 수행한다.
	void HandleScreenClassLoaded();

	// Stack이 화면을 활성화하기 전에 Before Push 이벤트를 안전하게 전달한다.
	void HandleBeforePush(UCommonActivatableWidget& Screen);

	// 결과에 맞는 이벤트를 전달하고 입력과 비동기 자원을 하나의 경로에서 정리한다.
	void FinishRequest(EMDAsyncScreenPushResult Result, UCommonActivatableWidget* Screen = nullptr);

	// 요청이 보유한 Streamable Handle을 취소하고 해제한다.
	void CancelStreaming();

	// Manager에 등록한 UI 입력 차단을 정확히 한 번 해제한다.
	void ReleaseInputBlock();
};
