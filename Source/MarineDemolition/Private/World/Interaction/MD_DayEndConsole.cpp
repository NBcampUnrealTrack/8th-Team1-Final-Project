// Fill out your copyright notice in the Description page of Project Settings.


#include "World/Interaction/MD_DayEndConsole.h"
#include "Component/Interaction/MD_InteractionPromptComponent.h"

#include "Character/MD_Character.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "UI/Game/MD_DailyReportWidget.h"
#include "Engine/LocalPlayer.h"
#include "UI/MD_LocalUIManager.h"


AMD_DayEndConsole::AMD_DayEndConsole()
{
	// 인스턴스와 파생 BP가 행동 유형을 선택하는 기본 구성 생성
	InteractionPrompt = CreateDefaultSubobject<UMD_InteractionPromptComponent>(TEXT("InteractionPrompt"));
	// 기존 상호작용 동작에 대응하는 공통 문구 유형 지정
	InteractionPrompt->PromptType = EMD_InteractionPromptType::EndDay;

	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace, ECR_Block);
}

void AMD_DayEndConsole::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	if (IsValid(Mesh))
	{
		Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue);
		Mesh->SetRenderCustomDepth(bHighlighted);
	}
}

void AMD_DayEndConsole::Interact_Implementation(AActor* Interactor)
{
	// 상호작용 캐릭터를 통한 로컬 플레이어 확인
	AMD_Character* InstigatorCharacter = Cast<AMD_Character>(Interactor);
	if (IsValid(InstigatorCharacter) == false)
	{
		return;
	}

	// 플레이어 컨트롤러 형식 확인
	APlayerController* PC = Cast<APlayerController>(InstigatorCharacter->GetController());
	// 컨트롤러 유효성, 로컬 소유 여부와 화면 태그를 순서대로 확인
	if (IsValid(PC) == false || PC->IsLocalController() == false || ConsoleWidgetTag.IsValid() == false)
	{
		return;
	}

	// 현재 콘솔 화면의 중복 활성화 방지
	UCommonActivatableWidget* ExistingWidget = ActiveWidget.Get();
	if (IsValid(ExistingWidget) == true && ExistingWidget->IsActivated() == true)
	{
		return;
	}

	// 해당 플레이어의 레이어 관리자 조회
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (IsValid(LocalPlayer) == false)
	{
		return;
	}
	// 로컬 플레이어별 UI 관리 경로 사용
	UMD_LocalUIManager* UIManager = LocalPlayer->GetSubsystem<UMD_LocalUIManager>();
	if (IsValid(UIManager) == false)
	{
		return;
	}

	// 태그에 등록된 화면 클래스와 부모 레이어 조회
	FGameplayTag LayerTag;
	// 동기 로드할 등록 클래스 보관
	TSoftClassPtr<UCommonActivatableWidget> RegisteredWidgetClass;
	if (UIManager->ResolveWidgetRegistration(ConsoleWidgetTag, LayerTag, RegisteredWidgetClass) == false)
	{
		return;
	}

	// 레이어에 추가하기 전 등록 클래스 로드
	UClass* ScreenClass = RegisteredWidgetClass.LoadSynchronous();
	if (IsValid(ScreenClass) == false)
	{
		return;
	}
	// 활성화 전에 현재 콘솔을 전달하여 사용
	auto InitializeScreen = [this](UCommonActivatableWidget& Screen)
	{
        // 보고서 UI의 퇴근 버튼에 서버 검증 콘솔 전달
        UMD_DailyReportWidget* ReportWidget = Cast<UMD_DailyReportWidget>(&Screen);
        if (IsValid(ReportWidget) == true)
        {
            // 활성화 전에 현재 콘솔의 마감 요청 컨텍스트 구성
            ReportWidget->SetOwningConsole(this);
        }
	};
	// UI Manager가 선택한 레이어의 스택을 통한 화면 생성
	UCommonActivatableWidget* PushedScreen = UIManager->PushScreenToLayerInitialized(LayerTag, ScreenClass, InitializeScreen);
	// BP 태그에 등록된 모든 CommonActivatable 화면의 생성 결과 확인
	if (IsValid(PushedScreen) == false)
	{
		return;
	}

    // 최상단 화면 재사용 시 생략되는 소유 콘솔 초기화 보완
    InitializeScreen(*PushedScreen);
	// 위젯 종류와 무관한 활성화 기반 중복 요청 방지
	ActiveWidget = PushedScreen;
}
