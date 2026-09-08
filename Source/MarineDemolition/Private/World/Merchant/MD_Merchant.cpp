// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Merchant/MD_Merchant.h"

#include "CommonActivatableWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
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
#include "UI/MD_LocalUIManager.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"
#include "UI/Merchant/MD_QuestScreenWidget.h"
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

	// 에디터에서 지정한 상인 메뉴 화면 요청 태그를 검증한다.
	bool bMenuWidgetTagValid = MenuWidgetTag.IsValid();
	if (bMenuWidgetTagValid == false)
	{
		return;
	}

	// 현재 상인 메뉴가 이미 활성화되어 있으면 중복 Push를 막는다.
	if (ActiveMenuWidget.IsValid() == true)
	{
		bool bMenuActivated = ActiveMenuWidget->IsActivated();
		if (bMenuActivated == true)
		{
			return;
		}
	}

	// 현재 플레이어가 소유한 중앙 UIManager를 조회한다.
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (IsValid(LocalPlayer) == false)
	{
		return;
	}

	UMD_LocalUIManager* UIManager = LocalPlayer->GetSubsystem<UMD_LocalUIManager>();
	if (IsValid(UIManager) == false)
	{
		return;
	}

	// 화면 태그로 등록된 클래스와 부모 Layer를 UIManager가 결정하여 Push한다.
	UCommonActivatableWidget* PushedScreen = UIManager->PushScreenToLayerByTag(MenuWidgetTag);
	if (IsValid(PushedScreen) == false)
	{
		return;
	}

	// 잘못 등록된 화면은 Stack에 남기지 않고 요청을 종료한다.
	UMD_MerchantMenuWidget* Widget = Cast<UMD_MerchantMenuWidget>(PushedScreen);
	if (IsValid(Widget) == false)
	{
		PushedScreen->DeactivateWidget();
		return;
	}

	// Push된 상인 메뉴에 소유 상인과 도메인 이벤트를 연결한다.
	Widget->SetOwningMerchant(this);
	Widget->OnExitRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleMenuExitRequested);

	// 현재 상인 상태를 Push된 메뉴에 즉시 반영한다.
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		Widget->SetMenuInfo(MerchantComponent->BuildMenuInfo());
	}

	// 대화는 메뉴 위젯이 자체 대사창으로 처리한다.
	// 치료(70₡/회, `07` §2.5)는 대화 안에서 처리하기로 정해졌다 → 대화 화면 담당.

	// 상태 갱신과 하위 화면 연결에 사용할 활성 메뉴를 저장한다.
	ActiveMenuWidget = Widget;
}

void AMD_Merchant::HandleMenuExitRequested()
{
	// 위젯 자체 제거와 입력 모드 복구는 위젯이 처리한다.
	// 상점·퀘스트가 위에 떠 있는 상태에서 메뉴가 닫히면 같이 정리한다.
	UMD_MerchantMenuWidget* MenuWidget = ActiveMenuWidget.Get();
	if (IsValid(MenuWidget) == true)
	{
		// UIManager가 재사용할 수 있는 메뉴 인스턴스에서 현재 상인 연결을 제거한다.
		MenuWidget->OnExitRequested.RemoveDynamic(this, &AMD_Merchant::HandleMenuExitRequested);
	}

	if (ActiveShopWidget.IsValid() == true)
	{
		// 실제 상태 정리는 상점의 공통 비활성화 이벤트에서 한 번만 처리한다.
		ActiveShopWidget->DeactivateWidget();
	}

	if (ActiveQuestWidget.IsValid() == true)
	{
		// 실제 상태 정리는 퀘스트 화면의 공통 비활성화 이벤트에서 한 번만 처리한다.
		ActiveQuestWidget->DeactivateWidget();
	}

	if (ActiveLoanWidget.IsValid() == true)
	{
		// 실제 상태 정리는 대출 화면의 공통 비활성화 이벤트에서 한 번만 처리한다.
		ActiveLoanWidget->DeactivateWidget();
	}

	ActiveMenuWidget.Reset();
}

void AMD_Merchant::InitializePushedShop(UMD_ShopWidget* ShopWidget, UMD_MerchantMenuWidget* MenuWidget)
{
	// Blueprint에서 전달한 상점 화면의 유효성을 확인한다.
	if (IsValid(ShopWidget) == false)
	{
		return;
	}

	// 상점 화면을 연 메뉴의 유효성을 확인한다.
	if (IsValid(MenuWidget) == false)
	{
		return;
	}

	// 이후 상태 조회가 Blueprint에서 전달한 메뉴와 상점 화면을 사용하도록 저장한다.
	ActiveMenuWidget = MenuWidget;
	ActiveShopWidget = ShopWidget;

	// 상점 활성화 전에 카탈로그와 루트 메뉴를 전달한다.
	ShopWidget->SetCatalog(ShopCatalog);
	ShopWidget->SetOwningMerchantMenu(MenuWidget);

	// Common UI Stack의 모든 상점 종료 경로에서 상태를 정리하도록 비활성화 이벤트를 연결한다.
	ShopWidget->OnDeactivated().RemoveAll(this);
	ShopWidget->OnDeactivated().AddUObject(this, &AMD_Merchant::HandleShopDeactivated);

	// 상점 화면의 구매 요청을 실제 상인 처리에 중복 없이 연결한다.
	ShopWidget->OnPurchaseRejected.AddUniqueDynamic(this, &AMD_Merchant::HandleShopPurchaseRejected);
	ShopWidget->OnItemPurchaseRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleShopItemPurchaseRequested);

	// 서버 원장의 복제 미러 변경을 현재 상점 화면에 반영한다.
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForShop);
	}

	// 상점을 이용하는 플레이어의 장비 소유권 복제를 현재 상점 화면에 반영한다.
	AMD_PlayerState* InteractingPlayerState = GetInteractingPlayerState();
	if (IsValid(InteractingPlayerState) == true)
	{
		InteractingPlayerState->OnShopOwnershipChanged.AddUniqueDynamic(this, &AMD_Merchant::HandlePlayerShopOwnershipChanged);
	}

	// 연결을 마친 상점 화면에 현재 복제 상태를 즉시 적용한다.
	RefreshShopWidgetFromComponent();
}

void AMD_Merchant::InitializePushedQuest(UMD_QuestScreenWidget* QuestWidget, UMD_MerchantMenuWidget* MenuWidget)
{
	// Blueprint에서 전달한 퀘스트 화면의 유효성을 확인한다.
	if (IsValid(QuestWidget) == false)
	{
		return;
	}

	// 퀘스트 화면을 연 메뉴의 유효성을 확인한다.
	if (IsValid(MenuWidget) == false)
	{
		return;
	}

	// 이후 상태 조회가 Blueprint에서 전달한 메뉴와 퀘스트 화면을 사용하도록 저장한다.
	ActiveMenuWidget = MenuWidget;
	ActiveQuestWidget = QuestWidget;

	// 퀘스트 화면 활성화 전에 루트 메뉴를 전달한다.
	QuestWidget->SetOwningMerchantMenu(MenuWidget);

	// Common UI Stack의 모든 퀘스트 화면 종료 경로에서 상태를 정리하도록 비활성화 이벤트를 연결한다.
	QuestWidget->OnDeactivated().RemoveAll(this);
	QuestWidget->OnDeactivated().AddUObject(this, &AMD_Merchant::HandleQuestBackRequested);

	// 반복 퀘스트 요청을 실제 상인 처리에 중복 없이 연결한다.
	QuestWidget->OnRepeatAcceptRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleRepeatAcceptRequested);
	QuestWidget->OnRepeatDeclineRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleRepeatDeclineRequested);
	QuestWidget->OnRepeatAbandonRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleRepeatAbandonRequested);

	// 상인 퀘스트 상태가 바뀔 때 열린 화면을 갱신하도록 구독한다.
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}

	// 로어 수집 원장이 바뀔 때 열린 로어 탭을 갱신하도록 구독한다.
	UMD_LoreComponent* LoreComponent = GetLoreComponent();
	if (LoreComponent != nullptr)
	{
		LoreComponent->OnLoreProgressUpdated.AddUniqueDynamic(this, &AMD_Merchant::HandleLoreProgressUpdated);
	}

	// 연결을 마친 퀘스트 화면에 현재 복제 상태를 즉시 적용한다.
	RefreshQuestWidgetFromComponent();
	RefreshLoreWidgetFromComponent();
}

void AMD_Merchant::InitializePushedLoan(UMD_LoanWidget* LoanWidget, UMD_MerchantMenuWidget* MenuWidget)
{
	// Blueprint에서 전달한 대출 화면의 유효성 확인
	if (IsValid(LoanWidget) == false)
	{
		return;
	}

	// 대출 화면을 연 상인 메뉴의 유효성 확인
	if (IsValid(MenuWidget) == false)
	{
		return;
	}

	// 이후 상태 조회가 Blueprint에서 전달한 메뉴와 대출 화면을 사용하도록 저장
	ActiveMenuWidget = MenuWidget;
	ActiveLoanWidget = LoanWidget;

	// 대출 화면 활성화 전에 루트 메뉴를 전달하여 배경 메뉴 패널 숨김
	LoanWidget->SetOwningMerchantMenu(MenuWidget);

	// Common UI Stack의 모든 대출 화면 종료 경로에서 상태를 정리하도록 비활성화 이벤트 연결
	LoanWidget->OnDeactivated().RemoveAll(this);
	LoanWidget->OnDeactivated().AddUObject(this, &AMD_Merchant::HandleLoanDeactivated);

	// 대출과 상환 요청을 실제 상인 처리에 중복 없이 연결
	LoanWidget->OnBorrowRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleBorrowRequested);
	LoanWidget->OnRepayRequested.AddUniqueDynamic(this, &AMD_Merchant::HandleRepayRequested);

	// 복제 원장 변경을 현재 대출 화면과 상인 메뉴에 반영하도록 구독
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.AddUniqueDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}

	// 연결을 마친 대출 화면에 현재 복제 상태를 즉시 적용
	RefreshLoanWidgetFromComponent();
}

void AMD_Merchant::HandleShopDeactivated()
{
	// 비활성화된 상점에 남아 있는 상인 연결을 제거한다.
	UMD_ShopWidget* ShopWidget = ActiveShopWidget.Get();
	if (IsValid(ShopWidget) == true)
	{
		ShopWidget->OnDeactivated().RemoveAll(this);
		ShopWidget->OnPurchaseRejected.RemoveDynamic(this, &AMD_Merchant::HandleShopPurchaseRejected);
		ShopWidget->OnItemPurchaseRequested.RemoveDynamic(this, &AMD_Merchant::HandleShopItemPurchaseRequested);
	}

	// 더 이상 표시되지 않는 상점에 복제 상태를 전달하지 않도록 구독을 해제한다.
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForShop);

		// 상점에서 변경된 크레딧을 다시 표시되는 루트 메뉴에 반영한다.
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

	// 비활성화된 Stack 화면을 더 이상 활성 상점으로 취급하지 않는다.
	ActiveShopWidget.Reset();
}

void AMD_Merchant::HandleLoanDeactivated()
{
	// 비활성화된 대출 화면에 남아 있는 상인 연결 제거
	UMD_LoanWidget* LoanWidget = ActiveLoanWidget.Get();
	if (IsValid(LoanWidget) == true)
	{
		LoanWidget->OnDeactivated().RemoveAll(this);
		LoanWidget->OnBorrowRequested.RemoveDynamic(this, &AMD_Merchant::HandleBorrowRequested);
		LoanWidget->OnRepayRequested.RemoveDynamic(this, &AMD_Merchant::HandleRepayRequested);
	}

	// 더 이상 표시되지 않는 대출 화면에 복제 상태를 전달하지 않도록 구독 해제
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);

		// 대출로 변경된 크레딧과 부채 정보를 다시 표시되는 상인 메뉴에 반영
		if (ActiveMenuWidget.IsValid() == true)
		{
			ActiveMenuWidget->SetMenuInfo(MerchantComponent->BuildMenuInfo());
		}
	}

	// 다시 표시되는 상인 메뉴의 본문 패널 복구
	if (ActiveMenuWidget.IsValid() == true)
	{
		ActiveMenuWidget->SetMenuPanelVisible(true);
	}

	// 비활성화된 Stack 화면을 더 이상 활성 대출 화면으로 취급하지 않음
	ActiveLoanWidget.Reset();
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

void AMD_Merchant::HandleQuestBackRequested()
{
	// 비활성화된 퀘스트 화면에 남아 있는 상인 연결을 제거한다.
	UMD_QuestScreenWidget* QuestWidget = ActiveQuestWidget.Get();
	if (IsValid(QuestWidget) == true)
	{
		QuestWidget->OnDeactivated().RemoveAll(this);
		QuestWidget->OnRepeatAcceptRequested.RemoveDynamic(this, &AMD_Merchant::HandleRepeatAcceptRequested);
		QuestWidget->OnRepeatDeclineRequested.RemoveDynamic(this, &AMD_Merchant::HandleRepeatDeclineRequested);
		QuestWidget->OnRepeatAbandonRequested.RemoveDynamic(this, &AMD_Merchant::HandleRepeatAbandonRequested);
	}

	// 더 이상 표시되지 않는 퀘스트 화면에 상태를 전달하지 않도록 구독을 해제한다.
	UMD_MerchantComponent* MerchantComponent = GetMerchantComponent();
	if (MerchantComponent != nullptr)
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChanged);
	}

	UMD_LoreComponent* LoreComponent = GetLoreComponent();
	if (LoreComponent != nullptr)
	{
		LoreComponent->OnLoreProgressUpdated.RemoveDynamic(this, &AMD_Merchant::HandleLoreProgressUpdated);
	}

	// 비활성화된 Stack 화면을 더 이상 활성 퀘스트 화면으로 취급하지 않는다.
	ActiveQuestWidget.Reset();
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

	// 현재 일차와 일치하는 메인 퀘스트를 찾기 위한 복제 상태 조회
	int32 CurrentDay = MerchantComponent->GetCurrentDay();
	TArray<FMD_MainQuestState> MainQuests = MerchantComponent->GetMainQuests();
	TArray<FMD_MainQuestState> CurrentDayMainQuests;

	// 잠긴 미래 퀘스트와 이전 일차 퀘스트를 제외하고 당일 메인 퀘스트 한 건만 전달
	for (FMD_MainQuestState& MainQuest : MainQuests)
	{
		// 현재 일차가 아니거나 잠긴 퀘스트를 함께 제외
		if (MainQuest.UnlockDay != CurrentDay || MainQuest.Status == EMD_MainQuestStatus::Locked)
		{
			continue;
		}

		CurrentDayMainQuests.Add(MainQuest);
		break;
	}

	// 퀘스트 화면에는 당일 진행 대상 메인 퀘스트만 적용
	ActiveQuestWidget->SetMainQuests(CurrentDayMainQuests);
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


	// 퀘스트 데이터와 분리된 현장별 로어 카탈로그 접근을 위한 탐사 현장 정의
	AMD_PlayerController* InteractingPlayerController = GetInteractingPlayerController();
	// 유효성 검사와 현장 조회에 동일 컨트롤러 재사용
	UMD_SiteDefinition* ExpeditionSite = IsValid(InteractingPlayerController) == true ? InteractingPlayerController->GetExpeditionSite() : nullptr;


	// 탐사 현장이 없는 경우 사용할 빈 이름 준비
	FText SiteName = FText::GetEmpty();
	// 선택한 사이트 정의의 유효성 확인
	if (IsValid(ExpeditionSite) == true)
	{
		// 카탈로그와 별도로 관리되는 실제 사이트 이름 조회
		SiteName = ExpeditionSite->SiteName;
	}
	// 선택 사이트 이름과 로어 데이터를 함께 전달
	ActiveQuestWidget->SetLoreData(
		IsValid(ExpeditionSite) == true ? ExpeditionSite->LoreCatalog : nullptr, LoreComponent->GetDiscoveredLoreAssetIds(), SiteName);
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
