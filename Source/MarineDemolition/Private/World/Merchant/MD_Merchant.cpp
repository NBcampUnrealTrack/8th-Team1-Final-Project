// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Merchant/MD_Merchant.h"

#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

#include "Character/MD_Character.h"
#include "Component/Merchant/MD_MerchantComponent.h"
#include "Data/Shop/MD_ShopCatalog.h"
#include "Game/MD_GameStateBase.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Player/MD_PlayerController.h"
#include "UI/Merchant/MD_MerchantMenuWidget.h"
#include "UI/Merchant/MD_QuestWidget.h"
#include "UI/Merchant/MD_ShopWidget.h"

namespace
{
	/**
	 * 캐릭터 에셋 대체용 큐브를 사람 크기(60×60×180cm)로 맞추는 값.
	 * 엔진 기본 큐브가 100cm 정육면체라 스케일로 눌러 세운다.
	 */
	constexpr float PlaceholderWidthScale = 0.6f;
	constexpr float PlaceholderHeightScale = 1.8f;

	/** 큐브 피벗이 가운데라 절반 높이만큼 올려야 액터 원점이 발밑에 온다. */
	constexpr float PlaceholderHalfHeight = 90.f;
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

	// 대출 화면이 생기면 HandleShopRequested와 같은 패턴으로 갈아끼운다:
	// 화면을 띄운 뒤 SetMenuButtonsVisible(false), 뒤로가기에서 다시 true.
	Widget->OnLoanRequested.AddDynamic(this, &AMD_Merchant::HandleNotReadyRequested);

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
		ActiveShopWidget->RemoveFromParent();
		ActiveShopWidget.Reset();
	}

	if (ActiveQuestWidget.IsValid())
	{
		if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
		{
			MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForQuest);
		}
		ActiveQuestWidget->RemoveFromParent();
		ActiveQuestWidget.Reset();
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

	// 메뉴가 들고 있는 정보를 그대로 넘겨 두 화면의 수치가 갈리지 않게 한다.
	ShopWidget->SetShopInfo(ActiveMenuWidget->GetMenuInfo());

	ShopWidget->OnBackRequested.AddDynamic(this, &AMD_Merchant::HandleShopBackRequested);
	ShopWidget->OnPurchaseRejected.AddDynamic(this, &AMD_Merchant::HandleShopPurchaseRejected);

	// 메뉴 위젯 자체는 살려둔다 — 파괴하면 입력 모드가 초기화되고 대사창도 함께 사라진다.
	// 대신 메뉴 패널을 숨겨서 상점과 겹치지 않게 한다.
	ShopWidget->SetOwningMerchantMenu(ActiveMenuWidget.Get());
	ShopWidget->AddToViewport();
	ActiveShopWidget = ShopWidget;
}

void AMD_Merchant::HandleShopBackRequested()
{
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

void AMD_Merchant::HandleShopPurchaseRejected(FName ItemId)
{
	// 대사창은 메뉴 위젯에 있다. 상점은 그 위에 겹쳐 뜨지만 하단을 비워두므로 가려지지 않는다.
	if (ActiveMenuWidget.IsValid())
	{
		ActiveMenuWidget->ShowDialogueLine(FullStockDialogue);
	}
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
		MerchantComponent->OnMerchantStateChanged.AddDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForQuest);
	}

	RefreshQuestWidgetFromComponent();
}

void AMD_Merchant::HandleQuestBackRequested()
{
	if (UMD_MerchantComponent* MerchantComponent = GetMerchantComponent())
	{
		MerchantComponent->OnMerchantStateChanged.RemoveDynamic(this, &AMD_Merchant::HandleMerchantStateChangedForQuest);
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

void AMD_Merchant::HandleMerchantStateChangedForQuest()
{
	RefreshQuestWidgetFromComponent();
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

UMD_MerchantComponent* AMD_Merchant::GetMerchantComponent() const
{
	const UWorld* World = GetWorld();
	const AMD_GameStateBase* GameState = IsValid(World) ? World->GetGameState<AMD_GameStateBase>() : nullptr;

	return IsValid(GameState) ? GameState->GetMerchantComponent() : nullptr;
}

AMD_PlayerController* AMD_Merchant::GetInteractingPlayerController() const
{
	return ActiveMenuWidget.IsValid() ? Cast<AMD_PlayerController>(ActiveMenuWidget->GetOwningPlayer()) : nullptr;
}
