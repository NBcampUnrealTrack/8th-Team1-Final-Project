// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Merchant/MD_Merchant.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "UObject/ConstructorHelpers.h"

#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Lore/MD_LoreComponent.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Component/Oxygen/MD_OxygenComponent.h"
#include "Component/Support/MD_SupplyComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Data/Merchant/MD_SiteDefinition.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Merchant/MD_MerchantSubsystem.h"
#include "Player/MD_PlayerController.h"
#include "UI/Merchant/MD_LoanWidget.h"
#include "Player/MD_PlayerState.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"
#include "UI/Merchant/MD_QuestWidget.h"
#include "UI/Merchant/MD_ShopWidget.h"

namespace
{
	/** 임시 지지대 구매를 기존 설치 수량 컴포넌트로 전달하기 위한 품목 식별자 */
	const FName SupportItemId(TEXT("IT-P1"));

	/** 산소 확장 탱크 구매 직후 패시브 효과를 장착하기 위한 품목 식별자 */
	const FName OxygenTankItemId(TEXT("IT-O2"));

	/**
	 * 캐릭터 에셋 대체용 큐브를 사람 크기(60×60×180cm)로 맞추는 값.
	 * 엔진 기본 큐브가 100cm 정육면체라 스케일로 눌러 세운다.
	 */
	constexpr float PlaceholderWidthScale = 0.6f;
	constexpr float PlaceholderHeightScale = 1.8f;

	/** 큐브 피벗이 가운데라 절반 높이만큼 올려야 액터 원점이 발밑에 온다. */
	constexpr float PlaceholderHalfHeight = 90.f;

	/** bPricePerUnit 값에 따라 ToolDefinition 수량 증가 또는 동일 GameplayTag 항목 교체를 수행하는 처리 */
	bool FulfillPurchasedItem(AMD_Character* PurchasingCharacter, const FMD_ShopItem& ShopItem)
	{
		if (IsValid(PurchasingCharacter) == false || PurchasingCharacter->HasAuthority() == false || ShopItem.ItemId.IsNone() == true)
		{
			return false;
		}

		UMD_EquipHotbarComponent* HotbarComponent = PurchasingCharacter->GetHotbarComponent();
		if (IsValid(HotbarComponent) == false)
		{
			return false;
		}

		// 지지대 설치 어빌리티가 기존 수량 컴포넌트를 원본으로 사용하기 위한 직접 지급 처리
		if (ShopItem.ItemId == SupportItemId)
		{
			UMD_SupplyComponent* SupplyComponent = PurchasingCharacter->FindComponentByClass<UMD_SupplyComponent>();
			if (IsValid(SupplyComponent) == false || SupplyComponent->GetSupportCount() >= MAX_int32)
			{
				return false;
			}

			SupplyComponent->AddSupport(1);
			PurchasingCharacter->ForceNetUpdate();
			return true;
		}

		// Hotbar 품목 데이터로 사용할 카탈로그 ToolDefinition 참조
		UMD_ToolDefinition* PurchasedToolDefinition = ShopItem.EquipmentDefinition.Get();
		if (IsValid(PurchasedToolDefinition) == false)
		{
			return false;
		}

		// 개당 판매 품목은 동일 ToolDefinition 항목의 수량을 증가시키는 처리
		if (ShopItem.bPricePerUnit == true)
		{
			return HotbarComponent->AddTool(PurchasedToolDefinition, 1);
		}

		// 탱크 패시브 적용 실패 전에 소유 원장이 변경되지 않도록 하는 필수 컴포넌트 검사
		UMD_OxygenComponent* OxygenComponent = ShopItem.ItemId == OxygenTankItemId ? PurchasingCharacter->GetOxygenComponent() : nullptr;
		if (ShopItem.ItemId == OxygenTankItemId && IsValid(OxygenComponent) == false)
		{
			return false;
		}

		// 일회성 판매 품목은 동일 GameplayTag 항목을 교체하거나 빈 항목에 추가하는 처리
		if (HotbarComponent->ReplaceTool(PurchasedToolDefinition) == false)
		{
			return false;
		}

		// 산소 탱크 아이템 효과 적용
		if (ShopItem.ItemId == OxygenTankItemId)
		{
			OxygenComponent->SetOxygenTankEquipped(true);
		}

		return true;
	}
}

AMD_Merchant::AMD_Merchant()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// Mesh가 루트가 아니어야 이 값들이 배치 트랜스폼에 덮이지 않는다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
	Mesh->SetRelativeScale3D(FVector(PlaceholderWidthScale, PlaceholderWidthScale, PlaceholderHeightScale));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, PlaceholderHalfHeight));
}

void AMD_Merchant::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

FText AMD_Merchant::GetInteractionDisplayName_Implementation() const
{
	return MerchantDisplayName;
}

void AMD_Merchant::Interact_Implementation(AActor* Interactor)
{
	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);

	if (!IsValid(InstigatorCharacter))
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(InstigatorCharacter->GetController());

	// UI는 로컬 플레이어에게만
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}

	if (!MenuWidgetClass)
	{
		return;
	}

	// 이미 열려 있으면 중복 생성하지 않음
	if (ActiveMenuWidget.IsValid() && ActiveMenuWidget->IsInViewport())
	{
		return;
	}

	UMD_MerchantMenuWidget* Widget = CreateWidget<UMD_MerchantMenuWidget>(PC, MenuWidgetClass);
	if (!IsValid(Widget))
	{
		return;
	}

	Widget->SetOwningMerchant(this);
	Widget->OnExitRequested.AddDynamic(this, &AMD_Merchant::HandleMenuExitRequested);
	Widget->OnShopRequested.AddDynamic(this, &AMD_Merchant::HandleShopRequested);
	Widget->OnQuestRequested.AddDynamic(this, &AMD_Merchant::HandleQuestRequested);

	if (const UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
	{
		Widget->SetMenuInfo(MerchantComponent->BuildMenuInfo());
	}

	Widget->OnLoanRequested.AddDynamic(this, &AMD_Merchant::HandleLoanRequested);

	// 대화는 메뉴 위젯이 자체 대사창으로 처리한다.
	// 치료(70₡/회, `07` §2.5)는 대화 안에서 처리하기로 정해졌다 → 대화 화면 담당.

	Widget->AddToViewport();
	ActiveMenuWidget = Widget;

	PC->SetShowMouseCursor(true);
	PC->SetInputMode(FInputModeGameAndUI());
}

void AMD_Merchant::HandleMenuExitRequested()
{
	// 위젯 자체 제거와 입력 모드 복구는 위젯이 처리한다.
	// 상점·퀘스트가 위에 떠 있는 상태에서 메뉴가 닫히면 같이 정리한다.
	if (ActiveShopWidget.IsValid())
	{
		UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
		if (MerchantComponent != nullptr)
		{
			MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForShop);
		}

		AMD_PlayerState* InteractingPlayerState = GetInteractingPlayerState();
		if (IsValid(InteractingPlayerState) == true)
		{
			InteractingPlayerState->OnShopOwnershipChanged.RemoveDynamic(this, &AMD_Merchant::HandlePlayerShopOwnershipChanged);
		}

		ActiveShopWidget->RemoveFromParent();
		ActiveShopWidget.Reset();
	}

	if (ActiveQuestWidget.IsValid())
	{
		if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
		{
			MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
		}
		if (UMD_LoreComponent* LoreComponent = GetLoreComponent())
		{
			LoreComponent->OnLoreProgressUpdated.RemoveDynamic(this, &AMD_Merchant::HandleLoreProgressUpdated);
		}
		ActiveQuestWidget->RemoveFromParent();
		ActiveQuestWidget.Reset();
	}

	if (ActiveLoanWidget.IsValid() == true)
	{
		if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
		{
			MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
		}
		ActiveLoanWidget->RemoveFromParent();
		ActiveLoanWidget.Reset();
	}

	ActiveMenuWidget.Reset();
}

void AMD_Merchant::HandleShopRequested()
{
	if (!ShopWidgetClass)
	{
		return;
	}

	if (ActiveShopWidget.IsValid() && ActiveShopWidget->IsInViewport())
	{
		return;
	}

	// 메뉴를 연 플레이어를 그대로 쓴다. GetFirstPlayerController()를 쓰면
	// 리슨 서버에서 클라이언트가 상인을 눌렀을 때 호스트 화면에 상점이 뜬다.
	if (!ActiveMenuWidget.IsValid())
	{
		return;
	}

	APlayerController* PC = ActiveMenuWidget->GetOwningPlayer();
	if (!IsValid(PC))
	{
		return;
	}

	UMD_ShopWidget* ShopWidget = CreateWidget<UMD_ShopWidget>(PC, ShopWidgetClass);
	if (!IsValid(ShopWidget))
	{
		return;
	}

	ShopWidget->SetCatalog(ShopCatalog);

	ShopWidget->OnBackRequested.AddDynamic(this, &AMD_Merchant::HandleShopBackRequested);
	ShopWidget->OnPurchaseRejected.AddDynamic(this, &AMD_Merchant::HandleShopPurchaseRejected);
	ShopWidget->OnItemPurchaseRequested.AddDynamic(this, &AMD_Merchant::HandleShopItemPurchaseRequested);

	// 메뉴 위젯 자체는 살려둔다 — 파괴하면 입력 모드가 초기화되고 대사창도 함께 사라진다.
	// 대신 메뉴 패널을 숨겨서 상점과 겹치지 않게 한다.
	ShopWidget->SetOwningMerchantMenu(ActiveMenuWidget.Get());
	ShopWidget->AddToViewport();
	ActiveShopWidget = ShopWidget;

	// 서버 원장의 복제 미러가 바뀌면 크레딧과 구매 가능 상태를 즉시 다시 계산하기 위한 구독
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForShop);
	}

	// 상점을 이용하는 플레이어의 장비 소유권 복제를 상점에 반영하기 위하여 이벤트 연결
	AMD_PlayerState* InteractingPlayerState = GetInteractingPlayerState();
	if (IsValid(InteractingPlayerState) == true)
	{
		InteractingPlayerState->OnShopOwnershipChanged.AddUniqueDynamic(this, &AMD_Merchant::HandlePlayerShopOwnershipChanged);
	}

	RefreshShopWidgetFromComponent();
}

void AMD_Merchant::HandleShopBackRequested()
{
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForShop);

		// 상점에서 차감된 크레딧을 다시 드러나는 루트 메뉴에도 즉시 반영하기 위한 갱신
		if (ActiveMenuWidget.IsValid() == true)
		{
			ActiveMenuWidget->SetMenuInfo(MerchantComponent->BuildMenuInfo());
		}
	}

	AMD_PlayerState* InteractingPlayerState = GetInteractingPlayerState();
	if (IsValid(InteractingPlayerState) == true)
	{
		InteractingPlayerState->OnShopOwnershipChanged.RemoveDynamic(this, &AMD_Merchant::HandlePlayerShopOwnershipChanged);
	}

	if (ActiveShopWidget.IsValid())
	{
		ActiveShopWidget->RemoveFromParent();
		ActiveShopWidget.Reset();
		// NativeDestruct에서 자동으로 메뉴 패널이 복구된다
	}
}

void AMD_Merchant::HandleNotReadyRequested()
{
	// 버튼을 숨기지 않는다 — 화면이 없으니 되돌아올 수단도 없다.
	if (ActiveMenuWidget.IsValid())
	{
		ActiveMenuWidget->ShowDialogueLine(NotReadyDialogue);
	}
}

void AMD_Merchant::HandleLoanRequested()
{
	if (IsValid(LoanWidgetClass) == false || ActiveMenuWidget.IsValid() == false)
	{
		return;
	}

	// 이미 생성된 UI가 있는 경우
	if (ActiveLoanWidget.IsValid() == true)
	{
		// Viewport에 없으면 추가
		if (ActiveLoanWidget->IsInViewport() == false)
		{
			ActiveLoanWidget->AddToViewport();
		}
		return;
	}

	// UI를 이용하는(소유하는) 플레이어의 Controller 얻기
	APlayerController* PlayerController = ActiveMenuWidget->GetOwningPlayer();
	if (IsValid(PlayerController) == false)
	{
		return;
	}

	// UI 생성
	UMD_LoanWidget* LoanWidget = CreateWidget<UMD_LoanWidget>(PlayerController, LoanWidgetClass);
	if (IsValid(LoanWidget) == false)
	{
		return;
	}

	// 초기 설정 진행
	LoanWidget->SetOwningMerchantMenu(ActiveMenuWidget.Get());
	LoanWidget->OnBackRequested.AddDynamic(this, &AMD_Merchant::HandleLoanBackRequested);
	LoanWidget->OnBorrowRequested.AddDynamic(this, &AMD_Merchant::HandleBorrowRequested);
	LoanWidget->OnRepayRequested.AddDynamic(this, &AMD_Merchant::HandleRepayRequested);
	LoanWidget->AddToViewport();
	ActiveLoanWidget = LoanWidget;

	if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
	{
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}

	RefreshLoanWidgetFromComponent();
}

void AMD_Merchant::HandleLoanBackRequested()
{
	if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}

	if (ActiveLoanWidget.IsValid() == true)
	{
		ActiveLoanWidget->RemoveFromParent();
		ActiveLoanWidget.Reset();
	}
}

void AMD_Merchant::HandleBorrowRequested(int32 Amount)
{
	AMD_PlayerController* InteractingPlayerController = GetInteractingPlayerController();
	if (IsValid(InteractingPlayerController) == true)
	{
		InteractingPlayerController->ServerBorrowCredits(this, Amount);
	}
}

void AMD_Merchant::HandleRepayRequested(int32 Amount)
{
	AMD_PlayerController* InteractingPlayerController = GetInteractingPlayerController();
	if (IsValid(InteractingPlayerController) == true)
	{
		InteractingPlayerController->ServerRepayLoan(this, Amount);
	}
}

void AMD_Merchant::HandleShopPurchaseRejected(FName ItemId)
{
	// 대사창은 메뉴 위젯에 있다. 상점은 그 위에 겹쳐 뜨지만 하단을 비워두므로 가려지지 않는다.
	if (ActiveMenuWidget.IsValid())
	{
		ActiveMenuWidget->ShowDialogueLine(FullStockDialogue);
	}
}

void AMD_Merchant::HandleShopItemPurchaseRequested(FName ItemId)
{
	// 소유 컨트롤러를 통해서만 서버 RPC를 보낼 수 있도록 하는 구매 요청 전달
	AMD_PlayerController* InteractingPlayerController = GetInteractingPlayerController();
	if (InteractingPlayerController != nullptr)
	{
		InteractingPlayerController->ServerRequestShopPurchase(this, ItemId);
	}
}

void AMD_Merchant::HandleMerchantStateChangedForShop()
{
	RefreshShopWidgetFromComponent();
}

void AMD_Merchant::HandlePlayerShopOwnershipChanged()
{
	RefreshShopWidgetFromComponent();
}

EMD_ShopPurchaseResult AMD_Merchant::TryPurchaseItem(AMD_Character* PurchasingCharacter, FName ItemId)
{
	// 클라이언트 액터에서 경제 원장을 변경하지 못하도록 하는 서버 권한 검사
	if (HasAuthority() == false || IsValid(PurchasingCharacter) == false || PurchasingCharacter->HasAuthority() == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 서버가 보유한 카탈로그에 없는 식별자를 이용한 가격 조작을 막는 품목 조회
	FMD_ShopItem ShopItem;
	if (IsValid(ShopCatalog) == false || ShopCatalog->FindItem(ItemId, ShopItem) == false)
	{
		return EMD_ShopPurchaseResult::InvalidItem;
	}

	UMD_EquipHotbarComponent* HotbarComponent = PurchasingCharacter->GetHotbarComponent();
	if (IsValid(HotbarComponent) == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 상점 거래 식별자를 Hotbar 품목 데이터인 ToolDefinition으로 변환하는 처리
	UMD_ToolDefinition* PurchasedToolDefinition = ShopItem.EquipmentDefinition.Get();
	if (ShopItem.ItemId != SupportItemId && IsValid(PurchasedToolDefinition) == false)
	{
		return EMD_ShopPurchaseResult::InvalidItem;
	}

	// 일회성 품목의 동일 계열 교체에 필요한 GameplayTag 유효성 검사
	if (ShopItem.ItemId != SupportItemId && ShopItem.bPricePerUnit == false && PurchasedToolDefinition->ToolAbilityTag.IsValid() == false)
	{
		return EMD_ShopPurchaseResult::InvalidItem;
	}

	// ToolDefinition 하나로 수량 증가, 동일 태그 교체, 빈 항목 추가 가능성을 결제 전에 확인하는 검사
	if (ShopItem.ItemId != SupportItemId && (HotbarComponent->CanAddTool(PurchasedToolDefinition) == false || (ShopItem.bPricePerUnit == true && HotbarComponent->GetToolCount(PurchasedToolDefinition) >= MAX_int32)))
	{
		return EMD_ShopPurchaseResult::FullyStocked;
	}

	UMD_MerchantSubsystem* MerchantSubsystem = GetMerchantSubsystem();
	if (IsValid(MerchantSubsystem) == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 상점을 이용하는 플레이어의 PlayerState가 유효하지 않으면 구매 과정 중단
	AMD_PlayerState* PurchasingPlayerState = PurchasingCharacter->GetPlayerState<AMD_PlayerState>();
	if (IsValid(PurchasingPlayerState) == false)
	{
		return EMD_ShopPurchaseResult::InvalidRequest;
	}

	// 실제 플레이어 지급이 성공한 경우에만 팀 경제 원장의 결제를 확정하기 위한 콜백 전달
	EMD_ShopPurchaseResult PurchaseResult = MerchantSubsystem->TryPurchaseShopItem(PurchasingPlayerState, ShopItem, [PurchasingCharacter, ShopItem]()
		{
			const bool bWasSuccessful = FulfillPurchasedItem(PurchasingCharacter, ShopItem);
			return bWasSuccessful;
		});
	return PurchaseResult;
}

void AMD_Merchant::ReceiveShopPurchaseResult(FName ItemId, EMD_ShopPurchaseResult Result)
{
	// 닫힌 메뉴에 피드백을 남기지 않도록 하는 로컬 위젯 수명 검사
	if (ActiveMenuWidget.IsValid() == false)
	{
		return;
	}

	if (Result == EMD_ShopPurchaseResult::Success)
	{
		FMD_ShopItem PurchasedItem;
		const bool bHasPurchasedItem = IsValid(ShopCatalog) == true && ShopCatalog->FindItem(ItemId, PurchasedItem) == true;
		const FText ItemName = bHasPurchasedItem == true ? PurchasedItem.DisplayName : FText::FromName(ItemId);
		ActiveMenuWidget->ShowDialogueLine(FText::Format(NSLOCTEXT("MDMerchant", "ShopPurchaseSuccess", "{0}, 챙겨 둬."), ItemName));
		return;
	}

	if (Result == EMD_ShopPurchaseResult::FullyStocked)
	{
		ActiveMenuWidget->ShowDialogueLine(FullStockDialogue);
		return;
	}

	if (Result == EMD_ShopPurchaseResult::InsufficientCredits)
	{
		ActiveMenuWidget->ShowDialogueLine(InsufficientCreditsDialogue);
		return;
	}

	if (Result == EMD_ShopPurchaseResult::Locked)
	{
		ActiveMenuWidget->ShowDialogueLine(LockedItemDialogue);
		return;
	}

	ActiveMenuWidget->ShowDialogueLine(InvalidPurchaseDialogue);
}

void AMD_Merchant::HandleQuestRequested()
{
	if (!QuestWidgetClass)
	{
		return;
	}

	if (ActiveQuestWidget.IsValid() && ActiveQuestWidget->IsInViewport())
	{
		return;
	}

	if (!ActiveMenuWidget.IsValid())
	{
		return;
	}

	APlayerController* PC = ActiveMenuWidget->GetOwningPlayer();
	if (!IsValid(PC))
	{
		return;
	}

	UMD_QuestWidget* QuestWidget = CreateWidget<UMD_QuestWidget>(PC, QuestWidgetClass);
	if (!IsValid(QuestWidget))
	{
		return;
	}

	QuestWidget->OnBackRequested.AddDynamic(this, &AMD_Merchant::HandleQuestBackRequested);
	QuestWidget->OnRepeatAcceptRequested.AddDynamic(this, &AMD_Merchant::HandleRepeatAcceptRequested);
	QuestWidget->OnRepeatDeclineRequested.AddDynamic(this, &AMD_Merchant::HandleRepeatDeclineRequested);
	QuestWidget->OnRepeatAbandonRequested.AddDynamic(this, &AMD_Merchant::HandleRepeatAbandonRequested);

	QuestWidget->SetOwningMerchantMenu(ActiveMenuWidget.Get());
	QuestWidget->AddToViewport();
	ActiveQuestWidget = QuestWidget;

	// 상인 상태(수락·완료 등)가 바뀔 때마다 화면을 다시 그리도록 구독한다.
	// 리슨 서버 호스트든 원격 클라이언트든 이 델리게이트 하나로 갱신된다 (`MD_MerchantComponent` 참고).
	if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
	{
		MerchantComponent->OnMerchantStateChanged.AddDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}

	// 로어 수집 정보가 변경될 때만 열린 코덱스 탭을 갱신하기 위한 전용 이벤트 구독
	if (UMD_LoreComponent* LoreComponent = GetLoreComponent())
	{
		LoreComponent->OnLoreProgressUpdated.AddDynamic(this, &AMD_Merchant::HandleLoreProgressUpdated);
	}

	RefreshQuestWidgetFromComponent();
	RefreshLoreWidgetFromComponent();
}

void AMD_Merchant::HandleQuestBackRequested()
{
	if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}
	if (UMD_LoreComponent* LoreComponent = GetLoreComponent())
	{
		LoreComponent->OnLoreProgressUpdated.RemoveDynamic(this, &AMD_Merchant::HandleLoreProgressUpdated);
	}

	if (ActiveQuestWidget.IsValid())
	{
		ActiveQuestWidget->RemoveFromParent();
		ActiveQuestWidget.Reset();
	}
}

void AMD_Merchant::HandleRepeatAcceptRequested(int32 QuestIndex)
{
	if (AMD_PlayerController* InteractingPC = GetInteractingPlayerController())
	{
		InteractingPC->ServerAcceptRepeat(QuestIndex);
	}
}

void AMD_Merchant::HandleRepeatDeclineRequested(int32 QuestIndex)
{
	if (AMD_PlayerController* InteractingPC = GetInteractingPlayerController())
	{
		InteractingPC->ServerDeclineRepeat(QuestIndex);
	}
}

void AMD_Merchant::HandleRepeatAbandonRequested(int32 QuestIndex)
{
	// PlayerController의 반복 퀘스트 포기 요청 Server RPC 함수 호출
	AMD_PlayerController* InteractingPC = GetInteractingPlayerController();
	if (IsValid(InteractingPC) == true)
	{
		InteractingPC->ServerAbandonRepeatQuest(QuestIndex);
	}
}

void AMD_Merchant::HandleMerchantStateChanged()
{
	const UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (IsValid(MerchantComponent) == true && ActiveMenuWidget.IsValid() == true)
	{
		ActiveMenuWidget->SetMenuInfo(MerchantComponent->BuildMenuInfo());
	}

	RefreshQuestWidgetFromComponent();
	RefreshLoanWidgetFromComponent();
}

void AMD_Merchant::HandleLoreProgressUpdated()
{
	RefreshLoreWidgetFromComponent();
}

void AMD_Merchant::RefreshQuestWidgetFromComponent()
{
	if (!ActiveQuestWidget.IsValid())
	{
		return;
	}

	const UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (!IsValid(MerchantComponent))
	{
		return;
	}

	ActiveQuestWidget->SetMainQuests(MerchantComponent->GetMainQuests());
	ActiveQuestWidget->SetRepeatQuests(MerchantComponent->GetRepeatQuests());
}

void AMD_Merchant::RefreshLoreWidgetFromComponent()
{
	if (ActiveQuestWidget.IsValid() == false)
	{
		return;
	}

	UMD_LoreComponent* LoreComponent = GetLoreComponent();
	if (IsValid(LoreComponent) == false)
	{
		return;
	}

	// 상인과 상호작용 중인 플레이어가 선택한 탐사 현장을 찾기 위한 컨트롤러
	AMD_PlayerController* InteractingPlayerController = GetInteractingPlayerController();

	// 퀘스트 데이터와 분리된 현장별 로어 카탈로그 접근을 위한 탐사 현장 정의
	UMD_SiteDefinition* ExpeditionSite = IsValid(InteractingPlayerController) == true ? InteractingPlayerController->GetExpeditionSite() : nullptr;

	// 수집 원장의 식별자와 화면 표시 정의를 연결하기 위한 로어 카탈로그
	UMD_LoreCatalog* LoreCatalog = IsValid(ExpeditionSite) == true ? ExpeditionSite->LoreCatalog : nullptr;

	ActiveQuestWidget->SetLoreData(LoreCatalog, LoreComponent->GetDiscoveredLoreAssetIds());
}

void AMD_Merchant::RefreshShopWidgetFromComponent()
{
	if (ActiveShopWidget.IsValid() == false)
	{
		return;
	}

	const UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (IsValid(MerchantComponent) == false)
	{
		return;
	}

	// 소유권과 선행 품목 해금을 동일한 복제 스냅샷으로 계산하기 위한 목록 복사
	AMD_PlayerState* InteractingPlayerState = GetInteractingPlayerState();
	TArray<FName> OwnedItemIds = IsValid(InteractingPlayerState) == true ? InteractingPlayerState->GetPurchasedShopItemIds() : TArray<FName>();
	TArray<FName> FullyStockedItemIds;

	if (IsValid(ShopCatalog) == true)
	{
		for (const FMD_ShopItem& ShopItem : ShopCatalog->Items)
		{
			if (ShopItem.bPricePerUnit == false && OwnedItemIds.Contains(ShopItem.ItemId) == true)
			{
				FullyStockedItemIds.Add(ShopItem.ItemId);
			}
		}
	}

	ActiveShopWidget->SetShopInfo(MerchantComponent->BuildMenuInfo());
	ActiveShopWidget->SetProgress(MerchantComponent->GetQuestCompletions(), OwnedItemIds);
	ActiveShopWidget->SetFullyStockedItems(FullyStockedItemIds);
}

void AMD_Merchant::RefreshLoanWidgetFromComponent()
{
	if (ActiveLoanWidget.IsValid() == false)
	{
		return;
	}

	const UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (IsValid(MerchantComponent) == false)
	{
		return;
	}

	ActiveLoanWidget->SetLoanInfo(MerchantComponent->BuildMenuInfo());
}

UMD_MerchantComponent* AMD_Merchant::GetMerchantComponent() const
{
	const UWorld* World = GetWorld();
	const AMD_GameStateBase* GameState = IsValid(World) ? World->GetGameState<AMD_GameStateBase>() : nullptr;

	return IsValid(GameState) ? GameState->GetMerchantComponent() : nullptr;
}

UMD_LoreComponent* AMD_Merchant::GetLoreComponent() const
{
	const UWorld* World = GetWorld();
	const AMD_GameStateBase* GameState = IsValid(World) == true ? World->GetGameState<AMD_GameStateBase>() : nullptr;
	return IsValid(GameState) == true ? GameState->GetLoreComponent() : nullptr;
}

UMD_MerchantSubsystem* AMD_Merchant::GetMerchantSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return IsValid(GameInstance) == true ? GameInstance->GetSubsystem<UMD_MerchantSubsystem>() : nullptr;
}

AMD_PlayerController* AMD_Merchant::GetInteractingPlayerController() const
{
	return ActiveMenuWidget.IsValid() ? Cast<AMD_PlayerController>(ActiveMenuWidget->GetOwningPlayer()) : nullptr;
}

AMD_PlayerState* AMD_Merchant::GetInteractingPlayerState() const
{
	const AMD_PlayerController* InteractingPlayerController = GetInteractingPlayerController();
	return IsValid(InteractingPlayerController) == true ? InteractingPlayerController->GetPlayerState<AMD_PlayerState>() : nullptr;
}
