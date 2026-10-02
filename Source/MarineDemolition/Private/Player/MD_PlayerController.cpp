// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerController.h"
#include "UI/Game/MD_HUDWidget.h"
#include "UI/MD_LocalUIManager.h"
#include "UI/Game/MD_DailyReportWidget.h"

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
#include "EngineUtils.h"
#include "World/Wall/MD_CuttableWall.h"
#include "World/Wall/MD_WallUtility.h"
#include "World/Sample/MD_QuestSampleSpawner.h"
#include "Data/Merchant/MD_QuestDefinition.h"
#include "Player/MD_PlayerState.h"
#include "Player/MD_PlayerCameraManager.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "Gameplay/Customization/MD_CharacterAppearanceLibrary.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Online/Session/MD_MultiplayManager.h"
#include "UI/Loading/MD_LoadingScreenSubsystem.h"

#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Data/Equipment/MD_QuestItemDefinition.h"
#include "World/Merchant/MD_Merchant.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "Util/MD_LogUtility.h"

AMD_PlayerController::AMD_PlayerController()
{
	PlayerCameraManagerClass = AMD_PlayerCameraManager::StaticClass();
}

void AMD_PlayerController::CancelScrapeInteraction()
{
	AMD_Character* const MDCharacter = Cast<AMD_Character>(GetPawn());
	if (IsValid(MDCharacter))
	{
		MDCharacter->CancelScrapeInteraction();
	}
}

void AMD_PlayerController::SetInputMode(const FInputModeDataBase& InData)
{
	// UI 전환으로 입력을 잃기 전에 진행 중인 오염물 타격을 정리한다.
	CancelScrapeInteraction();
	Super::SetInputMode(InData);
}

void AMD_PlayerController::SetViewTarget(
	AActor* NewViewTarget,
	const FViewTargetTransitionParams TransitionParams)
{
	if (NewViewTarget != GetPawn())
	{
		// 컷신·관전 카메라로 전환하기 전에 진행 중인 타격을 정리한다.
		CancelScrapeInteraction();
	}
	Super::SetViewTarget(NewViewTarget, TransitionParams);
}

void AMD_PlayerController::ClientWasKicked_Implementation(const FText& KickReason)
{
	Super::ClientWasKicked_Implementation(KickReason);

	// 강퇴된 클라이언트의 로컬 Named Session 정리를 위한 관리자 조회
	UGameInstance* GameInstance = GetGameInstance();
	UMD_MultiplayManager* MultiplayManager =
		IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MultiplayManager>() : nullptr;
	if (IsValid(MultiplayManager) == false)
	{
		return;
	}

	// 기존 Session 제거와 Title 복귀 절차 시작
	MultiplayManager->DestroySession();
}

void AMD_PlayerController::HandleApplicationWillDeactivate()
{
	CancelScrapeInteraction();
}

void AMD_PlayerController::SubmitPlatformNickname()
{
	// 원격 컨트롤러와 전용 서버에는 제출할 로컬 플랫폼 사용자가 없다.
	if (IsLocalController() == false)
	{
		return;
	}

	// 현재 플랫폼에 연결된 Online Subsystem을 가져온다.
	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();
	if (OnlineSubsystem == nullptr)
	{
		return;
	}

	// 로그인 사용자 정보에 접근할 Identity 인터페이스를 가져온다.
	IOnlineIdentityPtr IdentityInterface = OnlineSubsystem->GetIdentityInterface();
	if (IdentityInterface.IsValid() == false)
	{
		return;
	}

	// 플랫폼 로그인이 완료된 사용자만 닉네임을 제출한다.
	if (IdentityInterface->GetLoginStatus(0) != ELoginStatus::LoggedIn)
	{
		return;
	}

	// 첫 번째 로컬 사용자의 Steam 등 플랫폼 표시 이름을 가져온다.
	FString PlatformNickname = IdentityInterface->GetPlayerNickname(0);
	PlatformNickname.TrimStartAndEndInline();
	if (PlatformNickname.IsEmpty() == true)
	{
		return;
	}

	// 서버가 소유한 PlayerState에 플랫폼 닉네임을 저장하도록 요청한다.
	ServerSubmitPlatformNickname(PlatformNickname);
}

void AMD_PlayerController::ServerSubmitPlatformNickname_Implementation(const FString& PlatformNickname)
{
	// 이름표와 네트워크 데이터가 과도하게 커지지 않도록 표시 이름 길이를 제한한다.
	FString ValidatedPlatformNickname = PlatformNickname;
	ValidatedPlatformNickname.TrimStartAndEndInline();
	ValidatedPlatformNickname.LeftInline(32);
	if (ValidatedPlatformNickname.IsEmpty() == true)
	{
		return;
	}

	// 현재 컨트롤러가 소유한 PlayerState만 변경해 다른 사용자의 이름 변경을 차단한다.
	AMD_PlayerState* CurrentPlayerState = GetPlayerState<AMD_PlayerState>();
	if (IsValid(CurrentPlayerState) == false)
	{
		return;
	}

	// 엔진의 PlayerName 복제 경로를 사용해 모든 클라이언트에 플랫폼 닉네임을 전달한다.
	CurrentPlayerState->SetPlayerName(ValidatedPlatformNickname);
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

	// 클라가 보낸 건 인덱스뿐이며 레벨은 서버가 자기 사본에서 다시 조회한다.
	FString MapPackage = Console->ResolveSiteLevel(SiteIndex).ToSoftObjectPath().GetLongPackageName();

	// PIE에서 이미 방문한 World 참조에 붙은 UEDPIE 접두사를 실제 에셋 패키지 경로로 복원한다.
	MapPackage = UWorld::RemovePIEPrefix(MapPackage);

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

void AMD_PlayerController::ServerRequestKickPlayer_Implementation(APlayerState* TargetPlayerState)
{
	// 리슨 서버의 로컬 호스트가 아닌 원격 클라이언트 요청 거부
	if (IsLocalController() == false)
	{
		return;
	}

	// 요청한 호스트의 PlayerState 조회
	APlayerState* RequestingPlayerState = GetPlayerState<APlayerState>();
	if (IsValid(RequestingPlayerState) == false)
	{
		return;
	}

	// 유효하지 않거나 호스트 자신을 대상으로 한 강퇴 요청 거부
	if (IsValid(TargetPlayerState) == false || TargetPlayerState == RequestingPlayerState)
	{
		return;
	}

	// 서버에 존재하는 대상 PlayerController 조회
	APlayerController* TargetPlayerController = TargetPlayerState->GetPlayerController();
	if (IsValid(TargetPlayerController) == false)
	{
		return;
	}

	// 강퇴를 수행할 현재 서버 GameMode 조회
	UWorld* World = GetWorld();
	AGameModeBase* GameMode = IsValid(World) == true ? World->GetAuthGameMode() : nullptr;
	if (IsValid(GameMode) == false || IsValid(GameMode->GameSession) == false)
	{
		return;
	}

	// 대상 클라이언트에 전달할 강퇴 사유
	FText KickReason = NSLOCTEXT("MDPlayerKick", "KickedByHost", "호스트에 의해 강퇴되었습니다");
	// 엔진 GameSession을 통한 대상 접속 해제와 PlayerController 제거
	bool bWasKicked = GameMode->GameSession->KickPlayer(TargetPlayerController, KickReason);
	if (bWasKicked == false)
	{
		UE_LOG(MarineDemolition, Warning, TEXT("%s 선택한 플레이어의 강퇴에 실패했습니다"), *UE_CALL_LOCATIONINFO);
	}
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
		// 서버 콘솔에 설정된 보고서를 모든 접속 플레이어에게 표시
		GM->RequestEndDay(EMD_DayEndReason::Console, Console->GetConsoleWidgetTag());
	}
}

void AMD_PlayerController::ServerRequestShopPurchase_Implementation(AMD_Merchant* Merchant, FPrimaryAssetId ItemAssetId)
{
	// 유효하지 않은 액터 참조로 서버 원장에 접근하지 못하도록 하는 요청 검사
	if (IsValid(Merchant) == false || ItemAssetId.IsValid() == false)
	{
		// 거부된 요청의 전체 식별자와 실패 결과를 소유 클라이언트에 전달
		ClientReceiveShopPurchaseResult(Merchant, ItemAssetId, EMD_ShopPurchaseResult::InvalidRequest);
		return;
	}

	AMD_Character* RequestingCharacter = Cast<AMD_Character>(GetPawn());

	// 상점에서 멀어진 플레이어가 보관한 UI나 임의 RPC로 결제하지 못하도록 하는 거리 검사
	if (IsValid(RequestingCharacter) == false || FVector::Dist(RequestingCharacter->GetActorLocation(), Merchant->GetActorLocation()) > 500.0f)
	{
		// 거부된 요청의 전체 식별자와 실패 결과를 소유 클라이언트에 전달
		ClientReceiveShopPurchaseResult(Merchant, ItemAssetId, EMD_ShopPurchaseResult::InvalidRequest);
		return;
	}

	// 카탈로그와 서버 경제 원장을 소유한 상인 액터에 최종 구매 판정을 위임하는 처리
	const EMD_ShopPurchaseResult PurchaseResult = Merchant->TryPurchaseItem(RequestingCharacter, ItemAssetId);
	// 구매한 전체 식별자를 유지한 서버 결과 응답
	ClientReceiveShopPurchaseResult(Merchant, ItemAssetId, PurchaseResult);
}

void AMD_PlayerController::ClientReceiveShopPurchaseResult_Implementation(AMD_Merchant* Merchant, FPrimaryAssetId ItemAssetId, EMD_ShopPurchaseResult Result)
{
	// 다른 상인의 로컬 위젯에 결과가 섞이지 않도록 요청 대상 액터에 직접 전달하는 처리
	if (IsValid(Merchant) == true)
	{
		// 정확한 품목의 표시 이름을 조회할 전체 식별자 전달
		Merchant->ReceiveShopPurchaseResult(ItemAssetId, Result);
	}
}

// 서버가 트래블하기 직전에 각 소유 클라이언트에서 로딩 화면을 시작시킨다.
// 서버 화면만 덮으면 원격 클라이언트는 아무 예고 없이 멈춘 것처럼 보인다.
void AMD_PlayerController::ClientPrepareLoadingTransition_Implementation(EMD_LoadingContext Context)
{
	if (UMD_LoadingScreenSubsystem* LoadingScreen = GetGameInstance()->GetSubsystem<UMD_LoadingScreenSubsystem>())
	{
		LoadingScreen->BeginLoadingScreen(Context);
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
		UCommonActivatableWidget* PushedScreen = LocalUIManager->PushScreenToLayer(MDGameplayTags::UI_Layer_GameHUD, HUDWidgetClass);
		HUDWidgetInstance = Cast<UMD_HUDWidget>(PushedScreen);
		if (IsValid(HUDWidgetInstance) == false)
		{
			return;
		}

		// Project Settings에서 자동 결정한 Layer에 생성된 HUD의 입력 정책을 적용한다.
		HUDWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void AMD_PlayerController::ClientNotifyPlayerJoinAndExit_Implementation(const FText& PlayerName, bool bIsJoin)
{
	// 수신 컨트롤러의 로컬 UI 관리자 조회
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	// 로컬 플레이어가 없으면 알림 생략
	if (IsValid(LocalPlayer) == false)
	{
		return;
	}
	// 수신 플레이어별 GameHUD 검색을 위한 관리자 참조
	UMD_LocalUIManager* UIManager = LocalPlayer->GetSubsystem<UMD_LocalUIManager>();
	// UI 관리자가 없으면 알림 생략
	if (IsValid(UIManager) == false)
	{
		return;
	}

	// GameHUD 레이어에서 현재 표시 중인 HUD 선택
	UCommonActivatableWidget* ActiveScreen = UIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameHUD);
	// HUD 전용 참여 및 퇴장 이벤트 접근을 위한 형변환
	UMD_HUDWidget* HUD = Cast<UMD_HUDWidget>(ActiveScreen);
	// 활성 HUD가 없으면 보관 없이 알림 생략
	if (IsValid(HUD) == false)
	{
		return;
	}

	// 수신 시점에 존재하는 HUD의 Blueprint 이벤트로 즉시 전달
	HUD->HandlePlayerJoinAndExit(PlayerName, bIsJoin);
}

void AMD_PlayerController::ClientShowHUDMessage_Implementation(const FText& Message)
{
	// HUD 위젯이 알림 이벤트를 구현하기 전까지 화면에서 확인할 임시 표시 — 폼건·지지대 실패 메시지와 같은 방식
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, Message.ToString());
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UMD_LocalUIManager* UIManager = IsValid(LocalPlayer) ? LocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	if (IsValid(UIManager) == false)
	{
		return;
	}

	UMD_HUDWidget* HUD = Cast<UMD_HUDWidget>(UIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameHUD));
	if (IsValid(HUD) == true)
	{
		HUD->HandleHUDMessage(Message);
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


void AMD_PlayerController::BeginPlay()
{
	Super::BeginPlay();
	InitializeGameplayHUD();

	if (IsLocalController() == false)
	{
		return;
	}

	// 플랫폼 로그인이 완료된 로컬 사용자의 닉네임을 서버에 제출한다.
	SubmitPlatformNickname();

	if (!ApplicationWillDeactivateHandle.IsValid())
	{
		ApplicationWillDeactivateHandle =
			FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(
				this,
				&AMD_PlayerController::HandleApplicationWillDeactivate);
	}

}

void AMD_PlayerController::BeginPlayingState()
{
	Super::BeginPlayingState();

	InitializeGameplayHUD();
	SubmitPlatformNickname();
	RestoreAppearanceFromLocalSave();

	NotifyLoadingScreenPlayerReady();
}

void AMD_PlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	InitializeGameplayHUD();
	SubmitPlatformNickname();
	RestoreAppearanceFromLocalSave();

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
	}
}

void AMD_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 월드 종료 후 원장 변경 응답이 발생하지 않도록 구독 정리
    UnbindDailyLedger();
	if (ApplicationWillDeactivateHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(
			ApplicationWillDeactivateHandle);
		ApplicationWillDeactivateHandle.Reset();
	}

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


void AMD_PlayerController::ServerApplyAppearance_Implementation(FMD_CharacterAppearance NewAppearance)
{
	AMD_PlayerState* MDPlayerState = GetPlayerState<AMD_PlayerState>();
	if (IsValid(MDPlayerState) == true)
	{
		MDPlayerState->SetAppearance(NewAppearance);
	}
}

void AMD_PlayerController::RestoreAppearanceFromLocalSave()
{
	if (IsLocalController() == false)
	{
		return;
	}

	// 마지막으로 확정한 선택을 서버에 다시 실어 재접속·트래블 후에도 유지한다
	FMD_CharacterAppearance SavedAppearance;
	if (UMD_CharacterAppearanceLibrary::LoadLocalAppearance(SavedAppearance) == true)
	{
		ServerApplyAppearance(SavedAppearance);
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

AMD_WallUtility* AMD_PlayerController::FindUtilityToRepair(const float SearchRadiusCm) const
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);

	// 1) 조준선 우선. 배관이 여러 개 붙어 있을 때 어느 것을 고쳤는지 헷갈리지 않게 한다.
	FHitResult Hit;
	FCollisionQueryParams Params;
	if (const APawn* const OwnPawn = GetPawn())
	{
		Params.AddIgnoredActor(OwnPawn);
	}
	if (World->LineTraceSingleByChannel(
			Hit,
			ViewLocation,
			ViewLocation + ViewRotation.Vector() * SearchRadiusCm,
			ECC_Visibility,
			Params))
	{
		AMD_WallUtility* const Aimed = Cast<AMD_WallUtility>(Hit.GetActor());
		if (Aimed && Aimed->GetUtilityState() == EMD_UtilityState::Broken)
		{
			return Aimed;
		}
	}

	// 2) 조준이 빗나갔으면 반경 안에서 가장 가까운 파손된 인프라.
	//    파손된 것만 고르므로 명령을 반복해 4개를 차례로 고칠 수 있다.
	AMD_WallUtility* Closest = nullptr;
	float ClosestDistanceSquared = FMath::Square(SearchRadiusCm);
	for (TActorIterator<AMD_WallUtility> It(World); It; ++It)
	{
		AMD_WallUtility* const Utility = *It;
		if (!Utility || Utility->GetUtilityState() != EMD_UtilityState::Broken)
		{
			continue;
		}

		const float DistanceSquared =
			FVector::DistSquared(Utility->GetActorLocation(), ViewLocation);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			Closest = Utility;
		}
	}
	return Closest;
}

void AMD_PlayerController::MD_RepairUtility()
{
	AMD_WallUtility* const Utility = FindUtilityToRepair();
	if (!Utility)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("MD_RepairUtility: 조준선과 주변 20m 안에 파손된 배관·전선이 없다"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("MD_RepairUtility: %s 수리 요청"), *Utility->GetName());
	ServerRepairUtility(Utility);
}

void AMD_PlayerController::MD_SpawnDay3Sample()
{
#if !UE_BUILD_SHIPPING
	ServerSpawnDay3Sample();
#endif
}

void AMD_PlayerController::ServerSpawnDay3Sample_Implementation()
{
#if !UE_BUILD_SHIPPING
	const UMD_QuestDefinition* Definition = UMD_QuestDefinition::LoadDefinition(
		FPrimaryAssetId(TEXT("Quest"), TEXT("X7-M03")));
	if (IsValid(Definition))
	{
		for (TActorIterator<AMD_QuestSampleSpawner> It(GetWorld()); It; ++It)
		{
			if (It->DebugSpawnQuestSample(*Definition, 1))
			{
				ClientMessage(TEXT("MD_SpawnDay3Sample: Day3 sample is ready at the Isolation B spawner (existing samples are reused)."));
				return;
			}
		}
	}
	ClientMessage(TEXT("MD_SpawnDay3Sample: no matching Day3 spawner or valid quest sample class in this map."));
#endif
}

void AMD_PlayerController::ServerRepairUtility_Implementation(AMD_WallUtility* Utility)
{
	if (!IsValid(Utility))
	{
		return;
	}

	// 장착 도구 검사를 건너뛰는 치트 경로. 정상 수리는 Interact 가 도구 태그를 확인한다.
	Utility->DebugForceRepair(GetPawn());

	UE_LOG(LogTemp, Log, TEXT("MD_RepairUtility: %s 상태 = %s"),
		*Utility->GetName(),
		*StaticEnum<EMD_UtilityState>()->GetNameStringByValue(
			static_cast<int64>(Utility->GetUtilityState())));
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


void AMD_PlayerController::RequestDailyLedger()
{
    // 로컬 화면 요청만 서버 원장 조회로 전달
    if (IsLocalController() == false)
    {
        return;
    }
    // 이전 활성화의 지연 응답을 구분할 요청 번호 갱신
    DailyLedgerRequestId = DailyLedgerRequestId == MAX_int32 ? 1 : DailyLedgerRequestId + 1;
    // 서버 원장 조회와 변경 구독 요청
    ServerRequestDailyLedger(DailyLedgerRequestId);
}

void AMD_PlayerController::StopDailyLedger()
{
    // 로컬 화면에서 시작한 구독만 종료
    if (IsLocalController() == false)
    {
        return;
    }
    // 정리할 서버 구독의 요청 번호 보관
    int32 PreviousRequestId = DailyLedgerRequestId;
    // 전송 중이던 응답을 로컬에서 먼저 무효화
    DailyLedgerRequestId = DailyLedgerRequestId == MAX_int32 ? 1 : DailyLedgerRequestId + 1;
    // 서버의 해당 구독만 종료
    ServerStopDailyLedger(PreviousRequestId);
}

void AMD_PlayerController::ServerRequestDailyLedger_Implementation(int32 RequestId)
{
    // 동일 컨트롤러의 이전 구독 정리
    UnbindDailyLedger();
    // 새 응답의 요청 번호 저장
    ServerDailyLedgerRequestId = RequestId;
    // 현재 서버 월드 조회
    UWorld* World = GetWorld();
    if (IsValid(World) == false)
    {
        return;
    }
    // 점수와 크레딧 원장을 조합하는 서버 소유자 조회
    AMD_GameStateBase* GameState = World->GetGameState<AMD_GameStateBase>();
    if (IsValid(GameState) == false)
    {
        return;
    }
    // 최초 응답 전에 최신 원장 구성
    GameState->RefreshDailyLedger();
    // 이후 원장 변경에 대한 응답 RPC 연결
    GameState->OnDailyLedgerChanged.AddUObject(this, &AMD_PlayerController::SendDailyLedger);
    // 구독 해제와 응답 조회에 사용할 서버 원장 보관
    DailyLedgerSource = GameState;
    // 현재 원장의 최초 응답 전송
    SendDailyLedger();
}

void AMD_PlayerController::ServerStopDailyLedger_Implementation(int32 RequestId)
{
    // 오래된 종료 요청이 새 활성화의 구독을 해제하지 않도록 검사
    if (RequestId != ServerDailyLedgerRequestId)
    {
        return;
    }
    // 현재 화면 요청의 구독 정리
    UnbindDailyLedger();
}

void AMD_PlayerController::SendDailyLedger()
{
    // 서버에서 연결한 원장 소유자 조회
    AMD_GameStateBase* GameState = DailyLedgerSource.Get();
    if (IsValid(GameState) == false)
    {
        return;
    }
    // 요청한 플레이어의 소유 연결에만 데이터 전달
    ClientReceiveDailyLedger(ServerDailyLedgerRequestId, GameState->GetDailyLedger());
}

void AMD_PlayerController::ClientReceiveDailyLedger_Implementation(int32 RequestId, FMD_InspectionReport Report)
{
    // 닫혔거나 재활성화된 화면의 이전 응답 폐기
    if (RequestId != DailyLedgerRequestId)
    {
        return;
    }
    // 로컬 UI는 RPC 응답으로 받은 기록만 표시
    OnDailyLedgerReceived.Broadcast(Report);
}

void AMD_PlayerController::UnbindDailyLedger()
{
    // 서버 원장 이벤트 구독 대상 조회
    AMD_GameStateBase* GameState = DailyLedgerSource.Get();
    if (IsValid(GameState) == true)
    {
        // 현재 컨트롤러의 원장 변경 수신 해제
        GameState->OnDailyLedgerChanged.RemoveAll(this);
    }
    // 이전 월드와 구독 번호 정리
    DailyLedgerSource.Reset();
    // 전송할 활성 요청이 없는 상태 표시
    ServerDailyLedgerRequestId = 0;
}

void AMD_PlayerController::ClientShowDayEndReport_Implementation(FGameplayTag ReportWidgetTag, FMD_InspectionReport Report, double EndServerTime)
{
    // 실제 화면을 소유한 로컬 플레이어만 UI 처리
    if (IsLocalController() == false || IsValid(GetLocalPlayer()) == false)
    {
        return;
    }
    // 화면 등록 및 생성에 필요한 범용 레이어 관리자 조회
    UMD_LocalUIManager* UIManager = GetLocalPlayer()->GetSubsystem<UMD_LocalUIManager>();
    if (IsValid(UIManager) == false)
    {
        return;
    }
    // BP 태그가 지정한 화면 클래스와 실제 부모 레이어 조회
    FGameplayTag LayerTag;
    // 등록된 보고서 클래스의 동기 로드 대상
    TSoftClassPtr<UCommonActivatableWidget> RegisteredClass;
    if (UIManager->ResolveWidgetRegistration(ReportWidgetTag, LayerTag, RegisteredClass) == false)
    {
        return;
    }
    // 최종 정산 데이터를 받을 보고서 클래스만 사용
    UClass* ReportClass = RegisteredClass.LoadSynchronous();
    if (IsValid(ReportClass) == false || ReportClass->IsChildOf(UMD_DailyReportWidget::StaticClass()) == false)
    {
        return;
    }
    // 새 화면과 최상단 재사용 화면의 초기화 중복 방지
    bool bReportInitialized = false;
    // 활성화 전에 실시간 원장 요청을 멈추고 확정된 보고서 구성
    auto InitializeReport = [&Report, &bReportInitialized, EndServerTime](UCommonActivatableWidget& Screen)
    {
        // 확인된 보고서 클래스의 실제 인스턴스 조회
        UMD_DailyReportWidget* ReportWidget = Cast<UMD_DailyReportWidget>(&Screen);
        if (IsValid(ReportWidget) == true)
        {
            // 최종 보고서 입력 잠금과 표시 데이터 적용
            ReportWidget->SetFinalReport(Report, EndServerTime);
            // 활성화 전 초기화 완료 표시
            bReportInitialized = true;
        }
    };
    // 요청자가 이미 열어둔 보고서는 재사용하고 다른 플레이어는 생성
    UCommonActivatableWidget* Screen = UIManager->PushScreenToLayerInitialized(LayerTag, ReportClass, InitializeReport);
    if (IsValid(Screen) == false)
    {
        return;
    }
    // UI Manager가 최상단을 재사용하여 콜백을 생략한 경우 보완
    if (bReportInitialized == false)
    {
        // 기존 화면도 같은 서버 확정 보고서로 전환
        InitializeReport(*Screen);
    }
}

void AMD_PlayerController::ClientCloseDayEndReport_Implementation()
{
    // 서버 전용 컨트롤러의 UI 접근 제외
    if (IsLocalController() == false || IsValid(GetLocalPlayer()) == false)
    {
        return;
    }
    // 화면 등록 및 생성에 필요한 범용 레이어 관리자 조회
    UMD_LocalUIManager* UIManager = GetLocalPlayer()->GetSubsystem<UMD_LocalUIManager>();
    if (IsValid(UIManager) == false)
    {
        return;
    }
    // 범용 조회 결과에서 이 플레이어의 확정 보고서만 선택
    for (UCommonActivatableWidget* Screen : UIManager->GetActiveScreens())
    {
        // 일반 메뉴와 보고서 화면 구분
        UMD_DailyReportWidget* ReportWidget = Cast<UMD_DailyReportWidget>(Screen);
        // 콘솔 확인 화면은 유지하고 확정 보고서만 종료
        if (IsValid(ReportWidget) == true)
        {
            // Common UI의 표준 비활성화 및 레이어 제거 처리
            ReportWidget->CloseFinalReport();
        }
    }
}

void AMD_PlayerController::ServerRequestShopSale_Implementation(AMD_Merchant* Merchant, FPrimaryAssetId ItemAssetId, int32 SlotIndex, int32 Quantity)
{
	// 컨트롤러가 소유한 실제 캐릭터만 판매자로 사용
	AMD_Character* SellingCharacter = Cast<AMD_Character>(GetPawn());
	// 잘못된 참조와 식별자 및 수량을 거래 이전에 거부
	if (IsValid(Merchant) == false || IsValid(SellingCharacter) == false || ItemAssetId.IsValid() == false || Quantity <= 0)
	{
		// 요청 거부를 소유 클라이언트에 전달
		ClientReceiveShopSaleResult(Merchant, EMD_ShopSaleResult::InvalidRequest);
		// 잘못된 요청의 서버 처리 종료
		return;
	}
	// 상인에서 거리와 카탈로그 및 소지 상태를 다시 검증한 결과 전달
	ClientReceiveShopSaleResult(Merchant, Merchant->TrySellItem(SellingCharacter, ItemAssetId, SlotIndex, Quantity));
}

void AMD_PlayerController::ClientReceiveShopSaleResult_Implementation(AMD_Merchant* Merchant, EMD_ShopSaleResult Result)
{
	// 판매를 요청한 상인이 살아 있는 경우에만 화면 갱신
	if (IsValid(Merchant) == true)
	{
		// 상인별 활성 화면으로 서버 확정 결과 전달
		Merchant->ReceiveShopSaleResult(Result);
	}
}
