// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Interaction/MD_TravelConsole.h"

#include "Character/MD_Character.h"
#include "Online/Session/MD_TravelPreloadSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "CommonActivatableWidget.h"
#include "UI/Interaction/MD_TravelScreen.h"
#include "UI/MD_LocalUIManager.h"


AMD_TravelConsole::AMD_TravelConsole()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);

	// 현장 로스터 기본값. TR-9 / SS 벨몬트는 레벨이 아직 없어 목록에만 남긴다 (04 §3)
	// 레벨 참조는 코드에 박지 않는다 — 배치된 BP_MD_TravelConsole의 Sites 디폴트에서 에디터로 지정한다
	FMD_TravelSiteEntry& SiteX7 = Sites.AddDefaulted_GetRef();
	SiteX7.DisplayName = NSLOCTEXT("MDTravel", "Site_X7", "X-7 해저 연구기지");
	SiteX7.bAvailable = true;

	FMD_TravelSiteEntry& SiteTR9 = Sites.AddDefaulted_GetRef();
	SiteTR9.DisplayName = NSLOCTEXT("MDTravel", "Site_TR9", "TR-9 관통선");
	SiteTR9.bAvailable = false;

	FMD_TravelSiteEntry& SiteBelmont = Sites.AddDefaulted_GetRef();
	SiteBelmont.DisplayName = NSLOCTEXT("MDTravel", "Site_Belmont", "SS 벨몬트");
	SiteBelmont.bAvailable = false;
}

void AMD_TravelConsole::BeginPlay()
{
    Super::BeginPlay();
    if (auto* GI = GetGameInstance())
        if (auto* Preload = GI->GetSubsystem<UMD_TravelPreloadSubsystem>())
        {
            // Only one configured available destination, never every entry in the roster.
            if (Sites.IsEmpty()) Preload->QueueDestination(TargetLevel);
            else for (int32 I = 0; I < Sites.Num(); ++I)
                if (auto Level = ResolveSiteLevel(I); !Level.IsNull())
                { Preload->QueueDestination(Level); break; }
        }
}

TSoftObjectPtr<UWorld> AMD_TravelConsole::ResolveSiteLevel(int32 SiteIndex) const
{
	if (!Sites.IsValidIndex(SiteIndex))
	{
		// 목록을 비워둔 배치 액터는 기존 TargetLevel 하나로 동작
		return Sites.Num() == 0 ? TargetLevel : TSoftObjectPtr<UWorld>();
	}

	const FMD_TravelSiteEntry& Entry = Sites[SiteIndex];

	// 준비 중 현장은 서버에서 거부
	return Entry.bAvailable ? Entry.Level : TSoftObjectPtr<UWorld>();
}

void AMD_TravelConsole::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_TravelConsole::Interact_Implementation(AActor* Interactor)
{
	// 화면을 사용할 상호작용 캐릭터 조회
	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);
	if (IsValid(InstigatorCharacter) == false)
	{
		return; // 지원하지 않는 상호작용 대상
	}

	// 상호작용 캐릭터의 화면 소유 플레이어 조회
	APlayerController* PC = Cast<APlayerController>(InstigatorCharacter->GetController());
	if (IsValid(PC) == false)
	{
		return; // 화면 소유자 부재
	}
	if (PC->IsLocalController() == false)
	{
		return; // 원격 플레이어의 로컬 화면 생성 차단
	}

	// 등록된 항해 화면을 찾을 수 있는 태그인지 확인
	if (ConsoleWidgetTag.IsValid() == false)
	{
		return; // 화면 요청 태그 부재
	}

	// 메뉴 레이어를 소유한 로컬 플레이어 조회
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (IsValid(LocalPlayer) == false)
	{
		return; // 로컬 화면 컨텍스트 부재
	}
	// 화면 생성과 중복 확인을 담당하는 관리자 조회
	UMD_LocalUIManager* LocalUIManager = LocalPlayer->GetSubsystem<UMD_LocalUIManager>();
	if (IsValid(LocalUIManager) == false)
	{
		return; // 메뉴 레이어 관리자 부재
	}

	// 화면 태그에 등록된 클래스와 현재 사용할 레이어 조회
	FGameplayTag LayerTag; // 화면을 표시할 레이어
	TSoftClassPtr<UCommonActivatableWidget> RegisteredWidgetClass; // UI 설정에 등록된 화면 클래스
	if (LocalUIManager->ResolveWidgetRegistration(ConsoleWidgetTag, LayerTag, RegisteredWidgetClass) == false)
	{
		return; // 화면 등록 또는 레이어 부재
	}

	// 등록된 클래스 로드 후 항해 화면의 초기화 계약 검증
	UClass* ScreenClass = RegisteredWidgetClass.LoadSynchronous();
	if (IsValid(ScreenClass) == false)
	{
		return; // 화면 클래스 로드 실패
	}
	if (ScreenClass->IsChildOf(UCommonActivatableWidget::StaticClass()) == false ||
		ScreenClass->ImplementsInterface(UMD_TravelScreen::StaticClass()) == false)
	{
		return; // 콘솔 정보를 전달할 수 없는 화면
	}

	// 활성 화면을 재사용할 때에도 현재 상호작용한 콘솔 전달
	UCommonActivatableWidget* ActiveScreen = LocalUIManager->GetActiveScreen(LayerTag);
	if (IsValid(ActiveScreen) == true && ActiveScreen->GetClass() == ScreenClass)
	{
		IMD_TravelScreen::Execute_InitializeTravelScreen(ActiveScreen, this); // 재사용 화면의 콘솔 연결 갱신
		return; // 같은 화면의 중복 생성 방지
	}

	// 메뉴 활성화 전에 화면의 초기화 이벤트로 콘솔 전달
	LocalUIManager->PushScreenToLayerInitialized(LayerTag, ScreenClass,
		[this](UCommonActivatableWidget& Screen)
		{
			IMD_TravelScreen::Execute_InitializeTravelScreen(&Screen, this); // 화면별 자식 탭 연결 처리
		});
}
