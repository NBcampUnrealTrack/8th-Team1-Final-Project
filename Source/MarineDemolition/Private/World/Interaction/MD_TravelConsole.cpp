// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Interaction/MD_TravelConsole.h"

#include "Character/MD_Character.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "UI/Interaction/MD_CommonTravelWidget.h"
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
	// GameMenu Stack에 화면을 Push할 로컬 UI 관리자를 저장한다.
	// 설정된 UI 클래스가 실제 UObject 클래스인지 확인한다.
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	UMD_LocalUIManager* LocalUIManager = IsValid(LocalPlayer) == true ? LocalPlayer->GetSubsystem<UMD_LocalUIManager>() : nullptr;
	if (IsValid(LocalUIManager) == false || IsValid(ConsoleWidgetClass) == false)
	{
		return;
	}

	// 이미 활성화된 같은 UI 화면이 있으면 중복 Push하지 않는다.
	UCommonActivatableWidget* ActiveGameMenuWidget = LocalUIManager->GetActiveScreen(MDGameplayTags::UI_Layer_GameMenu);
	if (IsValid(ActiveGameMenuWidget) == true && ActiveGameMenuWidget->IsA(ConsoleWidgetClass) == true)
	{
		return;
	}

	// Stack 활성화 전에 현재 콘솔을 새 화면에 연결하면서 GameMenu 레이어에 Push한다.
	UCommonActivatableWidget* PushedScreen = LocalUIManager->PushScreenToLayerInitialized(
		ConsoleWidgetClass,
		[this](UCommonActivatableWidget& Screen)
		{
			// 생성된 화면을 출항 전용 위젯으로 변환한다.
			UMD_CommonTravelWidget* TravelWidget = Cast<UMD_CommonTravelWidget>(&Screen);
			if (IsValid(TravelWidget) == false)
			{
				return;
			}

			// 화면이 활성화되기 전에 현재 콘솔 데이터를 연결한다.
			TravelWidget->SetOwningConsole(this);
		});
}
