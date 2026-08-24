#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MD_LoadingScreenSubsystem.generated.h"

class APlayerController;
class SWidget;
class UTexture2D;
struct FWorldContext;

namespace MD_LoadingScreenPrivate
{
	struct FLoadingVisualState;
}

/**
 * 세션 작업부터 새 월드 준비까지 하나의 끊김 없는 로딩 전환을 관리한다.
 *
 * 전체 흐름:
 *   BeginLoadingScreen  → 인트로 0.90초로 화면을 덮음
 *   RunAfterIntro       → 덮인 뒤에야 실제 트래블 실행
 *   PreLoadMap          → MoviePlayer가 전용 스레드로 블로킹 구간을 그림
 *   PostLoadMap         → viewport 오버레이가 다시 이어받고 준비 판정 시작
 *   준비 완료/타임아웃  → COMPLETE 0.85초 → outro 1.10초 fade-out
 *
 * GameInstance 스코프인 이유는 맵이 바뀌어도 살아 있어야 하기 때문이고,
 * 화면을 UMG가 아닌 Slate로 그리는 이유는 하드 트래블에서 UMG가 월드와 함께
 * 파괴되는 데다 MoviePlayer가 Slate만 직접 그릴 수 있기 때문이다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_LoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "MD|UI|Loading")
	void BeginLoadingScreen(const FText& Subtitle);

	/** 트래블이 일어나지 않게 된 경우(세션 실패 등) COMPLETE 없이 즉시 걷어낸다. */
	UFUNCTION(BlueprintCallable, Category = "MD|UI|Loading")
	void CancelLoadingScreen();

	/**
	 * 로컬 PlayerController가 플레이 가능 상태가 됐음을 알린다.
	 * 준비 판정을 앞당길 뿐 종료를 확정하지는 않는다. 스트리밍까지 확인한 뒤
	 * TryFinishAfterWorldLoad가 최종 결정한다.
	 */
	void NotifyPlayerReady(APlayerController* PlayerController);

	UFUNCTION(BlueprintPure, Category = "MD|UI|Loading")
	bool IsLoadingScreenActive() const { return bLoadingRequested; }

	/** 화면을 완전히 덮는 인트로가 끝난 뒤에만 트래블을 실행한다. */
	void RunAfterIntro(TFunction<void()>&& Callback);

private:
	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void StartWorldReadyPolling(UWorld* TimerWorld);
	/** OutBlockingReason이 nullptr이면 사유 문자열을 만들지 않는 빠른 경로로 동작한다. */
	bool IsWorldReadyToFinish(FString* OutBlockingReason) const;
	void TryFinishAfterWorldLoad();
	void FinishLoadingScreen(bool bShowCompletion = true);
	void BeginOutro();
	void EnsureViewportLoadingWidget();
	void ConfigureMoviePlayer();
	void FlushPendingIntroCallbacks();
	void RemoveLoadingWidgets();
	void HandleIntroAnimationFinished();
	void HandleCompletionDisplayFinished();
	void HandleOutroAnimationFinished();

	UFUNCTION()
	void HandleSessionOperationComplete(bool bWasSuccessful);

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BubbleTexture;

	FText CurrentSubtitle;
	FTimerHandle IntroTimerHandle;
	FTimerHandle WorldReadyTimerHandle;
	FTimerHandle CompletionTimerHandle;
	FTimerHandle OutroTimerHandle;
	TArray<TFunction<void()>> PendingIntroCallbacks;
	TSharedPtr<MD_LoadingScreenPrivate::FLoadingVisualState> VisualState;
	TSharedPtr<SWidget> ViewportLoadingWidget;
	TSharedPtr<SWidget> MoviePlayerLoadingWidget;
	// 로딩 연출 진행 상태. bLoadingRequested는 outro 시작 시점에 false가 되므로
	// "연출이 화면에 남아 있는가"는 bOutroPlaying까지 함께 봐야 한다.
	bool bLoadingRequested = false;
	bool bIntroComplete = false;
	bool bCompletionShowing = false;
	bool bOutroPlaying = false;
	// 인트로가 끝나기 전에 준비가 완료된 경우, 종료 요청을 여기 담아 두고
	// 인트로 종료 콜백에서 이어서 처리한다(FinishLoadingScreen 참고).
	bool bFinishRequested = false;
	bool bFinishWithCompletionRequested = true;
	bool bViewportWidgetAttached = false;
	int32 ReadyStablePollCount = 0;
	/** 준비 판정 폴링을 시작한 시각. 0 이하이면 폴링 중이 아니다. */
	double ReadinessWaitStartSeconds = 0.0;
};
