#include "UI/Loading/MD_LoadingScreenSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "CommonActivatableWidget.h"
#include "ContentStreaming.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "MoviePlayer.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "TimerManager.h"
#include "UI/MD_LocalUIManager.h"
#include "UI/Loading/MD_LoadingScreenSettings.h"
#include "UI/Loading/SMD_LoadingScreenWidget.h"
#include "UI/MD_UISettings.h"
#include "Util/MD_LogUtility.h"

#define LOCTEXT_NAMESPACE "MDLoadingScreen"

namespace
{
	/**
	 * 진행 문구를 같은 상황의 완료 문구로 바꾼다.
	 * 호출부(MD_MultiplayManager / MD_GameModeBase)에서 진행 문구를 바꾸면 여기 접두사도
	 * 함께 고쳐야 한다. 안 고치면 조용히 기본 문구로 떨어진다.
	 */
	FText MakeCompletionSubtitle(const FText& Subtitle)
	{
		// static으로 두면 첫 호출 시점의 언어로 굳는다. 진행 문구는 호출부에서 매번 새로
		// 만들어지므로, 여기만 굳으면 언어 변경 후 완료 문구만 옛 언어로 남는다.
		struct FMapping { const TCHAR* Prefix; FText Completion; };
		const FMapping Mappings[] =
		{
			{TEXT("CREW ASSEMBLING"),      LOCTEXT("CrewAssembled",  "CREW ASSEMBLED • 잠수팀 준비 완료")},
			{TEXT("DIVE IN PROGRESS"),     LOCTEXT("DiveReady",      "DIVE READY • 잠수 준비 완료")},
			{TEXT("RETURNING TO SURFACE"), LOCTEXT("SurfaceReady",   "SURFACE ROUTE READY • 수면 복귀 준비 완료")},
			{TEXT("MISSION INCOMPLETE"),   LOCTEXT("ReturnReady",    "RETURN ROUTE READY • 복귀 준비 완료")},
			{TEXT("LEAVING THE CREW"),     LOCTEXT("DepartureReady", "DEPARTURE READY • 팀 이탈 준비 완료")},
			{TEXT("NEXT SHIFT PREPARING"), LOCTEXT("NextShiftReady", "NEXT SHIFT READY • 다음 작업일 준비 완료")},
		};

		const FString Source = Subtitle.ToString();
		for (const FMapping& Mapping : Mappings)
		{
			if (Source.StartsWith(Mapping.Prefix)) { return Mapping.Completion; }
		}
		return LOCTEXT("LoadingComplete", "LOADING COMPLETE • 로딩 완료");
	}
}

void UMD_LoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 아래에서 완료 델리게이트를 붙이려면 세션 관리자가 먼저 초기화돼 있어야 한다.
	Collection.InitializeDependency<UMD_MultiplayManager>();

	// 세션 작업이 실패하면 트래블이 없으므로 화면을 닫아줘야 한다(유저가 갇히는 것 방지).
	if (UMD_MultiplayManager* SessionManager = GetGameInstance()->GetSubsystem<UMD_MultiplayManager>())
	{
		SessionManager->OnCreateSessionComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnStartGameComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnEndGameComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnJoinSessionComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnDestroySessionComplete.AddDynamic(this, &ThisClass::HandleSessionOperationComplete);
	}

	// 맵 전환의 시작/끝. 세션 절차를 거치지 않는 직접 OpenLevel도 여기로 들어온다.
	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}

void UMD_LoadingScreenSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	if (UMD_MultiplayManager* SessionManager = GetGameInstance()->GetSubsystem<UMD_MultiplayManager>())
	{
		SessionManager->OnCreateSessionComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnStartGameComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnEndGameComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnJoinSessionComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
		SessionManager->OnDestroySessionComplete.RemoveDynamic(this, &ThisClass::HandleSessionOperationComplete);
	}

	PendingIntroCallbacks.Reset();
	RemoveLoadingWidgets();
	Super::Deinitialize();
}

void UMD_LoadingScreenSubsystem::BeginLoadingScreen(const FText& Subtitle)
{
	// 데디케이티드 서버에는 그릴 화면이 없다. 각 클라이언트는 ClientPrepareLoadingTransition
	// RPC를 받아 자기 쪽에서 로딩을 시작한다.
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer) { return; }

	// 이미 로딩 중이면 처음부터 다시 시작하지 않는다.
	// (서버 로컬 호출과 자기 자신에게 온 Client RPC가 겹치는 경우가 있다.)
	if (IsLoadingScreenActive()) { return; }

	// Common UI Transition이 이미 인트로를 재생 중이면 같은 연출을 Slate에서 다시 시작하지 않는다.
	if (IsCommonTransitionScreenActive()) { return; }

	StartLoadingVisuals(Subtitle, /*bSkipIntro=*/false);
	SetLoadingTimer(IntroTimerHandle, &ThisClass::HandleIntroAnimationFinished, MD_LoadingScreen::IntroDurationSeconds);
}

void UMD_LoadingScreenSubsystem::RunAfterIntro(TFunction<void()>&& Callback)
{
	if (!Callback) { return; }

	// 인트로 중이 아니면 기다릴 이유가 없다. 이 즉시 실행 경로가 있어야
	// 로딩 화면을 안 쓰는 호출부에서도 트래블이 멈추지 않는다.
	if (Phase != EMD_LoadingPhase::Intro)
	{
		Callback();
		return;
	}
	PendingIntroCallbacks.Add(MoveTemp(Callback));
}

void UMD_LoadingScreenSubsystem::CancelLoadingScreen()
{
	FinishLoadingScreen(false);
}

void UMD_LoadingScreenSubsystem::NotifyPlayerReady(APlayerController* PlayerController)
{
	if (!IsLoadingScreenActive() || !IsValid(PlayerController) || !PlayerController->IsLocalController()) { return; }

	TryFinishAfterWorldLoad();

	// 이미 폴링 중이면 마감 시한을 다시 늘리지 않는다.
	UWorld* World = GetWorld();
	if (IsLoadingScreenActive() && World && !World->GetTimerManager().IsTimerActive(WorldReadyTimerHandle))
	{
		StartWorldReadyPolling(World);
	}
}

void UMD_LoadingScreenSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance()) { return; }

	// 세션 절차를 거치지 않은 직접 트래블(BP의 OpenLevel 등)을 위한 fallback.
	// 이미 맵 로딩이 시작돼 인트로를 재생할 시간이 없으므로 인트로를 건너뛰고 화면만 덮는다.
	if (!IsLoadingScreenActive())
	{
		StartLoadingVisuals(FText::GetEmpty(), /*bSkipIntro=*/true);
	}
	if (VisualState.IsValid()) { VisualState->SetProgressFloor(MD_LoadingScreen::MapLoadStartProgress); }

	// viewport에 직접 붙은 UMG는 소유 월드가 파괴된 뒤에도 하드 트래블을 살아서 넘어온다.
	// 로딩층은 순수 Slate라 그것을 덮지 못하므로, 목적지에서 outro 아래로 옛 타이틀/일시정지
	// 위젯이 드러나지 않도록 여기서 정리한다.
	if (UWorld* World = GetWorld())
	{
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
	}
	ConfigureMoviePlayer();
}

void UMD_LoadingScreenSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!IsLoadingScreenActive() || !IsValid(LoadedWorld) || LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	// LoadMap은 GameViewport의 overlay slot을 비울 수 있다. MoviePlayer가 아직 위를 덮고 있는
	// 동안 같은 위젯을 다시 붙여, 자동 종료 뒤 검정 프레임이 없게 한다.
	DetachViewportLoadingWidget();
	EnsureViewportLoadingWidget();
	if (VisualState.IsValid()) { VisualState->SetProgressFloor(MD_LoadingScreen::MapLoadedProgress); }

	// 타이머는 월드에 종속돼 옛 월드와 함께 사라진다. 인트로가 남았다면 남은 시간만큼
	// 새 월드에서 다시 걸어야 트래블이 인트로를 잡아먹지 않는다.
	if (Phase == EMD_LoadingPhase::Intro && VisualState.IsValid())
	{
		const double Remaining = MD_LoadingScreen::IntroDurationSeconds - VisualState->GetElapsedSeconds();
		LoadedWorld->GetTimerManager().ClearTimer(IntroTimerHandle);
		SetLoadingTimer(IntroTimerHandle, &ThisClass::HandleIntroAnimationFinished, Remaining, LoadedWorld);
	}

	// 새 월드가 올라왔으므로 준비 판정과 타임아웃 시한을 여기서부터 새로 센다.
	// 위에서 인트로 잔여 시간이 0이라 곧바로 종료 절차로 넘어갔다면 폴링할 이유가 없다.
	if (IsLoadingScreenActive())
	{
		StartWorldReadyPolling(LoadedWorld);
	}
}

void UMD_LoadingScreenSubsystem::StartWorldReadyPolling(UWorld* TimerWorld)
{
	if (!IsValid(TimerWorld)) { return; }

	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = FPlatformTime::Seconds();
	TimerWorld->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
	TimerWorld->GetTimerManager().SetTimer(WorldReadyTimerHandle, this, &ThisClass::TryFinishAfterWorldLoad,
		MD_LoadingScreen::ReadinessPollSeconds, true);
}

void UMD_LoadingScreenSubsystem::StopWorldReadyPolling()
{
	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = 0.0;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WorldReadyTimerHandle);
	}
}

/**
 * 새 월드가 화면을 보여줘도 될 만큼 준비됐는지 검사한다.
 * "맵 파일을 다 읽음"과 "플레이 가능"은 다르다. 여기서 걸러내지 않으면 텍스처가 체크무늬로
 * 보이거나, 서브레벨이 안 떠 바닥이 뚫리거나, 조작이 안 되는 상태로 진입한다.
 *
 * 초당 20회 호출되므로 사유는 열거형으로만 돌려주고 문자열은 만들지 않는다.
 */
EMD_LoadingReadinessBlocker UMD_LoadingScreenSubsystem::CheckWorldReadiness() const
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) { return EMD_LoadingReadinessBlocker::WorldInvalid; }
	if (!World->HasBegunPlay()) { return EMD_LoadingReadinessBlocker::BeginPlayPending; }
	if (!IsValid(GetGameInstance()->GetFirstLocalPlayerController(World)))
	{
		return EMD_LoadingReadinessBlocker::PlayerControllerMissing;
	}
	if (IsAsyncLoading()) { return EMD_LoadingReadinessBlocker::AsyncLoading; }
	if (World->IsVisibilityRequestPending()) { return EMD_LoadingReadinessBlocker::LevelVisibilityPending; }

	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!IsValid(StreamingLevel)) { continue; }

		const bool bWaitingLoad = StreamingLevel->ShouldBeLoaded() && !StreamingLevel->IsLevelLoaded();
		const bool bWaitingVisible = StreamingLevel->ShouldBeVisible() && !StreamingLevel->IsLevelVisible();
		if (bWaitingLoad || bWaitingVisible) { return EMD_LoadingReadinessBlocker::StreamingLevelPending; }
	}

	// Audio streaming cache에는 절대 간섭하지 않는다. IStreamingManager::BlockTillAllRequestsFinished를
	// 부르면 AudioStreamingCache의 종료 Assert(NumberOfLoadsInFlight)가 터진다.
	const IRenderAssetStreamingManager& RenderAssetStreaming = IStreamingManager::Get().GetRenderAssetStreamingManager();
	if (RenderAssetStreaming.GetNumWantingResources() > 0)
	{
		return EMD_LoadingReadinessBlocker::RenderAssetStreaming;
	}

	return EMD_LoadingReadinessBlocker::None;
}

void UMD_LoadingScreenSubsystem::TryFinishAfterWorldLoad()
{
	if (!IsLoadingScreenActive()) { return; }

	const EMD_LoadingReadinessBlocker Blocker = CheckWorldReadiness();
	if (Blocker != EMD_LoadingReadinessBlocker::None)
	{
		ReadyStablePollCount = 0;

		// 준비 조건이 영영 충족되지 않는 구성에서도 화면이 남지 않도록 상한을 넘기면 강제 종료한다.
		const double ElapsedSeconds = ReadinessWaitStartSeconds > 0.0
			? FPlatformTime::Seconds() - ReadinessWaitStartSeconds
			: 0.0;
		if (ElapsedSeconds <= MD_LoadingScreen::ReadinessTimeoutSeconds) { return; }

		UE_LOG(MarineDemolition, Warning, TEXT("%s 로딩 준비 판정이 %.1f초를 초과해 강제 종료합니다. 원인: %s"),
			*UE_CALL_LOCATIONINFO, ElapsedSeconds, MD_LoadingScreen::ToString(Blocker));

		StopWorldReadyPolling();
		// 실제로는 준비되지 않았으므로 COMPLETE 연출 없이 곧바로 fade-out한다.
		FinishLoadingScreen(false);
		return;
	}

	// 안정 판정이 쌓이는 만큼 마지막 구간을 미리 채워, 종료 직전에 진행률이 한 번에 뛰지 않게 한다.
	++ReadyStablePollCount;
	if (VisualState.IsValid())
	{
		VisualState->SetProgressFloor(FMath::Lerp(MD_LoadingScreen::MapLoadedProgress,
			MD_LoadingScreen::ReadyStableProgress,
			static_cast<float>(ReadyStablePollCount) / MD_LoadingScreen::RequiredReadyStablePolls));
	}
	if (ReadyStablePollCount < MD_LoadingScreen::RequiredReadyStablePolls) { return; }

	StopWorldReadyPolling();
	FinishLoadingScreen();
}

void UMD_LoadingScreenSubsystem::FinishLoadingScreen(bool bShowCompletion)
{
	// Completion/Outro는 이미 종료 절차에 들어간 상태이므로 다시 받지 않는다.
	if (Phase != EMD_LoadingPhase::Intro && Phase != EMD_LoadingPhase::Loading) { return; }

	// 가벼운 맵에서는 인트로가 끝나기 전에 준비가 끝날 수 있다. 그때 바로 닫으면 화면이
	// 덮였다가 즉시 걷히며 깜빡이므로, 요청만 기록해 HandleIntroAnimationFinished에서 잇는다.
	if (Phase == EMD_LoadingPhase::Intro)
	{
		PendingFinish = bShowCompletion
			? EMD_LoadingFinishRequest::WithCompletion
			: EMD_LoadingFinishRequest::WithoutCompletion;
		return;
	}

	PendingFinish = EMD_LoadingFinishRequest::None;
	PendingIntroCallbacks.Reset();
	StopWorldReadyPolling();
	EnsureViewportLoadingWidget();

	// bShowCompletion이 false면 정상 완료가 아니다(세션 실패, 준비 판정 타임아웃).
	// 준비되지 않았는데 COMPLETE/체크를 띄우면 거짓 신호가 되므로 곧바로 outro만 재생한다.
	if (!bShowCompletion || !VisualState.IsValid())
	{
		BeginOutro();
		return;
	}

	Phase = EMD_LoadingPhase::Completion;
	VisualState->BeginCompletion();
	SetLoadingTimer(CompletionTimerHandle, &ThisClass::BeginOutro, MD_LoadingScreen::CompletionDisplaySeconds);
}

void UMD_LoadingScreenSubsystem::BeginOutro()
{
	if (Phase == EMD_LoadingPhase::Inactive || Phase == EMD_LoadingPhase::Outro) { return; }

	Phase = EMD_LoadingPhase::Outro;
	PendingFinish = EMD_LoadingFinishRequest::None;
	StopWorldReadyPolling();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CompletionTimerHandle);
	}
	if (VisualState.IsValid())
	{
		VisualState->BeginOutro();
	}

	// outro가 끝나면 위젯과 상태를 통째로 되돌린다.
	SetLoadingTimer(OutroTimerHandle, &ThisClass::RemoveLoadingWidgets, MD_LoadingScreen::OutroDurationSeconds);
}

// 연출 상태를 만들고 화면을 덮는다. bSkipIntro는 인트로를 기다릴 수 없는 fallback 경로용이다.
void UMD_LoadingScreenSubsystem::StartLoadingVisuals(const FText& Subtitle, bool bSkipIntro)
{
	// 남아 있는 위젯은 이전 VisualState를 붙든 채 재사용되므로 새 연출이 보이지 않는다.
	// outro 도중 다음 로딩이 시작되는 경우가 있어 여기서 먼저 걷어낸다.
	RemoveLoadingWidgets();

	const FText Resolved = Subtitle.IsEmpty()
		? LOCTEXT("DefaultSubtitle", "PREPARING EXPEDITION • 원정 준비 중")
		: Subtitle;

	Phase = bSkipIntro ? EMD_LoadingPhase::Loading : EMD_LoadingPhase::Intro;
	PendingFinish = EMD_LoadingFinishRequest::None;
	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = 0.0;
	VisualState = MakeShared<FMD_LoadingVisualState>(Resolved, MakeCompletionSubtitle(Resolved));
	EnsureViewportLoadingWidget();
}

// 로딩 타이머는 모두 월드에 종속된다. 월드가 없거나 남은 시간이 없으면 대기 없이
// 콜백을 즉시 실행해 연출이 그 자리에서 멈추지 않게 한다.
void UMD_LoadingScreenSubsystem::SetLoadingTimer(FTimerHandle& Handle, FTimerCallback Callback, double Seconds,
	UWorld* InWorld)
{
	UWorld* World = InWorld ? InWorld : GetWorld();
	if (World && Seconds > KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().SetTimer(Handle, this, Callback, static_cast<float>(Seconds), false);
		return;
	}
	(this->*Callback)();
}

void UMD_LoadingScreenSubsystem::ClearLoadingTimers()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	FTimerManager& TimerManager = World->GetTimerManager();
	for (FTimerHandle* Handle : {&IntroTimerHandle, &WorldReadyTimerHandle, &CompletionTimerHandle, &OutroTimerHandle})
	{
		TimerManager.ClearTimer(*Handle);
	}
}

// 작업선 텍스처는 Project Settings → Game → MD Loading Screen에서 지정한다.
// 한 번 로드하면 UPROPERTY가 붙들고 있으므로 다음 로딩에서는 다시 읽지 않는다.
TSharedRef<SWidget> UMD_LoadingScreenSubsystem::CreateLoadingWidget()
{
	if (!ShipTexture || !BeaconTexture || !LoadingFont)
	{
		if (const UMD_LoadingScreenSettings* Settings = UMD_LoadingScreenSettings::Get())
		{
			if (!ShipTexture) { ShipTexture = Settings->GetShipTexture(); }
			if (!BeaconTexture) { BeaconTexture = Settings->GetBeaconTexture(); }
			if (!LoadingFont) { LoadingFont = Settings->GetFont(); }
		}
	}
	return SNew(SMD_LoadingScreenWidget)
		.VisualState(VisualState)
		.ShipTexture(ShipTexture)
		.BeaconTexture(BeaconTexture)
		.Font(LoadingFont);
}

void UMD_LoadingScreenSubsystem::EnsureViewportLoadingWidget()
{
	if (!VisualState.IsValid()) { return; }

	if (!ViewportLoadingWidget.IsValid())
	{
		ViewportLoadingWidget = CreateLoadingWidget();
	}
	if (bViewportWidgetAttached) { return; }

	if (UGameViewportClient* ViewportClient = GetGameInstance()->GetGameViewportClient())
	{
		ViewportClient->AddViewportWidgetContent(ViewportLoadingWidget.ToSharedRef(), MD_LoadingScreen::ViewportZOrder);
		bViewportWidgetAttached = true;
	}
}

void UMD_LoadingScreenSubsystem::DetachViewportLoadingWidget()
{
	if (!ViewportLoadingWidget.IsValid()) { return; }

	if (UGameViewportClient* ViewportClient = GetGameInstance()->GetGameViewportClient())
	{
		ViewportClient->RemoveViewportWidgetContent(ViewportLoadingWidget.ToSharedRef());
	}
	bViewportWidgetAttached = false;
}

void UMD_LoadingScreenSubsystem::ConfigureMoviePlayer()
{
	if (!VisualState.IsValid() || !IsMoviePlayerEnabled() || !GetMoviePlayer()->IsInitialized()) { return; }

	// viewport용과 별개의 위젯이지만 VisualState는 같은 것을 넘긴다.
	// 그래서 그리는 주체가 바뀌어도 연출은 같은 시간축 위에서 이어진다.
	MoviePlayerLoadingWidget = CreateLoadingWidget();

	FLoadingScreenAttributes Attributes;
	// -1: 최소 표시 시간을 강제하지 않는다. 표시 길이는 우리 준비 판정이 결정한다.
	Attributes.MinimumLoadingScreenDisplayTime = -1.0f;
	// 맵 로딩이 끝나면 MoviePlayer는 스스로 물러나고 viewport 오버레이가 이어받는다.
	Attributes.bAutoCompleteWhenLoadingCompletes = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.bWaitForManualStop = false;
	Attributes.bAllowInEarlyStartup = false;
	// false여야 전용 Slate 스레드로 그린다. true면 트래블 전 PlayMovie와 겹쳐
	// RayTracingGeometryManager 중복 Tick Assert가 난다.
	Attributes.bAllowEngineTick = false;
	Attributes.WidgetLoadingScreen = MoviePlayerLoadingWidget.ToSharedRef();
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
}

void UMD_LoadingScreenSubsystem::FlushPendingIntroCallbacks()
{
	// 콜백 안에서 트래블이 시작되며 다시 로딩 상태를 건드릴 수 있으므로,
	// 원본을 먼저 비워 두고 지역 복사본을 순회한다.
	TArray<TFunction<void()>> Callbacks = MoveTemp(PendingIntroCallbacks);
	PendingIntroCallbacks.Reset();
	for (TFunction<void()>& Callback : Callbacks)
	{
		if (Callback) { Callback(); }
	}
}

// 연출이 완전히 끝난 뒤(또는 서브시스템 종료 시) 위젯과 상태를 모두 되돌린다.
void UMD_LoadingScreenSubsystem::RemoveLoadingWidgets()
{
	ClearLoadingTimers();
	DetachViewportLoadingWidget();

	ViewportLoadingWidget.Reset();
	MoviePlayerLoadingWidget.Reset();
	VisualState.Reset();
	Phase = EMD_LoadingPhase::Inactive;
	PendingFinish = EMD_LoadingFinishRequest::None;
	ReadyStablePollCount = 0;
	ReadinessWaitStartSeconds = 0.0;
}

void UMD_LoadingScreenSubsystem::HandleIntroAnimationFinished()
{
	if (Phase != EMD_LoadingPhase::Intro) { return; }

	Phase = EMD_LoadingPhase::Loading;
	if (PendingFinish != EMD_LoadingFinishRequest::None)
	{
		FinishLoadingScreen(PendingFinish == EMD_LoadingFinishRequest::WithCompletion);
		return;
	}
	FlushPendingIntroCallbacks();
}

bool UMD_LoadingScreenSubsystem::IsCommonTransitionScreenActive() const
{
	ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	UMD_LocalUIManager* LocalUIManager = IsValid(LocalPlayer)
		? LocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	const UMD_UISettings* UISettings = UMD_UISettings::Get();
	if (!IsValid(LocalUIManager) || !IsValid(UISettings)) { return false; }

	UCommonActivatableWidget* ActiveModalScreen = LocalUIManager->GetActiveScreen(MDGameplayTags::UI_Layer_Modal);
	const FGameplayTag TransitionTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("UI.Layer.Modal.Transition")), /*ErrorIfNotFound=*/false);
	if (!IsValid(ActiveModalScreen) || !TransitionTag.IsValid()) { return false; }

	// 활성 화면이 이미 클래스를 로드했으므로 Soft Class를 강제로 다시 로드하지 않는다.
	// 아직 로드되지 않았다면 그 화면일 리 없으므로 false로 떨어지는 것이 맞다.
	const UClass* TransitionScreenClass = UISettings->WidgetClassMap.FindRef(TransitionTag).Get();
	return TransitionScreenClass && ActiveModalScreen->GetClass() == TransitionScreenClass;
}

// 세션 작업이 실패하면 트래블이 없어 준비 판정이 시작되지 않는다. 화면이 남으므로
// COMPLETE 없이 즉시 걷어낸다. 성공한 경우에는 트래블이 이어지므로 아무것도 하지 않는다.
void UMD_LoadingScreenSubsystem::HandleSessionOperationComplete(bool bWasSuccessful)
{
	if (!bWasSuccessful) { CancelLoadingScreen(); }
}

#undef LOCTEXT_NAMESPACE
