// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/MD_PlayerController.h"
#include "UI/Game/MD_HUDWidget.h"
#include "UI/Equipment/MD_EquipHotbarWidget.h"

#include "Engine/World.h"
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

#include "Engine/GameInstance.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"

#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Character/MD_Character.h"
#include "World/Merchant/MD_Merchant.h"

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

	// 난이도는 URL 옵션으로 실어 보낸다. 도착 맵 GameMode가 InitGame에서 ParseOption으로 받는다
	const FString TravelURL = FString::Printf(
		TEXT("%s?listen?Difficulty=%d"), *MapPackage, static_cast<int32>(Difficulty));

	GetWorld()->ServerTravel(TravelURL);
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

	if (IsValid(HUDWidgetClass) == true)
	{
		if (IsValid(HUDWidgetInstance) == false)
		{
			// Pawn 소유가 확정된 컨트롤러에 플레이어 상태를 참조할 메인 HUD 생성
			HUDWidgetInstance = CreateWidget<UMD_HUDWidget>(this, HUDWidgetClass);
		}

		if (IsValid(HUDWidgetInstance) == true)
		{
			if (HUDWidgetInstance->IsInViewport() == false)
			{
				// 맵 전환 과정에서 화면에서 제거된 메인 HUD 재연결
				HUDWidgetInstance->AddToViewport();
			}
			HUDWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	if (IsValid(EquipHotbarWidgetClass) == true)
	{
		if (IsValid(EquipHotbarWidgetInstance) == false)
		{
			// 소유 Pawn의 장비 상태를 참조할 핫바 생성
			EquipHotbarWidgetInstance = CreateWidget<UMD_EquipHotbarWidget>(this, EquipHotbarWidgetClass);
		}

		if (IsValid(EquipHotbarWidgetInstance) == true)
		{
			if (EquipHotbarWidgetInstance->IsInViewport() == false)
			{
				// 맵 전환 과정에서 화면에서 제거된 핫바 재연결
				EquipHotbarWidgetInstance->AddToViewport();
			}
			EquipHotbarWidgetInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
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
		Merchant->AddCredits(Amount);
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
}

void AMD_PlayerController::BeginPlayingState()
{
	Super::BeginPlayingState();

	InitializeGameplayHUD();
}

void AMD_PlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	InitializeGameplayHUD();
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

void AMD_PlayerController::SetGameplayHUDVisible(bool bVisible)
{
	// Collapsed로 두면 레이아웃 계산에서도 빠지고, 되돌릴 때 SelfHitTestInvisible이라
	// HUD가 마우스 입력을 가로채지 않는다 (원래 AddToViewport 기본 동작과 동일).
	const ESlateVisibility Target =
		bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;

	if (IsValid(HUDWidgetInstance))
	{
		HUDWidgetInstance->SetVisibility(Target);
	}

	if (IsValid(EquipHotbarWidgetInstance))
	{
		EquipHotbarWidgetInstance->SetVisibility(Target);
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

	// 퀘스트 포기 요청 성공 시 점수 감소
	if (Merchant->TryAbandonRepeatQuest(Index) == true)
	{
		MDGameState->AddScoreDelta(-100);
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
		Merchant->BeginContract(DebugSite, DebugAdvancePayment);
	}
}

void AMD_PlayerController::MD_BeginDay(int32 Day)
{
	ServerBeginDay(Day);
}

void AMD_PlayerController::ServerBeginDay_Implementation(int32 Day)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->BeginDay(Day);
	}
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
	const FGameplayTag GradeTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("Resource.Sample.Grade.%s"), *GradeName)),/*ErrorIfNotFound=*/false);

	ServerDeliver(GradeTag, bIntact);
}

void AMD_PlayerController::ServerDeliver_Implementation(FGameplayTag GradeTag, bool bIntact)
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->NotifySampleDelivered(GradeTag, bIntact);
	}
}

void AMD_PlayerController::MD_MarkSiteCleared()
{
	ServerMarkSiteCleared();
}

void AMD_PlayerController::ServerMarkSiteCleared_Implementation()
{
	if (UMD_MerchantSubsystem* Merchant = GetMerchantSubsystem())
	{
		Merchant->MarkSiteCleared();
	}
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
