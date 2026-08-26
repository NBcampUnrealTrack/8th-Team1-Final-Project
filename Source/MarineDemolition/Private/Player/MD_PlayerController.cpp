// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerController.h"
#include "UI/Game/MD_HUDWidget.h"
#include "UI/Game/MD_DailyReportWidget.h"
#include "UI/Game/MD_ScrapeFocusReticleWidget.h"
#include "UI/MD_LocalUIManager.h"

#include "CommonActivatableWidget.h"
#include "World/Interaction/MD_TravelConsole.h"
#include "World/Interaction/MD_DayEndConsole.h"
#include "World/Interaction/MD_SectorCloseoutMarker.h"
#include "World/Merchant/MD_Merchant.h"
#include "Game/MD_GameModeBase.h"
#include "Game/MD_GameStateBase.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Gameplay/Attribute/MD_CharacterAttributeSet.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Player/MD_PlayerState.h"
#include "Player/MD_PlayerCameraManager.h"

#include "Engine/GameInstance.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"

#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Character/MD_Character.h"
#include "World/Merchant/MD_Merchant.h"

AMD_PlayerController::AMD_PlayerController()
{
	PlayerCameraManagerClass = AMD_PlayerCameraManager::StaticClass();
	ScrapeFocusReticleWidgetClass = UMD_ScrapeFocusReticleWidget::StaticClass();
}

void AMD_PlayerController::CancelScrapeInteraction()
{
	AMD_Character* const ScrapeCharacter = Cast<AMD_Character>(GetPawn());
	if (IsValid(ScrapeCharacter))
	{
		ScrapeCharacter->CancelScrapeInteraction();
	}
}

void AMD_PlayerController::SetInputMode(const FInputModeDataBase& InData)
{
	// UI 전환으로 Completed/Canceled 입력을 잃기 전에 먼저 Focus와 Ability를 정리한다.
	CancelScrapeInteraction();
	Super::SetInputMode(InData);
}

void AMD_PlayerController::SetViewTarget(
	AActor* NewViewTarget,
	const FViewTargetTransitionParams TransitionParams)
{
	if (NewViewTarget != GetPawn())
	{
		// 컷신·관전 카메라가 Focus 고정 카메라와 경쟁하지 않게 먼저 정리한다.
		CancelScrapeInteraction();
	}
	Super::SetViewTarget(NewViewTarget, TransitionParams);
}

void AMD_PlayerController::HandleApplicationWillDeactivate()
{
	CancelScrapeInteraction();
}

void AMD_PlayerController::ServerRequestTravel_Implementation(AMD_TravelConsole* Console, int32 SiteIndex, EMD_Difficulty Difficulty)
{
	if (!IsValid(Console))
	{
		return;
	}

	// 출발 지정은 호스트만. 리슨서버 호스트의 컨트롤러만 서버에서 로컬이다
	if (!IsLocalController())
	{
		return;
	}

	// 콘솔 근처에 있는 플레이어의 요청만 수행 (임의 호출 방지)
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn) || FVector::Dist(MyPawn->GetActorLocation(), Console->GetActorLocation()) > 500.f)
	{
		return;
	}

	// 클라가 보낸 건 인덱스뿐. 레벨은 서버가 자기 사본에서 읽고 잠긴 현장이면 빈 경로가 돌아온다
	const FString MapPackage = Console->ResolveSiteLevel(SiteIndex).ToSoftObjectPath().GetLongPackageName();

	if (MapPackage.IsEmpty())
	{
		return;
	}

	// 네트워크로 전달된 난이도 값이 정의된 계약 범위를 벗어나지 않도록 하는 서버 검증
	if (Difficulty < EMD_Difficulty::Safe || Difficulty > EMD_Difficulty::Extreme)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UMD_MultiplayManager* MultiplayManager = IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MultiplayManager>() : nullptr;
	if (IsValid(MultiplayManager) == false)
	{
		return;
	}

	// 서버가 출발 요청을 승인한 순간의 실제 접속 인원을 계약 인원으로 확정하기 위한 GameState 조회
	AGameStateBase* TravelGameState = IsValid(GetWorld()) == true ? GetWorld()->GetGameState() : nullptr;
	if (IsValid(TravelGameState) == false)
	{
		return;
	}

	// 접속 중인 PlayerState만 집계하여 클라이언트 입력으로 인원 배율을 조작할 수 없도록 하는 서버 계산
	int32 ConnectedCrewCount = 0;
	for (APlayerState* ConnectedPlayerState : TravelGameState->PlayerArray)
	{
		if (IsValid(ConnectedPlayerState) == true)
		{
			++ConnectedCrewCount;
		}
	}

	// 점수표가 정의한 1~4인 계약 범위 안에서 사용할 최종 출발 인원수
	int32 ContractCrewCount = FMath::Clamp(ConnectedCrewCount, 1, 4);

	// uint8 기반 난이도를 동일한 저장 폭의 숫자 문자열로 직렬화하는 출발 옵션 구성
	uint8 DifficultyValue = static_cast<uint8>(Difficulty);
	FString TravelOptions = TEXT("?listen?Difficulty=");
	TravelOptions.AppendInt(DifficultyValue);
	TravelOptions.Append(TEXT("?CrewCount="));
	TravelOptions.AppendInt(ContractCrewCount);

	// Session 갱신과 ServerTravel 실행은 MultiplayManager에 위임하는 처리
	MultiplayManager->StartSessionByMap(MapPackage, TravelOptions);
}

void AMD_PlayerController::ServerRequestEndDay_Implementation(AMD_DayEndConsole* Console)
{
	if (!IsValid(Console))
	{
		return;
	}

	// 콘솔 근처에 있는 플레이어의 요청만 수행 (임의 호출 방지)
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn) || FVector::Dist(MyPawn->GetActorLocation(), Console->GetActorLocation()) > 500.f)
	{
		return;
	}

	if (AMD_GameModeBase* GM = GetWorld()->GetAuthGameMode<AMD_GameModeBase>())
	{
		GM->RequestEndDay();
	}
}

void AMD_PlayerController::ServerRequestShopPurchase_Implementation(AMD_Merchant* Merchant, FName ItemId)
{
	// 유효하지 않은 액터 참조로 서버 원장에 접근하지 못하도록 하는 요청 검사
	if (IsValid(Merchant) == false || ItemId.IsNone() == true)
	{
		ClientReceiveShopPurchaseResult(Merchant, ItemId, EMD_ShopPurchaseResult::InvalidRequest);
		return;
	}

	AMD_Character* RequestingCharacter = Cast<AMD_Character>(GetPawn());

	// 상점에서 멀어진 플레이어가 보관한 UI나 임의 RPC로 결제하지 못하도록 하는 거리 검사
	if (IsValid(RequestingCharacter) == false || FVector::Dist(RequestingCharacter->GetActorLocation(), Merchant->GetActorLocation()) > 500.0f)
	{
		ClientReceiveShopPurchaseResult(Merchant, ItemId, EMD_ShopPurchaseResult::InvalidRequest);
		return;
	}

	// 카탈로그와 서버 경제 원장을 소유한 상인 액터에 최종 구매 판정을 위임하는 처리
	const EMD_ShopPurchaseResult PurchaseResult = Merchant->TryPurchaseItem(RequestingCharacter, ItemId);
	ClientReceiveShopPurchaseResult(Merchant, ItemId, PurchaseResult);
}

void AMD_PlayerController::ClientReceiveShopPurchaseResult_Implementation(AMD_Merchant* Merchant, FName ItemId, EMD_ShopPurchaseResult Result)
{
	// 다른 상인의 로컬 위젯에 결과가 섞이지 않도록 요청 대상 액터에 직접 전달하는 처리
	if (IsValid(Merchant) == true)
	{
		Merchant->ReceiveShopPurchaseResult(ItemId, Result);
	}
}

void AMD_PlayerController::ClientShowDailyReport_Implementation(FMD_InspectionReport Report)
{
	// 서버 전용 컨트롤러 사본에서 로컬 UI 생성을 시도하지 않도록 하는 검사
	if (IsLocalController() == false)
	{
		return;
	}

	// 보고서 클래스를 지정하지 않은 구성에서도 기존처럼 즉시 전환 딜레이를 시작하기 위한 대체 요청
	if (DailyReportWidgetClass == nullptr)
	{
		ServerConfirmDayTransition();
		return;
	}

	if (IsValid(DailyReportWidgetInstance) == false)
	{
		// EndDay마다 기존 디자인의 단일 보고서 화면을 생성하기 위한 로컬 인스턴스
		DailyReportWidgetInstance = CreateWidget<UMD_DailyReportWidget>(this, DailyReportWidgetClass);
		if (IsValid(DailyReportWidgetInstance) == true)
		{
			// 새 인스턴스에서 발생한 확인 이벤트를 현재 소유 컨트롤러에 한 번만 연결하는 처리
			DailyReportWidgetInstance->OnContinueRequested.AddDynamic(this, &AMD_PlayerController::HandleDailyReportContinueClicked);
		}
	}

	if (IsValid(DailyReportWidgetInstance) == false)
	{
		// 위젯 생성 실패로 전환 확인 입력을 받을 수 없는 경우에도 진행을 보장하기 위한 대체 요청
		ServerConfirmDayTransition();
		return;
	}

	// 전용 위젯이 표시 데이터와 내부 버튼을 관리하고 PlayerController는 화면 상태만 조정하는 구조
	DailyReportWidgetInstance->SetContinueEnabled(true);
	DailyReportWidgetInstance->SetReport(Report);

	if (DailyReportWidgetInstance->IsInViewport() == false)
	{
		// 다른 HUD보다 앞에서 전환 확인을 받도록 하는 최상위 화면 배치
		DailyReportWidgetInstance->AddToViewport(1000);
	}

	DailyReportWidgetInstance->SetVisibility(ESlateVisibility::Visible);
	SetGameplayHUDVisible(false);

	// 보고서 확인 외 게임플레이 입력을 막고 버튼 조작에 포커스를 고정하기 위한 입력 모드
	FInputModeUIOnly DailyReportInputMode;
	DailyReportInputMode.SetWidgetToFocus(DailyReportWidgetInstance->TakeWidget());
	SetInputMode(DailyReportInputMode);
	bShowMouseCursor = true;
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
}

void AMD_PlayerController::ClientCloseDailyReport_Implementation()
{
	CloseDailyReport();
}

// 서버가 트래블하기 직전에 각 소유 클라이언트에서 로딩 화면을 시작시킨다.
// 서버 화면만 덮으면 원격 클라이언트는 아무 예고 없이 멈춘 것처럼 보인다.
void AMD_PlayerController::ClientPrepareLoadingTransition_Implementation(const FText& Subtitle)
{
	if (UMD_LoadingScreenSubsystem* LoadingScreen = GetGameInstance()->GetSubsystem<UMD_LoadingScreenSubsystem>())
	{
		LoadingScreen->BeginLoadingScreen(Subtitle);
	}
}

// 조작 주체가 준비됐음을 알려 준비 판정을 앞당긴다. 하드 트래블(BeginPlayingState)과
// seamless travel(PostSeamlessTravel) 두 경로가 공유한다.
// 판정 자체는 로딩 서브시스템이 스트리밍까지 확인한 뒤 최종 결정한다.
void AMD_PlayerController::NotifyLoadingScreenPlayerReady()
{
	if (UMD_LoadingScreenSubsystem* LoadingScreen = GetGameInstance()->GetSubsystem<UMD_LoadingScreenSubsystem>())
	{
		LoadingScreen->NotifyPlayerReady(this);
	}
}

void AMD_PlayerController::ServerConfirmDayTransition_Implementation()
{
	// 실제 전환 분기와 중복 실행 방지는 서버 전용 GameMode가 최종 결정하는 구조
	AMD_GameModeBase* MDGameMode = IsValid(GetWorld()) == true ? GetWorld()->GetAuthGameMode<AMD_GameModeBase>() : nullptr;
	if (IsValid(MDGameMode) == true)
	{
		MDGameMode->ConfirmDayTransition(this);
	}
}

void AMD_PlayerController::HandleDailyReportContinueClicked()
{
	// 연속 클릭으로 같은 서버 RPC가 반복 전송되지 않도록 즉시 버튼을 잠그는 처리
	if (IsValid(DailyReportWidgetInstance) == true)
	{
		DailyReportWidgetInstance->SetContinueEnabled(false);
	}

	ServerConfirmDayTransition();
}

void AMD_PlayerController::CloseDailyReport()
{
	if (IsLocalController() == false)
	{
		return;
	}

	// 실제 보고서가 열린 경우에만 전용 입력 상태를 복구하기 위한 제거 결과 검사
	if (RemoveDailyReportWidget() == false)
	{
		return;
	}

	// 보고서 전용 UI 입력과 숨긴 HUD를 다음 맵의 정상 게임플레이 상태로 복구하는 처리
	SetGameplayHUDVisible(true);
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
}

bool AMD_PlayerController::RemoveDailyReportWidget()
{
	// 호출자가 입력 상태 복구 필요 여부를 판단할 수 있도록 하는 제거 결과
	bool bWasRemoved = false;

	if (IsValid(DailyReportWidgetInstance) == false)
	{
		return bWasRemoved;
	}

	// 재생성 또는 맵 전환 전에 외부 이벤트와 뷰포트 소유권을 한 번에 정리하는 처리
	DailyReportWidgetInstance->OnContinueRequested.RemoveDynamic(this, &AMD_PlayerController::HandleDailyReportContinueClicked);
	DailyReportWidgetInstance->RemoveFromParent();
	DailyReportWidgetInstance = nullptr;
	bWasRemoved = true;
	return bWasRemoved;
}

void AMD_PlayerController::ServerRequestSectorCloseout_Implementation(AMD_SectorCloseoutMarker* Marker)
{
	if (!IsValid(Marker))
	{
		return;
	}

	// 마커 근처에 있는 플레이어의 요청만 수행 (임의 호출 방지)
	APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn) || FVector::Dist(MyPawn->GetActorLocation(), Marker->GetActorLocation()) > 500.f)
	{
		return;
	}

	Marker->TryCloseout();
}

void AMD_PlayerController::InitializeGameplayHUD()
{
	if (IsLocalController() == false || IsValid(GetPawn()) == false)
	{
		return;
	}

	// 설정되지 않은 HUD 클래스는 Stack에 전달하지 않는다.
	if (IsValid(HUDWidgetClass) == false)
	{
		return;
	}

	// LocalPlayer의 중앙 UI 관리자를 가져온다.
	ULocalPlayer* OwningLocalPlayer = GetLocalPlayer();
	UMD_LocalUIManager* LocalUIManager = IsValid(OwningLocalPlayer) == true ? OwningLocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	if (IsValid(LocalUIManager) == false)
	{
		return;
	}

	// 여러 초기화 진입점이 호출되어도 이미 활성화된 HUD를 중복 Push하지 않는다.
	UCommonActivatableWidget* ActiveScreen = LocalUIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameHUD);
	UMD_HUDWidget* ActiveHUDWidget = Cast<UMD_HUDWidget>(ActiveScreen);
	if (IsValid(ActiveHUDWidget) == true)
	{
		HUDWidgetInstance = ActiveHUDWidget;
	}
	else
	{
		// HUD 생성과 활성화를 PrimaryLayout의 GameHUD Stack에 위임한다.
		UCommonActivatableWidget* PushedScreen = LocalUIManager->PushScreenToLayer(HUDWidgetClass);
		HUDWidgetInstance = Cast<UMD_HUDWidget>(PushedScreen);
		if (IsValid(HUDWidgetInstance) == false)
		{
			return;
		}

		// Project Settings에서 자동 결정한 Layer에 생성된 HUD의 입력 정책을 적용한다.
		HUDWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void AMD_PlayerController::InitializeScrapeFocusReticle()
{
	if (IsLocalController() == false || ScrapeFocusReticleWidgetClass == nullptr)
	{
		return;
	}

	if (IsValid(ScrapeFocusReticleWidgetInstance) == false)
	{
		ScrapeFocusReticleWidgetInstance =
			CreateWidget<UMD_ScrapeFocusReticleWidget>(
				this,
				ScrapeFocusReticleWidgetClass);
	}

	if (IsValid(ScrapeFocusReticleWidgetInstance) == false)
	{
		return;
	}

	// 파생 Blueprint defaults와 무관하게 이 overlay 전체를 비입력형으로 고정한다.
	ScrapeFocusReticleWidgetInstance->SetVisibility(ESlateVisibility::HitTestInvisible);
	ScrapeFocusReticleWidgetInstance->SetIsFocusable(false);
	ScrapeFocusReticleWidgetInstance->ForceVolatile(true);

	// Seamless Travel에서 유효한 위젯이 기존 viewport에서 제거된 경우에도 복구한다.
	if (ScrapeFocusReticleWidgetInstance->IsInViewport() == false)
	{
		// Owning local player 영역에만 붙여 split-screen이나 다중 local player에서도
		// 다른 플레이어의 normalized 좌표/입력을 시각화하지 않는다.
		ScrapeFocusReticleWidgetInstance->AddToPlayerScreen(1000);
	}
}

void AMD_PlayerController::MD_ApplyTestDamage()
{
	ServerApplyTestDamage();
}

void AMD_PlayerController::MD_ShowHealth()
{
	const AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	const UMD_CharacterAttributeSet* Attributes = IsValid(MDPlayerState) ? MDPlayerState->GetCharacterAttributeSet() : nullptr;

	if (!IsValid(Attributes))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Attribute] AttributeSet을 찾을 수 없다"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Attribute] HP = %.1f / %.1f"),
		Attributes->GetHealth(), Attributes->GetMaxHealth());
}

void AMD_PlayerController::ServerApplyTestDamage_Implementation()
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (!IsValid(MDPlayerState))
	{
		return;
	}

	UAbilitySystemComponent* ASC = MDPlayerState->GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	//GE를 일으킨 주체 확인
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);

	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(TestDamageEffect, 1.0f, Context);
	if (!Spec.IsValid())
	{
		return;
	}

	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}

void AMD_PlayerController::MD_ShowHealthState()
{
	const AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (!IsValid(MDPlayerState))
	{
		return;
	}

	const UMD_CharacterAttributeSet* Attributes = MDPlayerState->GetCharacterAttributeSet();
	if (!IsValid(Attributes))
	{
		return;
	}
	
	const UAbilitySystemComponent* ASC = MDPlayerState->GetAbilitySystemComponent();
	if (!IsValid(ASC))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Health][%s] HP=%.1f 상태=%d 부상태그=%d 사망태그=%d"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"),
		IsValid(Attributes) ? Attributes->GetHealth() : -1.0f,
		static_cast<int32>(MDPlayerState->GetHealthState()),
		IsValid(ASC) && ASC->HasMatchingGameplayTag(MDGameplayTags::State_Injured) ? 1 : 0,
		IsValid(ASC) && ASC->HasMatchingGameplayTag(MDGameplayTags::State_Dead) ? 1 : 0);
}

void AMD_PlayerController::MD_AddCredits(int32 Amount)
{
	ServerAddCredits(Amount);
}

void AMD_PlayerController::MD_ShowCredits()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return;
	}

	UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	if (!IsValid(Merchant))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Merchant][%s] Credits = %d"),HasAuthority() ? TEXT("Server") : TEXT("Client"),Merchant->GetCredits());
}

void AMD_PlayerController::MD_AddAffinity(int32 DeltaAffinity)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->AddAffinity(DeltaAffinity);
	}
}

void AMD_PlayerController::ServerAddCredits_Implementation(int32 Amount)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->AddCredits(EMD_DailyReportCategory::Other, Amount);
	}
}

void AMD_PlayerController::MD_BeginContract()
{
	ServerBeginContract();
}

void AMD_PlayerController::MD_DumpQuests()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return;
	}

	UMD_MerchantSubsystem* Merchant = GameInstance->GetSubsystem<UMD_MerchantSubsystem>();
	if (!IsValid(Merchant))
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Merchant] Credits=%d Day=%d"),Merchant->GetCredits(), Merchant->GetCurrentDay());
}

void AMD_PlayerController::BeginPlay()
{
	Super::BeginPlay();
	InitializeGameplayHUD();

	if (IsLocalController() == false)
	{
		return;
	}

	if (!ApplicationWillDeactivateHandle.IsValid())
	{
		ApplicationWillDeactivateHandle =
			FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(
				this,
				&AMD_PlayerController::HandleApplicationWillDeactivate);
	}

	InitializeScrapeFocusReticle();
}

void AMD_PlayerController::BeginPlayingState()
{
	Super::BeginPlayingState();

	InitializeGameplayHUD();
	InitializeScrapeFocusReticle();

	NotifyLoadingScreenPlayerReady();
}

void AMD_PlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	CloseDailyReport();
	InitializeGameplayHUD();
	InitializeScrapeFocusReticle();

	// Seamless travel은 BeginPlayingState를 거치지 않으므로 같은 알림을 여기서도 보낸다.
	NotifyLoadingScreenPlayerReady();
}

void AMD_PlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);

	if (HasActorBegunPlay() == true)
	{
		// 최초 스폰과 재스폰 모두 Pawn 지정이 끝난 뒤 HUD를 보장하는 처리
		InitializeGameplayHUD();
		InitializeScrapeFocusReticle();
	}
}

void AMD_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ApplicationWillDeactivateHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(
			ApplicationWillDeactivateHandle);
		ApplicationWillDeactivateHandle.Reset();
	}

	if (IsValid(ScrapeFocusReticleWidgetInstance))
	{
		ScrapeFocusReticleWidgetInstance->RemoveFromParent();
		ScrapeFocusReticleWidgetInstance = nullptr;
	}

	RemoveDailyReportWidget();

	CancelScrapeInteraction();
	Super::EndPlay(EndPlayReason);
}

void AMD_PlayerController::SetGameplayHUDVisible(bool bVisible)
{
	// Collapsed로 두면 레이아웃 계산에서도 빠지고, 되돌릴 때 SelfHitTestInvisible이라
	// GameHUD Stack 내부의 HUD가 마우스 입력을 가로채지 않는다.
	const ESlateVisibility Target =
		bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;

	if (IsValid(HUDWidgetInstance))
	{
		HUDWidgetInstance->SetVisibility(Target);
	}
}

void AMD_PlayerController::ServerAbandonRepeatQuest_Implementation(int32 Index)
{
	// 퀘스트 포기에 따른 패널티를 적용할 GameState 얻기
	AMD_GameStateBase* MDGameState = IsValid(GetWorld()) == true ? GetWorld()->GetGameState<AMD_GameStateBase>() : nullptr;
	if (IsValid(MDGameState) == false || MDGameState->HasAuthority() == false)
	{
		return;
	}

	// 퀘스트 상태와 경제·호감도 페널티를 관리하는 서버의 상인 매니저 얻기
	UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem();
	if (IsValid(Merchant) == false)
	{
		return;
	}

	// 고정 점수 차감 없이 퀘스트 에셋의 미회수 패널티만 적용하는 포기 처리
	Merchant->TryAbandonRepeatQuest(Index);
}

void AMD_PlayerController::ServerBorrowCredits_Implementation(AMD_Merchant* MerchantActor, int32 Amount)
{
	APawn* PlayerPawn = GetPawn();
	if (IsValid(MerchantActor) == false || IsValid(PlayerPawn) == false)
	{
		return;
	}

	// 멀리 떨어지 상태에서 돈을 빌리는 경우 함수 조기 종료
	if (FVector::DistSquared(PlayerPawn->GetActorLocation(), MerchantActor->GetActorLocation()) > FMath::Square(500.0f))
	{
		return;
	}

	// 대출 실행
	UMD_MerchantSubsystem* MerchantSubsystem = GetMerchantSubsystem();
	if (IsValid(MerchantSubsystem) == true)
	{
		MerchantSubsystem->TryBorrowCredits(Amount);
	}
}

void AMD_PlayerController::ServerRepayLoan_Implementation(AMD_Merchant* MerchantActor, int32 Amount)
{
	APawn* PlayerPawn = GetPawn();
	if (IsValid(MerchantActor) == false || IsValid(PlayerPawn) == false)
	{
		return;
	}

	// 멀리 떨어지 상태에서 돈을 상환하는 경우 함수 조기 종료
	if (FVector::DistSquared(PlayerPawn->GetActorLocation(), MerchantActor->GetActorLocation()) > FMath::Square(500.0f))
	{
		return;
	}


	UMD_MerchantSubsystem* MerchantSubsystem = GetMerchantSubsystem();
	if (IsValid(MerchantSubsystem) == true)
	{
		MerchantSubsystem->TryRepayLoan(Amount);
	}
}


/* 아래부터는 콘솔 커맨드 입력으로 디버깅용 ServerRPC래핑입니다. 미션선택 UI연결되면 삭제 */

UMD_MerchantSubsystem* AMD_PlayerController::GetMerchantSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();

	return IsValid(GameInstance) ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
}

void AMD_PlayerController::ServerBeginContract_Implementation()
{
	// DebugSite는 서버·클라 양쪽 BP에 같은 값이 들어 있으므로 서버 사본을 쓴다.
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->BeginContract(DebugSite);
	}
}

void AMD_PlayerController::MD_BeginDay(int32 Day)
{
	ServerBeginDay(Day);
}

void AMD_PlayerController::ServerBeginDay_Implementation(int32 Day)
{
	UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem();
	if (IsValid(Merchant) == false)
	{
		return;
	}

	// 실제 맵 재진입 경로처럼 종료된 계약 다음 Day 명령에서 새 디버그 계약을 먼저 구성하는 처리
	if (Merchant->HasActiveContract() == false)
	{
		Merchant->BeginContract(DebugSite);
	}

	// DebugSite 미설정으로 계약 생성에 실패했을 때 잘못된 Day 상태만 남기지 않기 위한 검사
	if (Merchant->HasActiveContract() == false)
	{
		return;
	}

	// 실제 Day 시작과 동일하게 GameState의 비상 상태를 디버그 착수금 계산에 전달
	AMD_GameStateBase* MDGameState = GetWorld()->GetGameState<AMD_GameStateBase>();
	bool bEmergencyDay = IsValid(MDGameState) == true ? bEmergencyDay = MDGameState->IsEmergencyDay() : false;

	Merchant->BeginDay(Day, DebugAdvancePayment, bEmergencyDay);
}

void AMD_PlayerController::MD_AcceptRepeat(int32 Index)
{
	ServerAcceptRepeat(Index);
}

void AMD_PlayerController::ServerAcceptRepeat_Implementation(int32 Index)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->TryAcceptRepeatQuest(Index);
	}
}

void AMD_PlayerController::MD_DeclineRepeat(int32 Index)
{
	ServerDeclineRepeat(Index);
}

void AMD_PlayerController::ServerDeclineRepeat_Implementation(int32 Index)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->TryDeclineRepeatQuest(Index);
	}
}

void AMD_PlayerController::MD_Deliver(const FString& GradeName, bool bIntact)
{
	FGameplayTag GradeTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("Resource.Sample.Grade.%s"), *GradeName)),/*ErrorIfNotFound=*/false);

	ServerDeliver(GradeTag, bIntact);
}

void AMD_PlayerController::ServerDeliver_Implementation(FGameplayTag GradeTag, bool bIntact)
{
	// 실제 퀘스트 아이템 액터 검사를 우회하는 필드 기반 디버그 제출의 차단 기록
	UE_LOG(LogTemp, Warning, TEXT("[Quest] MD_Deliver 제출 차단: 실제 아이템 액터 필요 (등급=%s, 온전함=%s)"), *GradeTag.ToString(), bIntact == true ? TEXT("true") : TEXT("false"));
}

void AMD_PlayerController::MD_MarkSiteCleared()
{
	ServerMarkSiteCleared();
}

void AMD_PlayerController::ServerMarkSiteCleared_Implementation()
{
	UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem();
	if (IsValid(Merchant) == false)
	{
		return;
	}

	// 실제 사이트 클리어 전환과 동일하게 이력 등록 후 현재 계약을 종료하는 처리
	Merchant->MarkSiteCleared();
	Merchant->FinalizeContract();
}

void AMD_PlayerController::MD_FinalizeContract()
{
	ServerFinalizeContract();

}

void AMD_PlayerController::ServerFinalizeContract_Implementation()
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->FinalizeContract();
	}
}
