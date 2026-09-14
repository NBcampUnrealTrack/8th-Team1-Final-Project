#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/UI/MD_LoadingScreenTypes.h"
#include "MD_LoadingScreenSubsystem.generated.h"

class APlayerController;
class UGameViewportClient;
class SWidget;
class UFont;
class UTexture2D;
struct FWorldContext;

/**
 * 세션 작업부터 새 월드 준비까지 하나의 끊김 없는 로딩 전환을 관리한다.
 * 화면을 그리는 일은 SMD_LoadingScreenWidget이 하고, 이 클래스는 그 위젯을 언제 붙이고
 * 언제 걷을지만 결정한다.
 *
 * 전체 흐름:
 *   BeginLoadingScreen  → 인트로 0.90초로 화면을 덮음
 *   RunAfterIntro       → 덮인 뒤에야 실제 트래블 실행
 *   PreLoadMap          → MoviePlayer가 전용 스레드로 블로킹 구간을 그림
 *   PostLoadMap         → viewport 오버레이가 다시 이어받고 준비 판정 시작
 *   준비 완료/타임아웃  → COMPLETE 0.35초 → outro 0.45초 fade-out
 *
 * GameInstance 스코프인 이유는 맵이 바뀌어도 살아 있어야 하기 때문이다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_LoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 표시할 문구는 상황에서 정해진다. 문구 자체는 서브시스템 안에 한 벌만 있다. */
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Loading")
	void BeginLoadingScreen(EMD_LoadingContext Context);

	/** 트래블이 일어나지 않게 된 경우(세션 실패 등) COMPLETE 없이 즉시 걷어낸다. */
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Loading")
	void CancelLoadingScreen();

	/**
	 * 로컬 PlayerController가 플레이 가능 상태가 됐음을 알린다. 준비 판정을 앞당길 뿐
	 * 종료를 확정하지는 않는다. 스트리밍까지 확인한 뒤 TryFinishAfterWorldLoad가 결정한다.
	 */
	void NotifyPlayerReady(APlayerController* PlayerController);
	/** Refresh the fully covered view during the initial map handoff. */
	bool UpdateCoveredWorldRendering();

	/** outro는 이미 걷히는 중이므로 활성으로 보지 않는다. */
	UFUNCTION(BlueprintPure, Category = "MD|UI|Loading")
	bool IsLoadingScreenActive() const
	{
		return Phase == EMD_LoadingPhase::Intro
			|| Phase == EMD_LoadingPhase::Loading
			|| Phase == EMD_LoadingPhase::Completion;
	}

	/** 화면을 완전히 덮는 인트로가 끝난 뒤에만 트래블을 실행한다. */
	void RunAfterIntro(TFunction<void()>&& Callback);

private:
	using FTimerCallback = void (UMD_LoadingScreenSubsystem::*)();

	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void StartWorldReadyPolling(UWorld* TimerWorld);
	void StopWorldReadyPolling();
	/** None이면 로딩을 닫아도 된다. 그 외 값은 아직 막고 있는 원인이며 타임아웃 로그에 쓴다. */
	EMD_LoadingReadinessBlocker CheckWorldReadiness() const;
	void TryFinishAfterWorldLoad();
	void RestoreWorldRendering();
	void FinishLoadingScreen(bool bShowCompletion = true);
	void BeginOutro();
	void StartLoadingVisuals(EMD_LoadingContext Context, bool bSkipIntro);
	void SetLoadingTimer(FTimerHandle& Handle, FTimerCallback Callback, double Seconds, UWorld* InWorld = nullptr);
	void ClearLoadingTimers();
	TSharedRef<SWidget> CreateLoadingWidget();
	void EnsureViewportLoadingWidget();
	void DetachViewportLoadingWidget();
	void ConfigureMoviePlayer();
	void FlushPendingIntroCallbacks();
	void RemoveLoadingWidgets();
	void HandleIntroAnimationFinished();
	// Common UI의 기존 Transition 화면이 인트로를 재생 중인지 확인한다.
	bool IsCommonTransitionScreenActive() const;

	UFUNCTION()
	void HandleSessionOperationComplete(bool bWasSuccessful);

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ShipTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BeaconTexture;
	UPROPERTY(Transient)
	TObjectPtr<UFont> LoadingFont;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BackgroundTexture;

	FTimerHandle IntroTimerHandle;
	FTimerHandle WorldReadyTimerHandle;
	FTimerHandle CompletionTimerHandle;
	FTimerHandle OutroTimerHandle;
	TArray<TFunction<void()>> PendingIntroCallbacks;
	TSharedPtr<FMD_LoadingVisualState> VisualState;
	TSharedPtr<SWidget> ViewportLoadingWidget;
	TSharedPtr<SWidget> MoviePlayerLoadingWidget;

	/** 연출 진행 단계. 이 값 하나가 예전의 진행 플래그 네 개를 대신한다. */
	EMD_LoadingPhase Phase = EMD_LoadingPhase::Inactive;
	/** 인트로 도중에 들어온 종료 요청. HandleIntroAnimationFinished가 소비한다. */
	EMD_LoadingFinishRequest PendingFinish = EMD_LoadingFinishRequest::None;
	/** 위젯은 재사용하되 부착 여부는 따로 본다(하드 트래블 후 overlay slot이 비워진다). */
	bool bViewportWidgetAttached = false;
	int32 ReadyStablePollCount = 0;
	// Own only a viewport that this loading screen paused; preserve a pre-existing disabled state.
	TWeakObjectPtr<UGameViewportClient> PausedWorldViewport;
	uint64 WorldRenderingResumedFrame = 0;
	/** 준비 판정 폴링을 시작한 시각. 0 이하이면 폴링 중이 아니다. */
	double ReadinessWaitStartSeconds = 0.0;
};
